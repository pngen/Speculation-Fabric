// Speculation Fabric — proposal and verification executor interfaces.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Executors are the only place device/framework specifics live. The runtime
// defines the contract; a CPU or CUDA implementation satisfies it. Backends
// are selected by identity and never leak into the runtime's semantics.

#pragma once

#include <cstdint>
#include <string>

#include "speculation_fabric/core/candidate.hpp"
#include "speculation_fabric/core/error.hpp"
#include "speculation_fabric/core/id.hpp"
#include "speculation_fabric/core/model.hpp"
#include "speculation_fabric/core/proposal.hpp"
#include "speculation_fabric/core/state.hpp"
#include "speculation_fabric/core/verification.hpp"

namespace speculation_fabric {

// Outcomes for a proposal cycle. Members are handled independently; a retry
// operation is distinct from a terminal rejection.
enum class ProposalOutcome : std::uint8_t {
    Success = 0,
    RetryableFailure = 1,
    NonRetryableFailure = 2,
    Cancelled = 3,
    Expired = 4,
    StaleAuthorityRejected = 5,
};

// The result of a proposal operation, with enough accounting to drive
// speculative economics and reservation release.
struct ProposalResult {
    ProposalOutcome outcome{ProposalOutcome::NonRetryableFailure};
    std::optional<CandidateSequence> candidate{};
    std::optional<StateDescriptor> speculative_state{};
    std::uint64_t compute_spent{0};       // measured
    std::uint64_t memory_held{0};         // measured
    std::uint64_t execution_measured_ns{0};
    ErrorCode error{ErrorCode::ok};
    std::string detail{};

    [[nodiscard]] bool success() const noexcept {
        return outcome == ProposalOutcome::Success;
    }
    [[nodiscard]] bool retryable() const noexcept {
        return outcome == ProposalOutcome::RetryableFailure;
    }

    bool operator==(const ProposalResult&) const = default;
};

// Input to a proposal operation. The executor receives the full authority
// envelope and the determinism seed for the request.
struct ProposalInput {
    ProposalProvenance provenance{};
    StateRef base_state{};
    AuthGeneration base_generation{};
    CandidateDepth depth{0};
    std::uint32_t branch_index{0};        // which branch within a multi-branch plan
    std::uint32_t branch_count{1};
    std::uint64_t deterministic_seed{0};  // derived from provenance
    std::uint64_t compute_budget{0};      // max compute budget
    std::uint64_t memory_budget{0};       // max memory budget

    bool operator==(const ProposalInput&) const = default;
};

// A deterministic proposer: given the same authority envelope and base state
// it reproduces the same candidate. Prior-step state must affect later tokens;
// a proposer that ignores its input state is a correctness bug, not a demo.
class ProposalExecutor {
public:
    virtual ~ProposalExecutor() = default;

    [[nodiscard]] virtual ExecutorIdentity identity() const = 0;
    [[nodiscard]] virtual ExecutorKind kind() const noexcept = 0;

    // Produce a candidate sequence of ProposalInput::depth tokens from
    // base_state. The returned candidate is final.
    virtual Result<ProposalResult> propose(const ProposalInput& input) = 0;
};

// A deterministic verifier: validates a candidate against its own independent
// verifier state, returning per-position acceptance. Verification is authority;
// a verifier must never be coerced by the proposer's output.
class VerificationExecutor {
public:
    virtual ~VerificationExecutor() = default;

    [[nodiscard]] virtual ExecutorIdentity identity() const = 0;
    [[nodiscard]] virtual ExecutorKind kind() const noexcept = 0;

    virtual Result<AcceptanceResult> verify(const VerificationInput& input) = 0;
};

}  // namespace speculation_fabric
