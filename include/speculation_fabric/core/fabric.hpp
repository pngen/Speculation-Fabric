// Speculation Fabric — the runtime public API.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// SpeculationFabric governs the lifecycle of speculative inference work
// between authoritative decode state and one or more speculative candidate
// paths. This is the runtime boundary: the authoritative sequence is the
// source of truth, and only valid verification may commit speculative
// progress.
//
// Thread-safety: a single SpeculationFabric instance may be used from many
// threads. The engine never invokes an executor, persistence layer, or
// callback while holding an internal lock, and never recursively acquires a
// lock it already holds. Callers must not invoke methods that re-enter the
// engine while holding a reference obtained from a snapshot.

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "speculation_fabric/core/clock.hpp"
#include "speculation_fabric/core/cpu_proposer.hpp"
#include "speculation_fabric/core/cpu_verifier.hpp"
#include "speculation_fabric/core/executor.hpp"
#include "speculation_fabric/core/lifecycle.hpp"
#include "speculation_fabric/core/policy.hpp"
#include "speculation_fabric/core/request.hpp"
#include "speculation_fabric/core/state.hpp"
#include "speculation_fabric/core/verification.hpp"

namespace speculation_fabric {

// ---------------------------------------------------------------------------
// Observability
// ---------------------------------------------------------------------------
struct Event {
    EventId id{};
    std::uint64_t timestamp_ns{0};
    RequestId request{};
    std::string type;       // machine-readable
    std::string detail;     // human-readable
    std::string json;       // structured payload (best-effort)
};

struct Stats {
    std::uint64_t requests_eligible_for_speculation{0};
    std::uint64_t speculative_cycles_started{0};
    std::uint64_t speculation_bypasses{0};
    std::uint64_t proposals_produced{0};
    std::uint64_t proposals_rejected_stale{0};
    std::uint64_t verifications_run{0};
    std::uint64_t tokens_proposed{0};
    std::uint64_t tokens_accepted{0};
    std::uint64_t tokens_rejected{0};
    std::uint64_t full_accept_count{0};
    std::uint64_t partial_accept_count{0};
    std::uint64_t full_reject_count{0};
    std::uint64_t rollback_count{0};
    std::uint64_t retry_count{0};
    std::uint64_t cancel_count{0};
    std::uint64_t stale_rejections_by_epoch{0};
    std::uint64_t stale_rejections_by_boot{0};
    std::uint64_t stale_rejections_by_attempt{0};
    std::uint64_t stale_rejections_by_generation{0};
    double acceptance_ratio{0.0};             // derived
    std::uint64_t speculative_reservations_current{0};
    std::uint64_t slots_outstanding{0};
};

struct Snapshot {
    std::string taken_at;
    std::uint64_t monotonic_ns{0};
    CoordinatorEpoch epoch{};
    std::size_t active_requests{0};
    std::size_t active_branches{0};
    std::size_t active_reservations{0};
    std::size_t active_proposals{0};
    std::vector<std::string> summaries;       // per-request one-line summaries
};

// The explanatory answer to a "why" question. Presented as text and JSON.
struct Explain {
    std::string question;
    std::string answer_text;
    std::string answer_json;
    std::vector<std::string> reasons;
};

// ---------------------------------------------------------------------------
// Persistence interface
// ---------------------------------------------------------------------------
class Persistence {
public:
    virtual ~Persistence() = default;
    // Serialize a snapshot of authoritative state to durable storage.
    virtual Result<void> save(const std::string& blob) = 0;
    // Load a previously persisted blob (e.g. for recovery).
    virtual Result<std::string> load() = 0;
};

// ---------------------------------------------------------------------------
// Speculation scheduler
// ---------------------------------------------------------------------------
// The scheduler decides whether a sequence may speculate, what depth and how
// many branches to request, and which proposer/verifier to use. Decisions are
// explicit and inspectable; no single opaque score defines them.
class SpeculationScheduler {
public:
    SpeculationScheduler() = default;
    explicit SpeculationScheduler(ProposalPolicy policy) : policy_(policy) {}

    void set_policy(ProposalPolicy p) { policy_ = p; }
    [[nodiscard]] const ProposalPolicy& policy() const noexcept { return policy_; }

    // Plans a proposal for a sequence state. Deterministic for the given input.
    ProposalPlan plan_proposal(const SpeculationRequest& req,
                               AuthGeneration auth_generation,
                               std::uint32_t available_depth_headroom,
                               std::uint32_t available_memory_headroom,
                               double verifier_saturation,
                               ProposalRationale& rationale) const;

    // Decides whether speculation should be attempted for a cycle.
    BackpressureDecision check_backpressure(const SpeculationRequest& req,
                                            std::uint32_t active_branches,
                                            std::uint32_t outstanding_speculative,
                                            std::uint64_t memory_headroom,
                                            double acceptance_ratio) const;

    AdaptiveDepth& adaptive() { return adaptive_; }
    [[nodiscard]] const AdaptiveDepth& adaptive() const { return adaptive_; }

private:
    ProposalPolicy policy_;
    AdaptiveDepth adaptive_;
};

// ---------------------------------------------------------------------------
// SpeculationFabric
// ---------------------------------------------------------------------------
// The main runtime. Construct with executors and a scheduler, submit requests,
// and drive speculative cycles. The authoritative sequence model is internal;
// committed tokens are monotonic and only advance through valid commit.
class SpeculationFabric {
public:
    struct Config {
        std::shared_ptr<ProposalExecutor> proposer;
        std::shared_ptr<VerificationExecutor> verifier;
        std::shared_ptr<Clock> clock{std::make_shared<SteadyClock>()};
        std::shared_ptr<Persistence> persistence;
        ProposalPolicy policy{};
        CoordinatorEpoch epoch{};
    };

    explicit SpeculationFabric(Config cfg);
    ~SpeculationFabric();

    SpeculationFabric(const SpeculationFabric&) = delete;
    SpeculationFabric& operator=(const SpeculationFabric&) = delete;

    // Submits a request; returns its RequestId. Registration is idempotent.
    Result<void> submit(const SpeculationRequest& req);
    // Cancels a request (idempotent); releases reservations and retires branches.
    Result<void> cancel(RequestId id, std::string why);
    // Marks a request terminal (e.g. finished).
    Result<void> complete(RequestId id);

    // Executes ONE speculative cycle for a request synchronously using the
    // registered executors. Real proposal + real verification + commit/rollback.
    Result<CycleOutcome> run_cycle(RequestId id);

    // The authoritative length of a sequence (monotonic).
    Result<std::uint64_t> authoritative_length(RequestId id) const;
    // Current proposal / branches for a request.
    Result<std::vector<Proposal>> proposals(RequestId id) const;
    Result<std::vector<Branch>> branches(RequestId id) const;

    CoordinatorEpoch epoch() const noexcept { return cfg_.epoch; }
    void roll_epoch() noexcept { cfg_.epoch = CoordinatorEpoch{cfg_.epoch.get() + 1}; }

    // Observability.
    Stats stats() const;
    Snapshot snapshot() const;
    Explain explain(RequestId id, const std::string& question) const;
    std::vector<Event> events(RequestId id) const;

    // Rolls back a request's speculative state to its authoritative point.
    Result<RollbackResult> rollback(RequestId id, std::string why);
    // Attempts a retry with a NEW attempt identity.
    Result<AttemptId> retry(RequestId id);

    // Emits an advisory event (does not mutate authoritative state).
    void emit_event(RequestId id, const std::string& type, const std::string& detail,
                    const std::string& json);

private:
    Config cfg_;
    std::shared_ptr<SpeculationScheduler> scheduler_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace speculation_fabric