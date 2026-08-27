// Speculation Fabric — speculation request types.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "speculation_fabric/core/id.hpp"
#include "speculation_fabric/core/model.hpp"
#include "speculation_fabric/core/policy.hpp"

namespace speculation_fabric {

// A request to perform speculative inference over a sequence. The request
// carries the authoritative prefix, the target (verifier) and draft (proposer)
// model, and the policy that governs speculation.
struct SpeculationRequest {
    RequestId id{};
    SequenceId sequence{};
    TenantId tenant{};
    ClientId client{};
    ModelIdentity target_model{};     // verifier / authoritative model
    ModelIdentity draft_model{};      // proposer / speculative model
    ModelPairCompatibilityKey pair_key{};
    std::vector<std::uint32_t> prefix{};   // initial authoritative tokens
    std::uint32_t max_attempts{3};
    std::optional<std::uint64_t> deadline_ns{};  // absolute deadline (monotonic)
    ProposalPolicy policy{};
    std::string prompt_label{};

    bool operator==(const SpeculationRequest&) const = default;
};

// Classification of the outcome of one speculative cycle.
enum class CycleOutcomeKind : std::uint8_t {
    ProposalSuccess = 0,
    ProposalRetryableFailure = 1,
    ProposalNonRetryableFailure = 2,
    VerificationSuccessFullAccept = 3,
    VerificationSuccessPartialAccept = 4,
    VerificationRejectAll = 5,
    VerificationRetryableFailure = 6,
    VerificationNonRetryableFailure = 7,
    Cancelled = 8,
    Expired = 9,
    StaleAuthorityRejected = 10,
    Superseded = 11,
};

// Result of completing one speculative cycle.
struct CycleOutcome {
    CycleOutcomeKind outcome{CycleOutcomeKind::ProposalSuccess};
    SpeculativePhase phase{SpeculativePhase::Ready};
    AcceptanceOutcome acceptance{AcceptanceOutcome::RejectAll};
    CommitResult commit{};
    RollbackResult rollback{};
    ProposalId proposal{};
    BranchId branch{};
    std::uint32_t proposed_tokens{0};
    std::uint32_t accepted_tokens{0};
    std::uint32_t rejected_tokens{0};
    std::uint32_t depth{0};
    std::string summary{};
    ErrorCode error{ErrorCode::ok};

    [[nodiscard]] bool committed() const noexcept { return commit.success; }
    [[nodiscard]] bool rolled_back() const noexcept { return rollback.success; }

    static CycleOutcome make(CycleOutcomeKind k) {
        CycleOutcome o;
        o.outcome = k;
        return o;
    }
};

}  // namespace speculation_fabric
