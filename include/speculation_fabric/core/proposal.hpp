// Speculation Fabric — proposal and branch model.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "speculation_fabric/core/candidate.hpp"
#include "speculation_fabric/core/id.hpp"
#include "speculation_fabric/core/model.hpp"
#include "speculation_fabric/core/state.hpp"

namespace speculation_fabric {

// Every distributed proposal/verification message carries enough authority
// to reject stale work. This is the authority envelope for a proposal.
struct ProposalProvenance {
    CoordinatorEpoch epoch{};             // coordinator epoch at dispatch
    RequestId request{};                  // the originating request
    SequenceId sequence{};                // the authoritative sequence
    AttemptId attempt{};                  // current request attempt
    BranchId branch{};                    // the branch this proposal belongs to
    CycleId cycle{};                      // the speculative cycle
    WorkerId proposer_worker{};           // which proposer produced it
    WorkerBootId proposer_boot{};         // boot identity of that proposer
    ModelIdentity proposer_model{};       // the proposer model
    AuthGeneration base_generation{};     // authoritative gen it was based on
    DispatchId dispatch{};                // dispatch authority token

    bool operator==(const ProposalProvenance&) const = default;
};

// A finalized, immutable proposal. Once a proposal becomes eligible for
// verification it is never mutated. All provenance is present so stale,
// duplicate, mismatched, or unauthorized work can be rejected deterministically.
struct Proposal {
    ProposalId id{};
    ProposalGeneration generation{};
    ProposalProvenance provenance{};
    CandidateSequence candidate{};        // the proposed token sequence (immutable)
    CandidateTokenIdentity token_identity{};
    StateRef base_state{};                 // authoritative state it consumed
    StateGeneration base_state_generation{};
    StateRef speculative_state{};          // speculative state it produced
    StateDescriptor speculative_state_desc{};
    std::uint64_t compute_spent{0};
    std::uint64_t memory_held{0};
    bool finalized{false};                 // immutable after finalize
    bool eligible_for_verification{false};
    bool committed{false};

    [[nodiscard]] CandidateDepth depth() const noexcept { return candidate.depth(); }

    bool operator==(const Proposal&) const = default;
};

// A branch is an independent speculative path from one authoritative point.
// Branches never collapse into a shared mutable candidate object; each branch
// has independent identity, lineage, and reservation so that tokens and state
// cannot cross-contaminate.
struct Branch {
    BranchId id{};
    RequestId request{};
    SequenceId sequence{};
    AttemptId attempt{};
    CycleId cycle{};
    AuthGeneration base_generation{};
    StateRef base_state{};
    ModelIdentity proposer{};              // intended proposer for this branch
    StateDescriptor speculative_state{};   // the branch's speculative state region
    std::optional<ProposalId> latest_proposal{};
    std::size_t proposal_count{0};
    bool retired{false};
    bool selected{false};                  // chosen as the authoritative branch

    [[nodiscard]] bool is_active() const noexcept { return !retired; }
    bool operator==(const Branch&) const = default;
};

}  // namespace speculation_fabric
