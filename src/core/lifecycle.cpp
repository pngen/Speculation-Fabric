// Speculation Fabric — speculative cycle state machine implementation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "speculation_fabric/core/lifecycle.hpp"

namespace speculation_fabric {

namespace {
const char* kPhaseNames[] = {
    "Ready", "ProposalPlanned", "ProposalReserved", "ProposalDispatched",
    "ProposalRunning", "ProposalProduced", "VerificationPlanned",
    "VerificationReserved", "VerificationDispatched", "VerificationRunning",
    "Verified", "PartiallyAccepted", "FullyAccepted", "Rejected", "RolledBack",
    "CancelRequested", "Cancelled", "RetryPending", "Superseded", "Expired",
    "Failed", "Committed", "Terminal",
};
const char* kEventNames[] = {
    "Null", "PlanProposal", "ReserveProposal", "DispatchProposal",
    "ProposalStarted", "ProposalFinished", "ProposalRetry", "ProposalFail",
    "PlanVerification", "ReserveVerification", "DispatchVerification",
    "VerificationStarted", "VerificationFinished", "AcceptPartial",
    "AcceptFull", "RejectAll", "Commit", "Rollback", "Cancel", "Retry",
    "Supersede", "Expire", "Fail", "EnterTerminal",
};

struct Transition {
    SpeculativePhase from;
    SpeculationEvent event;
    SpeculativePhase to;
};

// The canonical transition table. Every legal transition of a speculative
// cycle is enumerated here; anything not listed is rejected.
constexpr Transition kTransitions[] = {
    // Proposal phase
    {SpeculativePhase::Ready, SpeculationEvent::PlanProposal, SpeculativePhase::ProposalPlanned},
    {SpeculativePhase::ProposalPlanned, SpeculationEvent::ReserveProposal, SpeculativePhase::ProposalReserved},
    {SpeculativePhase::ProposalReserved, SpeculationEvent::DispatchProposal, SpeculativePhase::ProposalDispatched},
    {SpeculativePhase::ProposalDispatched, SpeculationEvent::ProposalStarted, SpeculativePhase::ProposalRunning},
    {SpeculativePhase::ProposalRunning, SpeculationEvent::ProposalFinished, SpeculativePhase::ProposalProduced},
    {SpeculativePhase::ProposalRunning, SpeculationEvent::ProposalRetry, SpeculativePhase::RetryPending},
    {SpeculativePhase::ProposalRunning, SpeculationEvent::ProposalFail, SpeculativePhase::Failed},

    // Verification phase
    {SpeculativePhase::ProposalProduced, SpeculationEvent::PlanVerification, SpeculativePhase::VerificationPlanned},
    {SpeculativePhase::VerificationPlanned, SpeculationEvent::ReserveVerification, SpeculativePhase::VerificationReserved},
    {SpeculativePhase::VerificationReserved, SpeculationEvent::DispatchVerification, SpeculativePhase::VerificationDispatched},
    {SpeculativePhase::VerificationDispatched, SpeculationEvent::VerificationStarted, SpeculativePhase::VerificationRunning},
    {SpeculativePhase::VerificationRunning, SpeculationEvent::VerificationFinished, SpeculativePhase::Verified},
    {SpeculativePhase::Verified, SpeculationEvent::AcceptPartial, SpeculativePhase::PartiallyAccepted},
    {SpeculativePhase::Verified, SpeculationEvent::AcceptFull, SpeculativePhase::FullyAccepted},
    {SpeculativePhase::Verified, SpeculationEvent::RejectAll, SpeculativePhase::Rejected},
    {SpeculativePhase::VerificationRunning, SpeculationEvent::ProposalRetry, SpeculativePhase::RetryPending},
    {SpeculativePhase::VerificationRunning, SpeculationEvent::ProposalFail, SpeculativePhase::Failed},

    // Commit / rollback
    {SpeculativePhase::FullyAccepted, SpeculationEvent::Commit, SpeculativePhase::Committed},
    {SpeculativePhase::PartiallyAccepted, SpeculationEvent::Commit, SpeculativePhase::Committed},
    {SpeculativePhase::PartiallyAccepted, SpeculationEvent::Rollback, SpeculativePhase::RolledBack},
    {SpeculativePhase::Rejected, SpeculationEvent::Rollback, SpeculativePhase::RolledBack},
    {SpeculativePhase::RolledBack, SpeculationEvent::Retry, SpeculativePhase::Ready},

    // Retry
    {SpeculativePhase::RetryPending, SpeculationEvent::Retry, SpeculativePhase::Ready},

    // Cancellation
    {SpeculativePhase::Ready, SpeculationEvent::Cancel, SpeculativePhase::CancelRequested},
    {SpeculativePhase::ProposalPlanned, SpeculationEvent::Cancel, SpeculativePhase::CancelRequested},
    {SpeculativePhase::ProposalReserved, SpeculationEvent::Cancel, SpeculativePhase::CancelRequested},
    {SpeculativePhase::ProposalDispatched, SpeculationEvent::Cancel, SpeculativePhase::CancelRequested},
    {SpeculativePhase::ProposalRunning, SpeculationEvent::Cancel, SpeculativePhase::CancelRequested},
    {SpeculativePhase::ProposalProduced, SpeculationEvent::Cancel, SpeculativePhase::CancelRequested},
    {SpeculativePhase::VerificationPlanned, SpeculationEvent::Cancel, SpeculativePhase::CancelRequested},
    {SpeculativePhase::VerificationDispatched, SpeculationEvent::Cancel, SpeculativePhase::CancelRequested},
    {SpeculativePhase::VerificationRunning, SpeculationEvent::Cancel, SpeculativePhase::CancelRequested},
    {SpeculativePhase::CancelRequested, SpeculationEvent::Expire, SpeculativePhase::Cancelled},

    // Supersession / expiry / failure
    {SpeculativePhase::Ready, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::ProposalPlanned, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::ProposalReserved, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::ProposalDispatched, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::ProposalRunning, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::ProposalProduced, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::VerificationPlanned, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::VerificationDispatched, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::VerificationRunning, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::Verified, SpeculationEvent::Supersede, SpeculativePhase::Superseded},
    {SpeculativePhase::RetryPending, SpeculationEvent::Supersede, SpeculativePhase::Superseded},

    {SpeculativePhase::Ready, SpeculationEvent::Expire, SpeculativePhase::Expired},
    {SpeculativePhase::ProposalPlanned, SpeculationEvent::Expire, SpeculativePhase::Expired},
    {SpeculativePhase::ProposalReserved, SpeculationEvent::Expire, SpeculativePhase::Expired},
    {SpeculativePhase::ProposalDispatched, SpeculationEvent::Expire, SpeculativePhase::Expired},
    {SpeculativePhase::ProposalRunning, SpeculationEvent::Expire, SpeculativePhase::Expired},
    {SpeculativePhase::ProposalProduced, SpeculationEvent::Expire, SpeculativePhase::Expired},
    {SpeculativePhase::VerificationPlanned, SpeculationEvent::Expire, SpeculativePhase::Expired},
    {SpeculativePhase::VerificationDispatched, SpeculationEvent::Expire, SpeculativePhase::Expired},
    {SpeculativePhase::VerificationRunning, SpeculationEvent::Expire, SpeculativePhase::Expired},
    {SpeculativePhase::RetryPending, SpeculationEvent::Expire, SpeculativePhase::Expired},

    {SpeculativePhase::Ready, SpeculationEvent::Fail, SpeculativePhase::Failed},
    {SpeculativePhase::ProposalPlanned, SpeculationEvent::Fail, SpeculativePhase::Failed},
    {SpeculativePhase::ProposalReserved, SpeculationEvent::Fail, SpeculativePhase::Failed},
    {SpeculativePhase::ProposalDispatched, SpeculationEvent::Fail, SpeculativePhase::Failed},
    {SpeculativePhase::ProposalRunning, SpeculationEvent::Fail, SpeculativePhase::Failed},
    {SpeculativePhase::VerificationRunning, SpeculationEvent::Fail, SpeculativePhase::Failed},
    {SpeculativePhase::PartiallyAccepted, SpeculationEvent::Fail, SpeculativePhase::Failed},
    {SpeculativePhase::Rejected, SpeculationEvent::Fail, SpeculativePhase::Failed},

    // Terminal absorb
    {SpeculativePhase::Committed, SpeculationEvent::EnterTerminal, SpeculativePhase::Terminal},
    {SpeculativePhase::RolledBack, SpeculationEvent::EnterTerminal, SpeculativePhase::Terminal},
    {SpeculativePhase::Cancelled, SpeculationEvent::EnterTerminal, SpeculativePhase::Terminal},
    {SpeculativePhase::Superseded, SpeculationEvent::EnterTerminal, SpeculativePhase::Terminal},
    {SpeculativePhase::Expired, SpeculationEvent::EnterTerminal, SpeculativePhase::Terminal},
    {SpeculativePhase::Failed, SpeculationEvent::EnterTerminal, SpeculativePhase::Terminal},
};

constexpr int kTransitionCount = static_cast<int>(sizeof(kTransitions) / sizeof(kTransitions[0]));
}  // namespace

const char* to_string(SpeculativePhase phase) noexcept {
    const int i = static_cast<int>(phase);
    if (i >= 0 && i < static_cast<int>(sizeof(kPhaseNames) / sizeof(kPhaseNames[0]))) {
        return kPhaseNames[i];
    }
    return "Unknown";
}

const char* to_string(SpeculationEvent event) noexcept {
    const int i = static_cast<int>(event);
    if (i >= 0 && i < static_cast<int>(sizeof(kEventNames) / sizeof(kEventNames[0]))) {
        return kEventNames[i];
    }
    return "Unknown";
}

bool is_terminal_phase(SpeculativePhase phase) noexcept {
    switch (phase) {
        case SpeculativePhase::Terminal:
        case SpeculativePhase::Committed:
        case SpeculativePhase::Cancelled:
        case SpeculativePhase::Expired:
        case SpeculativePhase::Superseded:
        case SpeculativePhase::Failed:
            return true;
        default:
            return false;
    }
}

std::optional<SpeculativePhase> transition_target(SpeculativePhase from,
                                                  SpeculationEvent event) noexcept {
    for (int i = 0; i < kTransitionCount; ++i) {
        if (kTransitions[i].from == from && kTransitions[i].event == event) {
            return kTransitions[i].to;
        }
    }
    return std::nullopt;
}

bool transition_declared(SpeculativePhase from, SpeculationEvent event) noexcept {
    return transition_target(from, event).has_value();
}

}  // namespace speculation_fabric
