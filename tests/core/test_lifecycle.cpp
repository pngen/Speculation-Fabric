// State machine transition tests.
#include "sf_test.hpp"
#include "speculation_fabric/core/lifecycle.hpp"

using namespace speculation_fabric;

static void step(SpeculativeStateMachine& sm, SpeculationEvent ev) {
    auto r = sm.apply(ev);
    SF_CHECK(r.has_value());
}

SF_TEST_FN(lifecycle_names_stable) {
    SF_CHECK(std::string(to_string(SpeculativePhase::Committed)) == "Committed");
    SF_CHECK(std::string(to_string(SpeculationEvent::Commit)) == "Commit");
}

SF_TEST_FN(lifecycle_legal_full_cycle) {
    SpeculativeStateMachine sm;
    step(sm, SpeculationEvent::PlanProposal);
    step(sm, SpeculationEvent::ReserveProposal);
    step(sm, SpeculationEvent::DispatchProposal);
    step(sm, SpeculationEvent::ProposalStarted);
    step(sm, SpeculationEvent::ProposalFinished);
    step(sm, SpeculationEvent::PlanVerification);
    step(sm, SpeculationEvent::ReserveVerification);
    step(sm, SpeculationEvent::DispatchVerification);
    step(sm, SpeculationEvent::VerificationStarted);
    step(sm, SpeculationEvent::VerificationFinished);
    step(sm, SpeculationEvent::AcceptFull);
    step(sm, SpeculationEvent::Commit);
    SF_CHECK(sm.phase() == SpeculativePhase::Committed);
    SF_CHECK(sm.is_terminal());
}

SF_TEST_FN(lifecycle_terminal_absorbing) {
    SpeculativeStateMachine sm;
    step(sm, SpeculationEvent::PlanProposal);
    step(sm, SpeculationEvent::Cancel);
    step(sm, SpeculationEvent::Expire);
    SF_CHECK(sm.phase() == SpeculativePhase::Cancelled);
    SF_CHECK(sm.is_terminal());

    // From Cancelled, a normal plan proposal must be rejected.
    auto rejected = sm.apply(SpeculationEvent::PlanProposal);
    SF_CHECK(rejected.is_error());
    SF_CHECK(rejected.error_code() == ErrorCode::invalid_transition);
}

SF_TEST_FN(lifecycle_partial_accept_rollback) {
    SpeculativeStateMachine sm;
    step(sm, SpeculationEvent::PlanProposal);
    step(sm, SpeculationEvent::ReserveProposal);
    step(sm, SpeculationEvent::DispatchProposal);
    step(sm, SpeculationEvent::ProposalStarted);
    step(sm, SpeculationEvent::ProposalFinished);
    step(sm, SpeculationEvent::PlanVerification);
    step(sm, SpeculationEvent::ReserveVerification);
    step(sm, SpeculationEvent::DispatchVerification);
    step(sm, SpeculationEvent::VerificationStarted);
    step(sm, SpeculationEvent::VerificationFinished);
    step(sm, SpeculationEvent::AcceptPartial);
    SF_CHECK(sm.phase() == SpeculativePhase::PartiallyAccepted);
    step(sm, SpeculationEvent::Rollback);
    SF_CHECK(sm.phase() == SpeculativePhase::RolledBack);
}

SF_TEST_FN(lifecycle_guard_blocks_declared_transition) {
    SpeculativeStateMachine sm;
    auto r = sm.apply(SpeculationEvent::PlanProposal, /*guard=*/false,
                      ErrorCode::stale_authority, "epoch stale");
    SF_CHECK(r.is_error());
    SF_CHECK(r.error_code() == ErrorCode::stale_authority);
    SF_CHECK(sm.phase() == SpeculativePhase::Ready);
}

SF_TEST_FN(lifecycle_no_duplicate_commit) {
    SpeculativeStateMachine sm;
    step(sm, SpeculationEvent::PlanProposal);
    step(sm, SpeculationEvent::ReserveProposal);
    step(sm, SpeculationEvent::DispatchProposal);
    step(sm, SpeculationEvent::ProposalStarted);
    step(sm, SpeculationEvent::ProposalFinished);
    step(sm, SpeculationEvent::PlanVerification);
    step(sm, SpeculationEvent::ReserveVerification);
    step(sm, SpeculationEvent::DispatchVerification);
    step(sm, SpeculationEvent::VerificationStarted);
    step(sm, SpeculationEvent::VerificationFinished);
    step(sm, SpeculationEvent::AcceptFull);
    step(sm, SpeculationEvent::Commit);
    auto second = sm.apply(SpeculationEvent::Commit);
    SF_CHECK(second.is_error());
    SF_CHECK(second.error_code() == ErrorCode::invalid_transition);
}
