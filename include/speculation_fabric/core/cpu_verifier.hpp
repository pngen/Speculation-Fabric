// Speculation Fabric — deterministic CPU verifier executor.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// A real verifier that computes the target token sequence from its own
// independent verifier model state, then accepts the candidate on the prefix
// where the candidate tokens agree with the verifier's expectations. The
// acceptance prefix emerges from the numerical relationship between the
// proposer's draft model and the verifier's target model, never from a
// hardcoded vector.

#pragma once

#include <cstdint>
#include <vector>

#include "speculation_fabric/core/executor.hpp"
#include "speculation_fabric/core/cpu_proposer.hpp"

namespace speculation_fabric {

struct CpuVerifierConfig {
    std::uint32_t state_seed{0xBEEFu};
    std::uint32_t weight_seed{0xBADA55u};
    // The verifier's target model weights. When a proposer is a perfect draft
    // (draft_quality=1.0) it reproduces these exactly -> full acceptance.
    // A proposer that diverges produces a shorter acceptance prefix.
    bool verify_from_candidate_identity{true};
};

class CpuVerifierExecutor final : public VerificationExecutor {
public:
    explicit CpuVerifierExecutor(CpuVerifierConfig cfg = {}) : cfg_(cfg) {}

    [[nodiscard]] ExecutorIdentity identity() const override;
    [[nodiscard]] ExecutorKind kind() const noexcept override { return ExecutorKind::CPU; }

    Result<AcceptanceResult> verify(const VerificationInput& input) override;

private:
    CpuVerifierConfig cfg_;
};

}  // namespace speculation_fabric
