// Speculation Fabric — verification, dispatch, and result types.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "speculation_fabric/core/candidate.hpp"
#include "speculation_fabric/core/compat.hpp"
#include "speculation_fabric/core/error.hpp"
#include "speculation_fabric/core/id.hpp"
#include "speculation_fabric/core/model.hpp"
#include "speculation_fabric/core/proposal.hpp"
#include "speculation_fabric/core/state.hpp"

namespace speculation_fabric {

// ---------------------------------------------------------------------------
// Verification input
// ---------------------------------------------------------------------------
// A verifier always receives the full authority envelope for the work it is
// asked to validate, so it can reject stale, duplicate, or mismatched work.
struct VerificationInput {
    RequestId request{};
    SequenceId sequence{};
    AttemptId attempt{};
    BranchId branch{};
    ProposalId proposal{};
    ProposalGeneration proposal_generation{};
    AuthGeneration authoritative_generation{};   // authoritative base gen
    StateRef authoritative_state{};               // authoritative state identity
    StateGeneration authoritative_state_generation{};
    CandidateSequence candidate{};                // the proposed tokens
    CandidateTokenIdentity candidate_identity{};
    ModelIdentity proposer{};
    ModelIdentity verifier{};
    ModelPairCompatibilityKey compatibility{};
    StateRef verification_state{};                // where verification runs
    DispatchId dispatch{};                        // dispatch authority token
    CoordinatorEpoch epoch{};
    WorkerId verifier_worker{};
    WorkerBootId verifier_boot{};

    bool operator==(const VerificationInput&) const = default;
};

// Output of a verification, classified.
enum class AcceptanceOutcome : std::uint8_t {
    FullAccept = 0,
    PartialAccept = 1,
    RejectAll = 2,
    VerifierFailure = 3,
};

// The per-candidate-position result. The acceptance prefix is the sequence
// of positions [0, accepted_prefix) that the verifier accepted; the position
// at first_rejection_index is the first rejected (or it equals candidate
// length when everything was accepted).
struct AcceptanceResult {
    AcceptanceOutcome outcome{AcceptanceOutcome::RejectAll};
    std::uint32_t candidate_length{0};
    std::uint32_t accepted_prefix{0};
    std::uint32_t first_rejection_index{0};
    std::vector<std::uint8_t> per_position{};      // 1=accepted, 0=rejected
    bool retryable{false};
    bool terminal{false};
    AuthGeneration authoritative_next_generation{};
    StateRef authoritative_next_state{};
    StateGeneration authoritative_next_state_generation{};
    std::uint64_t execution_measured_ns{0};        // measured, not estimated
    std::string verifier_note{};

    [[nodiscard]] bool fully_accepted() const noexcept {
        return outcome == AcceptanceOutcome::FullAccept;
    }
    [[nodiscard]] bool fully_rejected() const noexcept {
        return outcome == AcceptanceOutcome::RejectAll;
    }
    [[nodiscard]] bool is_failure() const noexcept {
        return outcome == AcceptanceOutcome::VerifierFailure;
    }
    [[nodiscard]] std::uint32_t rejected_count() const noexcept {
        return candidate_length - accepted_prefix;
    }

    bool operator==(const AcceptanceResult&) const = default;
};

// A rejection result for a branch that is not selected or whose suffix is
// rejected. The rejected-from index marks where the branch diverges.
struct RejectionResult {
    bool rejected_all{true};
    std::uint32_t accepted_prefix{0};
    std::uint32_t rejected_from_index{0};
    std::uint32_t rejected_tokens{0};
    std::string reason{};

    bool operator==(const RejectionResult&) const = default;
};

// The result of committing an accepted prefix into authoritative state.
struct CommitResult {
    std::uint32_t committed_tokens{0};
    std::uint32_t accepted_prefix{0};
    std::uint32_t rejected_tokens{0};
    AuthGeneration new_generation{};
    StateRef new_state{};
    StateGeneration new_state_generation{};
    bool success{false};
    std::string detail{};

    bool operator==(const CommitResult&) const = default;
};

// The result of rolling back a rejected suffix / branch.
struct RollbackResult {
    std::uint32_t rejected_tokens{0};
    std::uint32_t accepted_prefix{0};
    AuthGeneration restored_generation{};
    StateRef restored_state{};
    StateGeneration restored_state_generation{};
    std::uint32_t retired_branches{0};
    std::uint32_t released_reservations{0};
    bool success{false};
    std::string detail{};

    bool operator==(const RollbackResult&) const = default;
};

// ---------------------------------------------------------------------------
// Plan / dispatch carriers
// ---------------------------------------------------------------------------
// A plan is the scheduler's decision; a dispatch is the actual unit of work
// handed to an executor or worker. Both are inspectable so an operator can
// answer "why this proposer / why this depth / why this verifier".

struct ProposalPlan {
    RequestId request{};
    SequenceId sequence{};
    AttemptId attempt{};
    CycleId cycle{};
    BranchId branch{};
    ProposalId proposal{};
    CandidateDepth depth{0};
    std::uint32_t branch_count{1};
    ModelIdentity proposer{};
    WorkerId proposer_worker{};
    DispatchId dispatch{};
    std::string rationale{};      // deterministic explanation of the choice

    bool operator==(const ProposalPlan&) const = default;
};

struct VerificationPlan {
    RequestId request{};
    SequenceId sequence{};
    AttemptId attempt{};
    BranchId branch{};
    ProposalId proposal{};
    ProposalGeneration proposal_generation{};
    ModelIdentity verifier{};
    WorkerId verifier_worker{};
    DispatchId dispatch{};
    std::string rationale{};

    bool operator==(const VerificationPlan&) const = default;
};

struct ProposalDispatch {
    ProposalPlan plan{};
    StateRef base_state{};
    AuthGeneration base_generation{};
    CandidateDepth depth{0};
    std::uint32_t branch_count{1};

    bool operator==(const ProposalDispatch&) const = default;
};

struct VerificationDispatch {
    VerificationPlan plan{};
    VerificationInput input{};

    bool operator==(const VerificationDispatch&) const = default;
};

}  // namespace speculation_fabric
