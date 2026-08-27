// Speculation Fabric — error model implementation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "speculation_fabric/core/error.hpp"

namespace speculation_fabric {

namespace {
struct CodeName { ErrorCode code; const char* name; const char* describe; };
// Keep this table exhaustive and sorted by numeric value so a missing entry
// is easy to spot. Never extend an existing numeric value.
constexpr CodeName kCodes[] = {
    {ErrorCode::ok, "ok", "success"},
    {ErrorCode::invalid_argument, "invalid_argument", "invalid argument"},
    {ErrorCode::out_of_range, "out_of_range", "value out of range"},
    {ErrorCode::not_found, "not_found", "entity not found"},
    {ErrorCode::already_exists, "already_exists", "entity already exists"},
    {ErrorCode::invalid_state, "invalid_state", "invalid state"},
    {ErrorCode::unknown, "unknown", "unknown error"},
    {ErrorCode::not_implemented, "not_implemented", "not implemented"},
    {ErrorCode::internal, "internal", "internal error"},
    {ErrorCode::capacity_exceeded, "capacity_exceeded", "capacity exceeded"},
    {ErrorCode::buffer_full, "buffer_full", "buffer full"},
    {ErrorCode::corrupt, "corrupt", "data corrupt"},
    {ErrorCode::malformed, "malformed", "malformed data"},
    {ErrorCode::invalid_proposal, "invalid_proposal", "invalid proposal"},
    {ErrorCode::invalid_candidate, "invalid_candidate", "invalid candidate"},
    {ErrorCode::invalid_branch, "invalid_branch", "invalid branch"},
    {ErrorCode::invalid_depth, "invalid_depth", "invalid speculative depth"},
    {ErrorCode::invalid_token_encoding, "invalid_token_encoding", "invalid token encoding"},
    {ErrorCode::token_count_mismatch, "token_count_mismatch", "token count mismatch"},
    {ErrorCode::acceptance_exceeds_candidate, "acceptance_exceeds_candidate", "accepted count exceeds candidate count"},
    {ErrorCode::invalid_verification_result, "invalid_verification_result", "invalid verification result"},
    {ErrorCode::empty_candidate, "empty_candidate", "empty candidate sequence"},
    {ErrorCode::invalid_authoritative_generation, "invalid_authoritative_generation", "invalid authoritative generation"},
    {ErrorCode::invalid_candidate_generation, "invalid_candidate_generation", "invalid candidate generation"},
    {ErrorCode::candidate_length_mismatch, "candidate_length_mismatch", "candidate length mismatch"},
    {ErrorCode::invalid_request, "invalid_request", "invalid request"},
    {ErrorCode::invalid_identity, "invalid_identity", "invalid identity"},
    {ErrorCode::invalid_dispatch, "invalid_dispatch", "invalid dispatch"},
    {ErrorCode::invalid_attempt, "invalid_attempt", "invalid attempt"},
    {ErrorCode::impossible_candidate_depth, "impossible_candidate_depth", "impossible candidate depth"},
    {ErrorCode::invalid_zero_depth, "invalid_zero_depth", "zero speculative depth is invalid"},
    {ErrorCode::stale_epoch, "stale_epoch", "stale coordinator epoch"},
    {ErrorCode::stale_worker_boot, "stale_worker_boot", "stale worker boot identity"},
    {ErrorCode::stale_attempt, "stale_attempt", "stale attempt"},
    {ErrorCode::stale_proposal_generation, "stale_proposal_generation", "stale proposal generation"},
    {ErrorCode::stale_verification_generation, "stale_verification_generation", "stale verification generation"},
    {ErrorCode::wrong_base_generation, "wrong_base_generation", "wrong authoritative base generation"},
    {ErrorCode::duplicate_proposal, "duplicate_proposal", "duplicate proposal completion"},
    {ErrorCode::duplicate_verification, "duplicate_verification", "duplicate verification completion"},
    {ErrorCode::completion_after_cancellation, "completion_after_cancellation", "completion after cancellation"},
    {ErrorCode::completion_after_terminal, "completion_after_terminal", "completion after terminal state"},
    {ErrorCode::losing_branch_commit, "losing_branch_commit", "commit from losing branch"},
    {ErrorCode::late_proposal, "late_proposal", "late proposal completion"},
    {ErrorCode::late_verification, "late_verification", "late verification completion"},
    {ErrorCode::stale_authority, "stale_authority", "stale authority"},
    {ErrorCode::obsolete_worker, "obsolete_worker", "obsolete worker"},
    {ErrorCode::incompatible_model_pair, "incompatible_model_pair", "incompatible model pair"},
    {ErrorCode::incompatible_tokenizer, "incompatible_tokenizer", "incompatible tokenizer"},
    {ErrorCode::incompatible_vocabulary, "incompatible_vocabulary", "incompatible vocabulary"},
    {ErrorCode::incompatible_adapter, "incompatible_adapter", "incompatible adapter stack"},
    {ErrorCode::incompatible_protocol, "incompatible_protocol", "incompatible candidate protocol"},
    {ErrorCode::incompatible_device, "incompatible_device", "incompatible device"},
    {ErrorCode::incompatible_executor, "incompatible_executor", "incompatible executor"},
    {ErrorCode::incompatible_state_layout, "incompatible_state_layout", "incompatible state layout"},
    {ErrorCode::incompatible_revision, "incompatible_revision", "incompatible model revision"},
    {ErrorCode::incompatible_operator_policy, "incompatible_operator_policy", "incompatible operator policy"},
    {ErrorCode::reservation_underflow, "reservation_underflow", "reservation underflow"},
    {ErrorCode::double_release, "double_release", "double release"},
    {ErrorCode::deadline_exceeded, "deadline_exceeded", "deadline exceeded"},
    {ErrorCode::budget_exceeded, "budget_exceeded", "budget exceeded"},
    {ErrorCode::backpressure, "backpressure", "speculative backpressure"},
    {ErrorCode::request_cancelled, "request_cancelled", "request cancelled"},
    {ErrorCode::expired, "expired", "expired"},
    {ErrorCode::proposer_unavailable, "proposer_unavailable", "proposer unavailable"},
    {ErrorCode::verifier_unavailable, "verifier_unavailable", "verifier unavailable"},
    {ErrorCode::reservation_leak, "reservation_leak", "reservation leak"},
    {ErrorCode::memory_pressure, "memory_pressure", "memory pressure"},
    {ErrorCode::shut_down, "shut_down", "runtime shut down"},
    {ErrorCode::rejected, "rejected", "rejected"},
    {ErrorCode::rolled_back, "rolled_back", "rolled back"},
    {ErrorCode::superseded, "superseded", "superseded"},
    {ErrorCode::failed, "failed", "failed"},
    {ErrorCode::already_terminal, "already_terminal", "already terminal"},
    {ErrorCode::cancelled, "cancelled", "cancelled"},
    {ErrorCode::committed, "committed", "committed"},
    {ErrorCode::fully_accepted, "fully_accepted", "fully accepted"},
    {ErrorCode::partially_accepted, "partially_accepted", "partially accepted"},
    {ErrorCode::retryable_failure, "retryable_failure", "retryable failure"},
    {ErrorCode::non_retryable_failure, "non_retryable_failure", "non-retryable failure"},
    {ErrorCode::proposal_success, "proposal_success", "proposal succeeded"},
    {ErrorCode::proposal_retryable_failure, "proposal_retryable_failure", "proposal retryable failure"},
    {ErrorCode::proposal_non_retryable_failure, "proposal_non_retryable_failure", "proposal non-retryable failure"},
    {ErrorCode::verification_success_full_accept, "verification_success_full_accept", "verification full accept"},
    {ErrorCode::verification_success_partial_accept, "verification_success_partial_accept", "verification partial accept"},
    {ErrorCode::verification_reject_all, "verification_reject_all", "verification rejected all"},
    {ErrorCode::verification_retryable_failure, "verification_retryable_failure", "verification retryable failure"},
    {ErrorCode::verification_non_retryable_failure, "verification_non_retryable_failure", "verification non-retryable failure"},
    {ErrorCode::malformed_frame, "malformed_frame", "malformed frame"},
    {ErrorCode::oversized_frame, "oversized_frame", "oversized frame"},
    {ErrorCode::truncated_frame, "truncated_frame", "truncated frame"},
    {ErrorCode::unknown_protocol_version, "unknown_protocol_version", "unknown protocol version"},
    {ErrorCode::unknown_message_type, "unknown_message_type", "unknown message type"},
    {ErrorCode::malformed_identity, "malformed_identity", "malformed identity"},
    {ErrorCode::invalid_message, "invalid_message", "invalid message"},
    {ErrorCode::zero_length_message, "zero_length_message", "zero-length message"},
    {ErrorCode::invalid_transition, "invalid_transition", "invalid state transition"},
    {ErrorCode::not_ready, "not_ready", "not ready"},
    {ErrorCode::wrong_state, "wrong_state", "wrong state"},
    {ErrorCode::state_machine_cycle_overflow, "state_machine_cycle_overflow", "state machine cycle overflow"},
};
}  // namespace

const char* to_string(ErrorCode code) noexcept {
    for (const auto& e : kCodes) {
        if (e.code == code) return e.name;
    }
    return "unknown";
}

const char* describe(ErrorCode code) noexcept {
    for (const auto& e : kCodes) {
        if (e.code == code) return e.describe;
    }
    return "unknown error";
}

bool is_terminal(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::committed:
        case ErrorCode::fully_accepted:
        case ErrorCode::partially_accepted:
        case ErrorCode::rejected:
        case ErrorCode::rolled_back:
        case ErrorCode::superseded:
        case ErrorCode::failed:
        case ErrorCode::cancelled:
        case ErrorCode::expired:
        case ErrorCode::already_terminal:
            return true;
        default:
            return false;
    }
}

}  // namespace speculation_fabric