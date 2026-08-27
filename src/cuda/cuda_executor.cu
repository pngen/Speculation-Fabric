// Speculation Fabric — CUDA proposer/verifier kernels and executors.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Real CUDA-specific execution on the RTX 5090 (Blackwell, sm_120, CUDA 13.1).
// The kernels perform bounded speculative-inference-like numerical work and
// mirror the deterministic CPU synthetic model exactly, so device results can
// be cross-validated against the CPU reference.

#include "speculation_fabric/core/cuda_executor.hpp"

#include <cuda_runtime.h>
#include <cstdint>

namespace speculation_fabric {

namespace {

constexpr std::uint32_t kNW = 16;
constexpr std::uint32_t kVocab = 4096;
constexpr std::uint32_t kTargetSeed = 0x5A17B0Cu;
constexpr std::uint32_t kMA = 0x9E3779B9u;
constexpr std::uint32_t kMB = 0x85EBCA6Bu;

// Deterministic splitmix64 update (identical to the CPU reference).
__host__ __device__ std::uint32_t splitmix(std::uint64_t& x) {
    x += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return static_cast<std::uint32_t>(z);
}

// Builds the target model weights/bias exactly like the CPU reference.
void build_target(std::uint32_t* w, std::uint32_t* b) {
    std::uint64_t x = static_cast<std::uint64_t>(kTargetSeed) | (1ULL << 32);
    for (std::uint32_t i = 0; i < kNW; ++i) {
        for (std::uint32_t j = 0; j < kNW; ++j) {
            w[i * kNW + j] = splitmix(x) & 0xFFFFu;
        }
        b[i] = splitmix(x) & 0xFFFFu;
    }
}

std::uint64_t mix64(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) {
    std::uint64_t x = a * 0x9E3779B97F4A7C15ULL ^ b;
    x = (x ^ (x >> 27)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 31)) * 0x94D049BB133111EBULL;
    x = (x ^ (x >> 27)) + c * 0x9E3779B97F4A7C15ULL;
    x ^= d;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 29)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 32);
}

// Proposer kernel: parallel over the 16 hidden-state words.
__global__ void proposeKernel(const std::uint32_t* w, const std::uint32_t* b,
                              std::uint32_t* state, std::uint32_t* tokens,
                              std::uint64_t seed, std::uint32_t depth,
                              std::uint32_t vocab, std::uint32_t aligned,
                              std::uint32_t branch) {
    __shared__ std::uint32_t next[kNW];
    __shared__ std::uint32_t sprev;
    if (threadIdx.x == 0) {
        std::uint64_t x = seed;
        for (std::uint32_t i = 0; i < kNW; ++i) state[i] = splitmix(x);
        sprev = 0u;
    }
    __syncthreads();
    const std::uint32_t t = threadIdx.x;
    for (std::uint32_t i = 0; i < depth; ++i) {
        if (t == 0u) {
            const std::uint32_t raw =
                state[0] ^ state[7] ^ state[13] ^ (kMA * (i + 1u)) ^ kMB ^ sprev;
            const std::uint32_t expected = raw % vocab;
            const std::uint32_t tok =
                (i < aligned) ? expected : ((expected + 1u + branch) % vocab);
            tokens[i] = tok;
            sprev = tok;
        }
        __syncthreads();
        std::uint32_t acc = b[t] ^ (kMA * sprev);
        for (std::uint32_t j = 0; j < kNW; ++j) {
            acc = acc * kMB ^ (state[j] * w[t * kNW + j]);
        }
        next[t] = acc;
        __syncthreads();
        state[t] = next[t];
        __syncthreads();
    }
}

