// Speculation Fabric — deterministic CPU proposer executor implementation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "speculation_fabric/core/cpu_proposer.hpp"


namespace speculation_fabric {

namespace {

// Canonical target model seed shared by the proposer (at quality 1.0) and the
// verifier, so that a perfect draft reproduces the target exactly.
// (shared target seed is defined in cpu_proposer.hpp)

SyntheticModel make_model(std::uint32_t weight_seed) {
    SyntheticModel m;
    std::uint64_t x = static_cast<std::uint64_t>(weight_seed) | (1ULL << 32);
    for (std::uint32_t i = 0; i < SyntheticModel::kStateWords; ++i) {
        for (std::uint32_t j = 0; j < SyntheticModel::kStateWords; ++j) {
            m.weights[i][j] = SyntheticModel::splitmix64_update(x) & 0xFFFFu;
        }
        m.bias[i] = SyntheticModel::splitmix64_update(x) & 0xFFFFu;
    }
    return m;
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

}  // namespace

// GPU/CPU-independent vocabulary size for the synthetic model.
constexpr std::uint32_t kSyntheticVocab = 4096;

ExecutorIdentity CpuProposerExecutor::identity() const {
    ExecutorIdentity id;
    id.id = ExecutorId{0x50524F50};  // 'PROP'
    id.name = "cpu-proposer";
    id.kind = ExecutorKind::CPU;
    id.protocol_version = 1;
    return id;
}

Result<ProposalResult> CpuProposerExecutor::propose(const ProposalInput& input) {
    ProposalResult result;
    if (input.depth == 0) {
        result.outcome = ProposalOutcome::NonRetryableFailure;
        result.error = ErrorCode::invalid_zero_depth;
        result.detail = "proposal depth must be positive";
        return Result<ProposalResult>::ok(result);
    }
    if (input.depth > 256) {
        result.outcome = ProposalOutcome::NonRetryableFailure;
        result.error = ErrorCode::invalid_depth;
        result.detail = "proposal depth exceeds synthetic model bound";
        return Result<ProposalResult>::ok(result);
    }
    if (input.compute_budget != 0 && input.depth > input.compute_budget) {
        result.outcome = ProposalOutcome::RetryableFailure;
        result.error = ErrorCode::budget_exceeded;
        result.detail = "proposal would exceed compute budget";
        return Result<ProposalResult>::ok(result);
    }

    // Deterministic base seed from the authority envelope. The seed is a pure
    // function of the authoritative state and the proposer revision so the
    // verifier can reproduce the identical target sequence.
    const auto& p = input.provenance;
    const std::uint64_t base = input.base_state.id.get() * 0x9E3779B97F4A7C15ULL ^
                               input.base_state.generation.get() * 0x94D049BB133111EBULL;
    const std::uint64_t seed = mix64(base, input.base_generation.get(),
                                     p.proposer_model.revision.get(),
                                     std::uint64_t(input.depth) * 0x100000001B3ULL);

    // The target model the verifier uses. The proposer reproduces the target's
    // expected token for the first cfg_.aligned_tokens positions (the "correct
    // draft prefix"), then emits a deterministically perturbed token so the
    // draft diverges exactly at the alignment boundary. This is a genuine,
    // reproducible divergence: the verifier independently recomputes the target
    // sequence and the acceptance prefix emerges from the comparison.
    const SyntheticModel tgt = make_model(kTargetModelSeed);

    std::vector<Token> tokens;
    tokens.reserve(input.depth);
    std::uint32_t state[SyntheticModel::kStateWords]{};
    tgt.seed_state(seed, state);
    std::uint32_t prev = 0;
    for (std::uint32_t i = 0; i < input.depth; ++i) {
        const std::uint32_t expected = tgt.token(state, i, prev, kSyntheticVocab);
        const std::uint32_t tok =
            (i < cfg_.aligned_tokens)
                ? expected
                : static_cast<std::uint32_t>((expected + 1u + input.branch_index) % kSyntheticVocab);
        tokens.push_back(Token{tok});
        tgt.step(state, tok);
        prev = tok;
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
    result.compute_spent = static_cast<std::uint64_t>(input.depth) * 8u;
    result.memory_held = ss.bytes_held;
    result.execution_measured_ns = static_cast<std::uint64_t>(input.depth) * 1u;
    result.detail = "proposer produced " + std::to_string(input.depth) + " tokens";
    return Result<ProposalResult>::ok(result);
}

}  // namespace speculation_fabric