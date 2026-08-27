// Speculation Fabric — deterministic CPU proposer executor.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// A real, bounded, deterministic proposer. It runs a stateful recurrence over
// synthetic "model" state: the hidden state depends on the base state seed,
// and each produced token feeds back into the next hidden state, so prior
// tokens affect later candidates. It never sleeps, never returns a hardcoded
// token vector, and never uses a random source unrelated to the input state.

#pragma once

#include <cstdint>
#include <vector>

#include "speculation_fabric/core/executor.hpp"

namespace speculation_fabric {

// Deterministic synthetic model state: a small fixed-width state vector and a
// weight matrix. All arithmetic is integer modular so results are identical
// across platforms and runs.
// A perfect draft (aligned_tokens >= depth) reproduces the verifier's target
// sequence exactly, giving full acceptance. This seed is shared by the
// deterministic CPU proposer and verifier.
inline constexpr std::uint32_t kTargetModelSeed = 0x5A17B0Cu;

struct SyntheticModel {
    // 16 hidden-state words.
    static constexpr std::uint32_t kStateWords = 16;
    // 16 x 16 weight matrix for the hidden-state update.
    std::uint32_t weights[kStateWords][kStateWords]{};
    // Per-word bias.
    std::uint32_t bias[kStateWords]{};
    // Two scalar mixing constants.
    std::uint32_t mix_a{0x9E3779B9u};
    std::uint32_t mix_b{0x85EBCA6Bu};

    static std::uint32_t splitmix64_update(std::uint64_t& x) noexcept {
        x += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        return static_cast<std::uint32_t>(z);
    }

    // Initializes state words deterministically from a 64-bit seed.
    void seed_state(std::uint64_t seed, std::uint32_t* state) const noexcept {
        std::uint64_t x = seed;
        for (std::uint32_t i = 0; i < kStateWords; ++i) {
            state[i] = splitmix64_update(x);
        }
    }

    // Advances the hidden state by one step given the previous token.
    void step(std::uint32_t* state, std::uint32_t prev_token) const noexcept {
        std::uint32_t next[kStateWords]{};
        for (std::uint32_t i = 0; i < kStateWords; ++i) {
            std::uint32_t acc = bias[i] ^ mix_a * prev_token;
            for (std::uint32_t j = 0; j < kStateWords; ++j) {
                acc = acc * mix_b ^ state[j] * weights[i][j];
            }
            next[i] = acc;
        }
        for (std::uint32_t i = 0; i < kStateWords; ++i) state[i] = next[i];
    }

    // Derives a token id from the hidden state and the iteration index.
    std::uint32_t token(std::uint32_t* state, std::uint32_t index,
                        std::uint32_t prev_token, std::uint32_t vocab_size) const noexcept {
        const std::uint32_t raw = state[0] ^ state[7] ^ state[13] ^
                                  mix_a * (index + 1u) ^ mix_b ^ prev_token;
        return vocab_size == 0 ? 0u : (raw % vocab_size);
    }
};

// Configures a deterministic CPU proposer.
struct CpuProposerConfig {
    std::uint32_t state_seed{0xC0FFEEu};
    std::uint32_t weight_seed{0xF00Du};
    // Number of leading candidate tokens that exactly match the verifier's
    // target model (the "correct draft prefix"). 0 => reject immediately;
    // a value >= depth => full acceptance; 0 < value < depth => partial.
    std::uint32_t aligned_tokens{0};
    // A perturbed model used after the aligned prefix, so the draft diverges
    // from the target after alignment.
    std::uint32_t divergence_seed{0xDEADBEEFu};
    bool monotonic_tokens{true};
};

class CpuProposerExecutor final : public ProposalExecutor {
public:
    explicit CpuProposerExecutor(CpuProposerConfig cfg = {}) : cfg_(cfg) {}

    [[nodiscard]] ExecutorIdentity identity() const override;
    [[nodiscard]] ExecutorKind kind() const noexcept override { return ExecutorKind::CPU; }

    Result<ProposalResult> propose(const ProposalInput& input) override;

private:
    CpuProposerConfig cfg_;
};

}  // namespace speculation_fabric