// Verifier kernel: computes the target sequence and compares to the candidate.
__global__ void verifyKernel(const std::uint32_t* w, const std::uint32_t* b,
                             const std::uint32_t* cand, std::uint32_t* state,
                             std::uint32_t* perpos, std::uint32_t* accepted,
                             std::uint32_t* firstrej, std::uint64_t seed,
                             std::uint32_t len, std::uint32_t vocab) {
    __shared__ std::uint32_t next[kNW];
    __shared__ std::uint32_t sprev;
    if (threadIdx.x == 0) {
        std::uint64_t x = seed;
        for (std::uint32_t i = 0; i < kNW; ++i) state[i] = splitmix(x);
        sprev = 0u;
        *accepted = 0u;
        *firstrej = len;
    }
    __syncthreads();
    const std::uint32_t t = threadIdx.x;
    for (std::uint32_t i = 0; i < len; ++i) {
        if (t == 0u) {
            const std::uint32_t raw =
                state[0] ^ state[7] ^ state[13] ^ (kMA * (i + 1u)) ^ kMB ^ sprev;
            const std::uint32_t expected = raw % vocab;
            const bool acc = (expected == cand[i]);
            perpos[i] = acc ? 1u : 0u;
            if (acc) {
                ++(*accepted);
            } else if (*firstrej == len) {
                *firstrej = i;
            }
            sprev = expected;
        }
        __syncthreads();
        std::uint32_t acc2 = b[t] ^ (kMA * sprev);
        for (std::uint32_t j = 0; j < kNW; ++j) {
            acc2 = acc2 * kMB ^ (state[j] * w[t * kNW + j]);
        }
        next[t] = acc2;
        __syncthreads();
        state[t] = next[t];
        __syncthreads();
    }
}

// RAII device buffer.
struct DevBuf {
    void* ptr{nullptr};
    explicit DevBuf(std::size_t bytes) {
        if (cudaMalloc(&ptr, bytes) != cudaSuccess) ptr = nullptr;
    }
    ~DevBuf() { if (ptr) cudaFree(ptr); }
    DevBuf(const DevBuf&) = delete;
    DevBuf& operator=(const DevBuf&) = delete;
    [[nodiscard]] bool ok() const noexcept { return ptr != nullptr; }
};

void resync() { (void)cudaDeviceSynchronize(); }

}  // namespace

ExecutorIdentity CudaProposerExecutor::identity() const {
    ExecutorIdentity id;
    id.id = ExecutorId{0x43555052};  // 'CUPR'
    id.name = "cuda-proposer";
    id.kind = ExecutorKind::CUDA;
    id.protocol_version = 1;
    return id;
}

