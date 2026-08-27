// Speculation Fabric — CUDA proposer/verifier executors.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Real CUDA execution of bounded speculative-inference-like numerical work.
// The kernels mirror the deterministic CPU synthetic model so that device
// output can be independently cross-validated against the CPU reference.
// CUDA is one proven backend and is never assumed by the runtime type system.

#pragma once

#include "speculation_fabric/core/executor.hpp"

namespace speculation_fabric {

// CUDA proposer: allocates real device memory, transfers the synthetic model
// state, runs a proposer kernel, synchronizes, and transfers candidate tokens
// back to the host.
class CudaProposerExecutor final : public ProposalExecutor {
public:
    explicit CudaProposerExecutor(std::uint32_t aligned_tokens = 0,
                                  std::uint32_t branch_index = 0)
        : aligned_tokens_(aligned_tokens), branch_index_(branch_index) {}

    [[nodiscard]] ExecutorIdentity identity() const override;
    [[nodiscard]] ExecutorKind kind() const noexcept override { return ExecutorKind::CUDA; }

    Result<ProposalResult> propose(const ProposalInput& input) override;

private:
    std::uint32_t aligned_tokens_;
    std::uint32_t branch_index_;
};

// CUDA verifier: computes the target token sequence on the device and compares
// it to the candidate, returning per-position acceptance.
class CudaVerifierExecutor final : public VerificationExecutor {
public:
    CudaVerifierExecutor() = default;

    [[nodiscard]] ExecutorIdentity identity() const override;
    [[nodiscard]] ExecutorKind kind() const noexcept override { return ExecutorKind::CUDA; }

    Result<AcceptanceResult> verify(const VerificationInput& input) override;
};

}  // namespace speculation_fabric
