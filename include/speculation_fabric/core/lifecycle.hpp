// Speculation Fabric — speculative cycle state machine.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// A speculative cycle progresses through an explicit, guarded lifecycle.
// Transitions are declared as a table; a transition is only legal if declared
// in the table AND its guard predicate passes. Terminal phases are absorbing.
//
// The authoritative sequence is the source of truth. No speculative output
// becomes authoritative without valid verification; rejected, stale,
// duplicate, mismatched, superseded, or unauthorized work never mutates
// authoritative state.

#pragma once

#include <cstdint>
#include <optional>

#include "speculation_fabric/core/error.hpp"

namespace speculation_fabric {

// The phases of a single speculative cycle.
enum class SpeculativePhase : std::uint8_t {
    Ready = 0,                     // awaiting a proposal decision
    ProposalPlanned = 1,           // a proposal was planned
    ProposalReserved = 2,          // resources reserved for the proposal
    ProposalDispatched = 3,        // proposal dispatched to a proposer
    ProposalRunning = 4,           // proposer is executing
    ProposalProduced = 5,          // proposer produced a candidate
    VerificationPlanned = 6,       // verification was planned
    VerificationReserved = 7,      // resources reserved for verification
    VerificationDispatched = 8,    // verification dispatched to a verifier
    VerificationRunning = 9,       // verifier is executing
    Verified = 10,                 // verification returned a result
    PartiallyAccepted = 11,        // a prefix was accepted
    FullyAccepted = 12,            // the whole candidate was accepted
    Rejected = 13,                 // verification rejected all
    RolledBack = 14,               // rejected suffix / branch rolled back
    CancelRequested = 15,          // cancellation was requested
    Cancelled = 16,                // cancellation took effect
    RetryPending = 17,             // a retry is pending (new attempt)
    Superseded = 18,               // a newer cycle superseded this one
    Expired = 19,                  // deadline pressure forced expiry
    Failed = 20,                   // non-retryable failure
    Committed = 21,                // committed to the authoritative sequence
    Terminal = 22,                 // absorbing terminal state
};

// Events that drive a speculative cycle forward.
enum class SpeculationEvent : std::uint8_t {
    Null = 0,
    PlanProposal = 1,
    ReserveProposal = 2,
    DispatchProposal = 3,
    ProposalStarted = 4,
    ProposalFinished = 5,          // success: produce candidate
    ProposalRetry = 6,             // retryable proposal failure
    ProposalFail = 7,              // non-retryable proposal failure
    PlanVerification = 8,
    ReserveVerification = 9,
    DispatchVerification = 10,
    VerificationStarted = 11,
    VerificationFinished = 12,     // result produced (outcome decides next)
    AcceptPartial = 13,
    AcceptFull = 14,
    RejectAll = 15,
    Commit = 16,
    Rollback = 17,
    Cancel = 18,
    Retry = 19,
    Supersede = 20,
    Expire = 21,
    Fail = 22,
    EnterTerminal = 23,
};

const char* to_string(SpeculativePhase phase) noexcept;
const char* to_string(SpeculationEvent event) noexcept;

// Returns true when the phase is absorbing (no further transitions).
bool is_terminal_phase(SpeculativePhase phase) noexcept;

// The canonical transition table. Returns the target phase for a legal
// transition, or nullopt if the transition is undeclared.
std::optional<SpeculativePhase> transition_target(SpeculativePhase from,
                                                  SpeculationEvent event) noexcept;

// Whether a transition is declared in the table (ignores the guard).
bool transition_declared(SpeculativePhase from, SpeculationEvent event) noexcept;

// ---------------------------------------------------------------------------
// A guarded state machine for one speculative cycle. Guards are supplied by
// the caller before applying. A transition requires BOTH a declared table
// entry AND a passing guard. If a guard fails for a declared transition the
// machine returns a structured error (e.g. invalid_state / stale_authority).
// ---------------------------------------------------------------------------
class SpeculativeStateMachine {
public:
    SpeculativeStateMachine() = default;
    explicit SpeculativeStateMachine(SpeculativePhase start) : phase_(start) {}

    [[nodiscard]] SpeculativePhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool is_terminal() const noexcept { return is_terminal_phase(phase_); }

    // Applies a declared+guarded transition. on_guard may be null (no guard).
    // Returns the new phase on success, or a structured error.
    Result<SpeculativePhase> apply(SpeculationEvent event, bool guard = true,
                                   ErrorCode guard_error = ErrorCode::invalid_transition,
                                   std::string guard_message = {}) {
        if (!transition_declared(phase_, event)) {
            return Result<SpeculativePhase>::err(
                ErrorCode::invalid_transition,
                std::string(to_string(phase_)) + " --" + to_string(event) +
                    "--> (undeclared)");
        }
        if (!guard) {
            return Result<SpeculativePhase>::err(
                guard_error,
                std::string("guard blocked ") + to_string(event) +
                    std::string(guard_message.empty() ? "" : (": " + guard_message)));
        }
        const auto next = transition_target(phase_, event);
        if (!next) {
            return Result<SpeculativePhase>::err(ErrorCode::internal,
                                                 "declared transition resolved to null");
        }
        phase_ = *next;
        return Result<SpeculativePhase>::ok(phase_);
    }

private:
    SpeculativePhase phase_{SpeculativePhase::Ready};
};

}  // namespace speculation_fabric