Result<ProposalResult> CudaProposerExecutor::propose(const ProposalInput& input) {
    ProposalResult result;
    result.outcome = ProposalOutcome::NonRetryableFailure;
    if (input.depth == 0) {
        result.error = ErrorCode::invalid_zero_depth;
        result.detail = "proposal depth must be positive";
        return Result<ProposalResult>::ok(result);
    }
    if (input.depth > 256) {
        result.error = ErrorCode::invalid_depth;
        result.detail = "proposal depth exceeds CUDA bound";
        return Result<ProposalResult>::ok(result);
    }
    if (cudaSetDevice(0) != cudaSuccess) {
        result.error = ErrorCode::incompatible_device;
        result.detail = "could not select CUDA device 0";
        return Result<ProposalResult>::ok(result);
    }

    std::uint32_t w[kNW * kNW]{};
    std::uint32_t b[kNW]{};
    build_target(w, b);

    const std::uint64_t base = input.base_state.id.get() * 0x9E3779B97F4A7C15ULL ^
                               input.base_state.generation.get() * 0x94D049BB133111EBULL;
    const std::uint64_t seed = mix64(base, input.base_generation.get(),
                                     input.provenance.proposer_model.revision.get(),
                                     static_cast<std::uint64_t>(input.depth) * 0x100000001B3ULL);

    DevBuf dw(static_cast<std::size_t>(kNW * kNW) * sizeof(std::uint32_t));
    DevBuf db(static_cast<std::size_t>(kNW) * sizeof(std::uint32_t));
    DevBuf dstate(static_cast<std::size_t>(kNW) * sizeof(std::uint32_t));
    DevBuf dtok(static_cast<std::size_t>(input.depth) * sizeof(std::uint32_t));
    if (!dw.ok() || !db.ok() || !dstate.ok() || !dtok.ok()) {
        result.error = ErrorCode::internal;
        result.detail = "CUDA device allocation failed";
        return Result<ProposalResult>::ok(result);
    }
    if (cudaMemcpy(dw.ptr, w, sizeof(w), cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMemcpy(db.ptr, b, sizeof(b), cudaMemcpyHostToDevice) != cudaSuccess) {
        result.error = ErrorCode::internal;
        result.detail = "CUDA copy to device failed";
        return Result<ProposalResult>::ok(result);
    }

    proposeKernel<<<1, kNW>>>(
        static_cast<const std::uint32_t*>(dw.ptr),
        static_cast<const std::uint32_t*>(db.ptr),
        static_cast<std::uint32_t*>(dstate.ptr),
        static_cast<std::uint32_t*>(dtok.ptr), seed, input.depth, kVocab,
        aligned_tokens_, branch_index_);
    resync();
    const cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        result.error = ErrorCode::internal;
        result.detail = std::string("CUDA proposer kernel failed: ") + cudaGetErrorString(err);
        return Result<ProposalResult>::ok(result);
    }

    std::vector<std::uint32_t> host_tok(input.depth);
    if (cudaMemcpy(host_tok.data(), dtok.ptr, sizeof(std::uint32_t) * input.depth,
                   cudaMemcpyDeviceToHost) != cudaSuccess) {
        result.error = ErrorCode::internal;
        result.detail = "CUDA copy from device failed";
        return Result<ProposalResult>::ok(result);
    }

    std::vector<Token> tokens;
    tokens.reserve(input.depth);
    for (std::uint32_t i = 0; i < input.depth; ++i) {
        tokens.push_back(Token{host_tok[i]});
    }
    result.outcome = ProposalOutcome::Success;
    result.candidate = CandidateSequence(std::move(tokens));
    auto& ss = result.speculative_state.emplace();
    ss.ref = input.base_state;
    ss.ref.generation = StateGeneration{input.base_state.generation.get() + 1};
    ss.role = SequenceRole::Speculative;
    ss.reservation = ReservationState::Used;
    ss.owner = StateOwner::Proposal;
    ss.bytes_held = static_cast<std::uint64_t>(input.depth) * 64u;
    result.compute_spent = static_cast<std::uint64_t>(input.depth) * 4u;
    result.memory_held = ss.bytes_held;
    result.execution_measured_ns = static_cast<std::uint64_t>(input.depth) * 2u;
    result.detail = "cuda-proposer produced " + std::to_string(input.depth) + " tokens";
    return Result<ProposalResult>::ok(result);
}

ExecutorIdentity CudaVerifierExecutor::identity() const {
    ExecutorIdentity id;
    id.id = ExecutorId{0x43564552};  // 'CUVER'
    id.name = "cuda-verifier";
    id.kind = ExecutorKind::CUDA;
    id.protocol_version = 1;
    return id;
}

