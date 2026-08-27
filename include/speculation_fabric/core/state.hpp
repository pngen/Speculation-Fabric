// Speculation Fabric — state, sequence, and memory governance descriptors.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "speculation_fabric/core/id.hpp"
#include "speculation_fabric/core/model.hpp"

namespace speculation_fabric {

// Whether a sequence is the authoritative source of truth or a speculative
// candidate path. Speculative sequences may explore ahead but never become
// authoritative without valid verification.
enum class SequenceRole : std::uint8_t {
    Authoritative = 0,
    Speculative = 1,
};

// An opaque reference to an externally managed state region (e.g. KV cache).
// The runtime models the reference and its generation but never owns the
// actual contents; state is passed through typed interfaces.
struct StateRef {
    StateId id{};
    StateGeneration generation{};

    [[nodiscard]] bool is_valid() const noexcept { return !id.is_null(); }
    bool operator==(const StateRef&) const = default;
};

// Domain the state lives in. A DNS-like string such as "cpu", "cuda:0".
// The runtime only compares identity.
struct StateDomain {
    std::string name{"cpu"};
    DeviceDescriptor device{};

    bool operator==(const StateDomain&) const = default;
};

// Lifecycle of a state / memory reservation.
enum class ReservationState : std::uint8_t {
    None = 0,
    Reserved = 1,
    Pending = 2,
    Used = 3,
    Released = 4,
};

// Ownership of a speculative state region.
enum class StateOwner : std::uint8_t {
    None = 0,
    Proposal = 1,
    Branch = 2,
    Verification = 3,
    AuthoritativeCommit = 4,
};

// Full descriptor for one state region or reservation. The runtime uses this
// to prove that speculative state is reserved, owned, committed, or released
// and never leaked or double-released.
struct StateDescriptor {
    StateRef ref{};                                // identity + generation
    SequenceRole role{SequenceRole::Speculative};  // authoritative vs speculative
    StateDomain domain{};                          // where the state lives
    std::optional<StateRef> parent{};              // lineage
    std::optional<StateGeneration> parent_generation{};
    std::uint64_t bytes_held{0};                   // configured/estimated bytes
    ReservationState reservation{ReservationState::None};
    StateOwner owner{StateOwner::None};
    std::optional<ProposalId> proposal_owner{};    // who requested it
    std::optional<BranchId> branch_owner{};        // which branch holds it
    bool committed{false};                          // rolled into authoritative

    [[nodiscard]] bool is_held() const noexcept {
        return reservation == ReservationState::Reserved ||
               reservation == ReservationState::Pending ||
               reservation == ReservationState::Used;
    }

    bool operator==(const StateDescriptor&) const = default;
};

// A memory reservation. Reservations are created before speculative work is
// admitted and must be released exactly once on every path.
struct Reservation {
    ReservationId id{};
    StateDomain domain{};
    std::uint64_t bytes{0};
    ReservationState state{ReservationState::None};
    StateOwner purpose{StateOwner::None};
    std::optional<ProposalId> proposal{};
    std::optional<BranchId> branch{};
    std::optional<RequestId> request{};
    std::string purpose_label;

    [[nodiscard]] bool is_active() const noexcept {
        return state == ReservationState::Reserved ||
               state == ReservationState::Pending ||
               state == ReservationState::Used;
    }

    bool operator==(const Reservation&) const = default;
};

// A single snapshot of authoritative sequence progress. The committed token
// prefix is monotonic within a sequence.
struct AuthoritativeState {
    SequenceId sequence{};
    AuthGeneration generation{};
    StateRef state{};
    std::vector<std::uint32_t> committed_tokens{};  // authoritative token ids
    ModelIdentity model{};
    std::uint64_t committed_length{0};

    [[nodiscard]] std::uint64_t length() const noexcept { return committed_length; }
    bool operator==(const AuthoritativeState&) const = default;
};

}  // namespace speculation_fabric
