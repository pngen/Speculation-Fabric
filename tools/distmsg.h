// Shared field-layout constants for the distributed control plane.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
#pragma once
#include <cstdint>
#include <vector>
#include "speculation_fabric/core/wire.hpp"

namespace spec_fabric {
namespace dist {

using speculation_fabric::wire::Msg;
using speculation_fabric::wire::Message;
using speculation_fabric::wire::encode;
using speculation_fabric::wire::decode;

// Worker roles.
constexpr std::uint32_t kRoleProposer = 1;
constexpr std::uint32_t kRoleVerifier = 2;
constexpr std::uint32_t kRoleDriver = 3;

// Authorization sub-codes for the Reject message body.
constexpr const char* kReasonStaleEpoch = "stale_epoch";
constexpr const char* kReasonStaleBoot = "stale_worker_boot";
constexpr const char* kReasonStaleAttempt = "stale_attempt";
constexpr const char* kReasonStaleGeneration = "stale_generation";
constexpr const char* kReasonWrongBase = "wrong_base_generation";

// Outcome codes.
constexpr std::uint32_t kOutcomeSuccess = 0;
constexpr std::uint32_t kOutcomeRetryable = 1;
constexpr std::uint32_t kOutcomeNonRetryable = 2;
// AcceptanceOutcome codes.
constexpr std::uint32_t kAccFull = 0;
constexpr std::uint32_t kAccPartial = 1;
constexpr std::uint32_t kAccReject = 2;
constexpr std::uint32_t kAccFailure = 3;

// ProposalDispatch u64 indices.
enum : std::size_t {
    PD_REQUEST = 0, PD_SEQUENCE = 1, PD_ATTEMPT = 2, PD_EPOCH = 3,
    PD_BASE_STATE_ID = 4, PD_BASE_STATE_GEN = 5, PD_BASE_GEN = 6,
    PD_PROPOSER_REV = 7, PD_DISPATCH = 8, PD_BRANCH = 9, PD_PROPOSER_BOOT = 10,
    PD_PROPOGEN = 11,
};
// ProposalDispatch u32 indices.
enum : std::size_t { PD_DEPTH = 0, PD_ALIGNED = 1, PD_BRANCHCOUNT = 2 };
// ProposalResult u64 indices.
enum : std::size_t {
    PR_REQUEST = 0, PR_SEQUENCE = 1, PR_ATTEMPT = 2, PR_EPOCH = 3,
    PR_DISPATCH = 4, PR_BRANCH = 5, PR_BASE_GEN = 6, PR_PROPOSER_BOOT = 7,
    PR_PROPOSER_REV = 8, PR_PROPOGEN = 9, PR_EXEC_NS = 10,
};
// ProposalResult u32 indices.
enum : std::size_t { PR_OUTCOME = 0, PR_DEPTH = 1, PR_TOKENS = 2 };

// VerificationDispatch u64 indices.
enum : std::size_t {
    VD_REQUEST = 0, VD_SEQUENCE = 1, VD_ATTEMPT = 2, VD_EPOCH = 3,
    VD_STATE_ID = 4, VD_STATE_GEN = 5, VD_BASE_GEN = 6, VD_PROPOSER_REV = 7,
    VD_DISPATCH = 8, VD_PROPOSER_BOOT = 9,
};
// VerificationDispatch u32 indices.
enum : std::size_t { VD_TOKENIZER = 0, VD_VOCAB = 1, VD_LEN = 2, VD_TOKENS = 3 };
// VerificationResult u64 indices.
enum : std::size_t {
    VR_REQUEST = 0, VR_SEQUENCE = 1, VR_ATTEMPT = 2, VR_EPOCH = 3,
    VR_BASE_GEN = 4, VR_DISPATCH = 5, VR_STATE_ID = 6, VR_STATE_GEN = 7,
};
// VerificationResult u32 indices.
enum : std::size_t { VR_OUTCOME = 0, VR_ACCEPTED = 1, VR_LEN = 2, VR_FIRST_REJ = 3 };

}  // namespace dist
}  // namespace spec_fabric