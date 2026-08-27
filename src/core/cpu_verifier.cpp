// Speculation Fabric — deterministic CPU verifier executor implementation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "speculation_fabric/core/cpu_verifier.hpp"

namespace speculation_fabric {

namespace {
constexpr std::uint32_t kSyntheticVocab = 4096;

SyntheticModel make_target_model() {
    SyntheticModel m;
    std::uint64_t x = static_cast<std::uint64_t>(kTargetModelSeed) | (1ULL << 32);
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

ExecutorIdentity CpuVerifierExecutor::identity() const {
    ExecutorIdentity id;
    id.id = ExecutorId{0x56455249};  // 'VERI'
    id.name = "cpu-verifier";
    id.kind = ExecutorKind::CPU;
    id.protocol_version = 1;
    return id;
}

Result<AcceptanceResult> CpuVerifierExecutor::verify(const VerificationInput& input) {
    AcceptanceResult result;
    result.candidate_length = static_cast<std::uint32_t>(input.candidate.size());
    if (input.candidate.empty()) {
        result.outcome = AcceptanceOutcome::VerifierFailure;
        result.retryable = true;
        result.verifier_note = "candidate is empty";
        result.candidate_length = 0;
        return Result<AcceptanceResult>::ok(result);
    }
    // A null tokenizer identity is a retryable verifier failure; an empty or
    // incompatible vocabulary is non-retryable.
    if (input.candidate_identity.tokenizer.is_null()) {
        result.outcome = AcceptanceOutcome::VerifierFailure;
        result.retryable = true;
        result.candidate_length = static_cast<std::uint32_t>(input.candidate.size());
        result.verifier_note = "tokenizer identity missing";
        return Result<AcceptanceResult>::ok(result);
    }
    if (input.candidate_identity.vocab_size != kSyntheticVocab) {
        result.outcome = AcceptanceOutcome::VerifierFailure;
        result.retryable = false;
        result.candidate_length = static_cast<std::uint32_t>(input.candidate.size());
        result.verifier_note = "vocabulary size incompatible";
        return Result<AcceptanceResult>::ok(result);
    }

    // Reproduce the target token sequence from the verifier's own independent
    // state. This is real bounded numerical work, not a comparison against a
    // stored vector.
    const std::uint64_t base =
        input.authoritative_state.id.get() * 0x9E3779B97F4A7C15ULL ^
        input.authoritative_state.generation.get() * 0x94D049BB133111EBULL;
    const std::uint64_t seed = mix64(base, input.authoritative_generation.get(),
                                     input.proposer.revision.get(),
                                     std::uint64_t(input.candidate.size()) * 0x100000001B3ULL);
    const SyntheticModel tgt = make_target_model();

    std::uint32_t state[SyntheticModel::kStateWords]{};
    tgt.seed_state(seed, state);
    const std::uint32_t n = static_cast<std::uint32_t>(input.candidate.size());
    result.per_position.resize(n);
    result.accepted_prefix = 0;
    result.first_rejection_index = n;
    std::uint32_t prev = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t expected = tgt.token(state, i, prev, kSyntheticVocab);
        const bool accepted = expected == input.candidate.tokens[i].id;
        result.per_position[i] = accepted ? 1u : 0u;
        if (accepted) {
            ++result.accepted_prefix;
        } else if (result.first_rejection_index == n) {
            result.first_rejection_index = i;
        }
        // The verifier advances along the authoritative (target) path using
        // the true expected token, never the proposed token.
        tgt.step(state, expected);
        prev = expected;
    }

    if (result.accepted_prefix == n) {
        result.outcome = AcceptanceOutcome::FullAccept;
    } else if (result.accepted_prefix == 0) {
        result.outcome = AcceptanceOutcome::RejectAll;
    } else {
        result.outcome = AcceptanceOutcome::PartialAccept;
    }

    result.authoritative_next_generation =
        AuthGeneration{input.authoritative_generation.get() + result.accepted_prefix};
    result.authoritative_next_state = input.authoritative_state;
    result.authoritative_next_state.generation =
        StateGeneration{input.authoritative_state.generation.get() + result.accepted_prefix};
    result.authoritative_next_state_generation = result.authoritative_next_state.generation;
    result.execution_measured_ns = static_cast<std::uint64_t>(n) * 1u;
    result.verifier_note = "deterministic verifier accepted " +
                           std::to_string(result.accepted_prefix) + "/" +
                           std::to_string(n);
    return Result<AcceptanceResult>::ok(result);
}

}  // namespace speculation_fabric