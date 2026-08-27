// Speculation Fabric — strong identifiers.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Every entity in the speculation lifecycle carries a distinct, strongly
// typed identifier. Identifiers are 64-bit lossless integers with a tag
// type so that a ProposalId can never be silently mixed with a RequestId
// at the type level. Identifiers are serialized as binary 64-bit values in
// the distributed protocol, never through floating-point JSON numbers.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>

namespace speculation_fabric {

// StrongId<Tag, T>: a typed integer identifier.
template<class Tag, class T = std::uint64_t>
class StrongId {
public:
    using tag_type = Tag;
    using value_type = T;

    constexpr StrongId() noexcept = default;
    constexpr explicit StrongId(T value) noexcept : value_(value) {}
    constexpr StrongId(const StrongId&) noexcept = default;
    constexpr StrongId& operator=(const StrongId&) noexcept = default;

    // The underlying 64-bit value in lossless integer form.
    [[nodiscard]] constexpr T get() const noexcept { return value_; }
    [[nodiscard]] constexpr T value() const noexcept { return value_; }

    [[nodiscard]] constexpr bool is_null() const noexcept { return value_ == T{}; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != T{}; }

    [[nodiscard]] constexpr bool operator==(const StrongId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const StrongId& other) const noexcept { return value_ <=> other.value_; }

    [[nodiscard]] std::string str() const { return std::to_string(value_); }

private:
    T value_{};
};

// Monotonic factory that generates unique, non-zero identifiers of one tag.
// thread-safe; deterministic only in the sense that no identifier is reused.
template<class Tag, class T = std::uint64_t>
class IdFactory {
public:
    IdFactory() noexcept {
        // Start at 1 so that the null identifier (0) is always invalid.
        next_.store(1, std::memory_order_relaxed);
    }
    explicit IdFactory(T start) noexcept {
        next_.store(start == T{} ? T{1} : start, std::memory_order_relaxed);
    }

    // Returns the next identifier without advancing the sequence.
    [[nodiscard]] StrongId<Tag, T> peek() const noexcept {
        return StrongId<Tag, T>{next_.load(std::memory_order_relaxed)};
    }

    // Returns the next unique identifier and advances the sequence.
    StrongId<Tag, T> next() noexcept {
        const T v = next_.fetch_add(1, std::memory_order_relaxed);
        // Hedge against wraparound to zero; production loads never wrap.
        return StrongId<Tag, T>{v == T{} ? next_.fetch_add(1, std::memory_order_relaxed) : v};
    }

private:
    std::atomic<T> next_;
};

// Tag types for the core identifier kinds.
struct RequestIdTag {};
struct SequenceIdTag {};
struct AttemptIdTag {};
struct ProposalIdTag {};
struct BranchIdTag {};
struct VerifierIdTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct DispatchIdTag {};
struct TenantIdTag {};
struct AdapterIdTag {};
struct ModelIdTag {};
struct RevisionTag {};
struct TokenizerIdTag {};
struct ExecutorIdTag {};
struct DeviceIdTag {};
struct ReservationIdTag {};
struct StateIdTag {};
struct EventIdTag {};
struct ProposalGenTag {};    // proposal generation
struct VerifyGenTag {};      // verification generation
struct AuthGenTag {};        // authoritative generation
struct StateGenTag {};       // state generation
struct EpochTag {};          // coordinator epoch
struct CycleIdTag {};        // speculative cycle
struct ClientIdTag {};

// Core identifier aliases (64-bit values, lossless).
using RequestId = StrongId<RequestIdTag, std::uint64_t>;
using SequenceId = StrongId<SequenceIdTag, std::uint64_t>;
using AttemptId = StrongId<AttemptIdTag, std::uint64_t>;
using ProposalId = StrongId<ProposalIdTag, std::uint64_t>;
using BranchId = StrongId<BranchIdTag, std::uint64_t>;
using VerifierId = StrongId<VerifierIdTag, std::uint64_t>;
using WorkerId = StrongId<WorkerIdTag, std::uint64_t>;
using WorkerBootId = StrongId<WorkerBootIdTag, std::uint64_t>;
using DispatchId = StrongId<DispatchIdTag, std::uint64_t>;
using TenantId = StrongId<TenantIdTag, std::uint64_t>;
using AdapterId = StrongId<AdapterIdTag, std::uint64_t>;
using ModelId = StrongId<ModelIdTag, std::uint64_t>;
using Revision = StrongId<RevisionTag, std::uint64_t>;
using TokenizerId = StrongId<TokenizerIdTag, std::uint64_t>;
using ExecutorId = StrongId<ExecutorIdTag, std::uint64_t>;
using DeviceId = StrongId<DeviceIdTag, std::uint64_t>;
using ReservationId = StrongId<ReservationIdTag, std::uint64_t>;
using StateId = StrongId<StateIdTag, std::uint64_t>;
using EventId = StrongId<EventIdTag, std::uint64_t>;
using ProposalGeneration = StrongId<ProposalGenTag, std::uint64_t>;
using VerificationGeneration = StrongId<VerifyGenTag, std::uint64_t>;
using AuthGeneration = StrongId<AuthGenTag, std::uint64_t>;
using StateGeneration = StrongId<StateGenTag, std::uint64_t>;
using CoordinatorEpoch = StrongId<EpochTag, std::uint64_t>;
using CycleId = StrongId<CycleIdTag, std::uint64_t>;
using ClientId = StrongId<ClientIdTag, std::uint64_t>;

// Token identity for a single candidate position. A token is a 32-bit id
// from the vocabulary plus an optional byte representation. The integer id
// is the lossless canonical form; token ids are compared by value.
using TokenId = std::uint32_t;

}  // namespace speculation_fabric

// std::hash support for strong ids.
namespace std {
template<class Tag, class T>
struct hash<speculation_fabric::StrongId<Tag, T>> {
    [[nodiscard]] size_t operator()(const speculation_fabric::StrongId<Tag, T>& id) const noexcept {
        std::hash<T> h;
        return h(id.get());
    }
};
}  // namespace std