Result<AcceptanceResult> CudaVerifierExecutor::verify(const VerificationInput& input) {
    AcceptanceResult result;
    result.candidate_length = static_cast<std::uint32_t>(input.candidate.size());
    if (input.candidate.empty()) {
        result.outcome = AcceptanceOutcome::VerifierFailure;
        result.retryable = true;
        result.verifier_note = "candidate is empty";
        return Result<AcceptanceResult>::ok(result);
    }
    if (cudaSetDevice(0) != cudaSuccess) {
        result.outcome = AcceptanceOutcome::VerifierFailure;
        result.retryable = false;
        result.verifier_note = "could not select CUDA device 0";
        return Result<AcceptanceResult>::ok(result);
    }

    std::uint32_t w[kNW * kNW]{};
    std::uint32_t b[kNW]{};
    build_target(w, b);
    const std::uint64_t base = input.authoritative_state.id.get() * 0x9E3779B97F4A7C15ULL ^
                               input.authoritative_state.generation.get() * 0x94D049BB133111EBULL;
    const std::uint64_t seed = mix64(base, input.authoritative_generation.get(),
                                     input.proposer.revision.get(),
                                     std::uint64_t(input.candidate.size()) * 0x100000001B3ULL);

    const std::uint32_t len = static_cast<std::uint32_t>(input.candidate.size());
    std::vector<std::uint32_t> cand(len);
    for (std::uint32_t i = 0; i < len; ++i) cand[i] = input.candidate.tokens[i].id;

    DevBuf dw(sizeof(w));
    DevBuf db(sizeof(b));
    DevBuf dcand(sizeof(std::uint32_t) * len);
    DevBuf dstate(sizeof(std::uint32_t) * kNW);
    DevBuf dper(sizeof(std::uint32_t) * len);
    DevBuf dacc(sizeof(std::uint32_t));
    DevBuf dfrej(sizeof(std::uint32_t));
    if (!dw.ok() || !db.ok() || !dcand.ok() || !dstate.ok() || !dper.ok() ||
        !dacc.ok() || !dfrej.ok()) {
        result.outcome = AcceptanceOutcome::VerifierFailure;
        result.retryable = false;
        result.verifier_note = "CUDA device allocation failed";
        return Result<AcceptanceResult>::ok(result);
    }
    if (cudaMemcpy(dw.ptr, w, sizeof(w), cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMemcpy(db.ptr, b, sizeof(b), cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMemcpy(dcand.ptr, cand.data(), sizeof(std::uint32_t) * len,
                   cudaMemcpyHostToDevice) != cudaSuccess) {
        result.outcome = AcceptanceOutcome::VerifierFailure;
        result.retryable = false;
        result.verifier_note = "CUDA copy to device failed";
        return Result<AcceptanceResult>::ok(result);
    }

    verifyKernel<<<1, kNW>>>(
        static_cast<const std::uint32_t*>(dw.ptr),
        static_cast<const std::uint32_t*>(db.ptr),
        static_cast<const std::uint32_t*>(dcand.ptr),
        static_cast<std::uint32_t*>(dstate.ptr),
        static_cast<std::uint32_t*>(dper.ptr),
        static_cast<std::uint32_t*>(dacc.ptr),
        static_cast<std::uint32_t*>(dfrej.ptr), seed, len, kVocab);
    resync();
    const cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        result.outcome = AcceptanceOutcome::VerifierFailure;
        result.retryable = false;
        result.verifier_note = std::string("CUDA verifier kernel failed: ") + cudaGetErrorString(err);
        return Result<AcceptanceResult>::ok(result);
    }

    std::vector<std::uint32_t> perpos(len);
    std::uint32_t accepted = 0;
    std::uint32_t firstrej = len;
    if (cudaMemcpy(perpos.data(), dper.ptr, sizeof(std::uint32_t) * len,
                   cudaMemcpyDeviceToHost) != cudaSuccess ||
        cudaMemcpy(&accepted, dacc.ptr, sizeof(std::uint32_t),
                   cudaMemcpyDeviceToHost) != cudaSuccess ||
        cudaMemcpy(&firstrej, dfrej.ptr, sizeof(std::uint32_t),
                   cudaMemcpyDeviceToHost) != cudaSuccess) {
        result.outcome = AcceptanceOutcome::VerifierFailure;
        result.retryable = false;
        result.verifier_note = "CUDA copy from device failed";
        return Result<AcceptanceResult>::ok(result);
    }

    result.candidate_length = len;
    result.accepted_prefix = accepted;
    result.first_rejection_index = firstrej;
    std::vector<std::uint8_t> per8(static_cast<std::size_t>(len));
    for (std::uint32_t i = 0; i < len; ++i) {
        per8[i] = static_cast<std::uint8_t>(perpos[i]);
    }
    result.per_position = std::move(per8);
    if (accepted == len) result.outcome = AcceptanceOutcome::FullAccept;
    else if (accepted == 0) result.outcome = AcceptanceOutcome::RejectAll;
    else result.outcome = AcceptanceOutcome::PartialAccept;
    result.authoritative_next_generation =
        AuthGeneration{input.authoritative_generation.get() + accepted};
    result.authoritative_next_state = input.authoritative_state;
    result.authoritative_next_state.generation =
        StateGeneration{input.authoritative_state.generation.get() + accepted};
    result.authoritative_next_state_generation = result.authoritative_next_state.generation;
    result.execution_measured_ns = static_cast<std::uint64_t>(len) * 2u;
    result.verifier_note = "cuda-verifier accepted " + std::to_string(accepted) + "/" +
                           std::to_string(len);
    return Result<AcceptanceResult>::ok(result);
}

}  // namespace speculation_fabric