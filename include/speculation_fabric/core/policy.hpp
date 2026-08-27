// Speculation Fabric — policy, adaptive depth, backpressure, fairness.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "speculation_fabric/core/error.hpp"
#include "speculation_fabric/core/id.hpp"

namespace speculation_fabric {

// Latency classes for requests; trade speculative depth against added latency.
enum class LatencyClass : std::uint8_t {
    Interactive = 0,
    Balanced = 1,
    Throughput = 2,
};

// A configurable proposal policy. The planner is deterministic for identical
// input state and policy: the same (sequence state, policy, history) always
// yields the same proposal plan.
struct ProposalPolicy {
    std::uint32_t max_depth{4};
    std::uint32_t max_branches{1};
    std::uint32_t max_candidate_tokens_per_proposal{64};
    std::uint32_t max_concurrent_proposals_per_sequence{2};
    std::uint64_t max_proposer_compute_budget{0};   // 0 => unlimited
    std::uint64_t max_proposer_memory_budget{0};    // 0 => unlimited
    std::uint32_t max_outstanding_speculative_work{8};
    std::uint64_t max_speculative_delay_ns{1000000000ULL};  // 1s default
    LatencyClass latency_class{LatencyClass::Balanced};
    bool speculating_enabled{true};
    std::uint64_t tenant_max_speculative_budget_ns{0};      // 0 => unlimited
    std::uint32_t tenant_max_concurrent_branches{0};        // 0 => unlimited
    double min_economic_acceptance_ratio{0.0};              // 0 => never gate on economics
    std::uint32_t adaptive_window{32};                      // history window size
    bool adaptive_depth_enabled{true};
};

// A deterministic adaptive-depth policy. It varies proposal depth based on a
// bounded recent acceptance history and available headroom. For a fixed
// history it is perfectly reproducible.
class AdaptiveDepth {
public:
    // Records an acceptance ratio for one completed cycle.
    void record(double ratio) {
        history_.push_back(ratio);
        if (history_.size() > window_) history_.pop_front();
    }

    void set_window(std::uint32_t w) { window_ = w; }

    // Chooses a depth for the next proposal given the policy and available
    // headroom. Deterministic for a given history and policy.
    std::uint32_t choose_depth(const ProposalPolicy& policy,
                               std::uint32_t available_memory_headroom,
                               double verifier_saturation) const;

    [[nodiscard]] double average_ratio() const;
    void clear() { history_.clear(); }

private:
    std::deque<double> history_;
    std::uint32_t window_{32};
};

// Backpressure is an explicit decision, never a silent drop.
enum class BackpressureReason : std::uint8_t {
    None = 0,
    ProposerCapacity = 1,
    VerifierCapacity = 2,
    BranchLimit = 3,
    MemoryHeadroom = 4,
    OutstandingLimit = 5,
    TenantBudget = 6,
    DeadlineTooTight = 7,
    EconomicsBelowThreshold = 8,
};

struct BackpressureDecision {
    bool blocked{false};
    BackpressureReason reason{BackpressureReason::None};
    std::string detail{};

    static BackpressureDecision allow() { return {}; }
    static BackpressureDecision deny(BackpressureReason r, std::string d) {
        BackpressureDecision b;
        b.blocked = true;
        b.reason = r;
        b.detail = std::move(d);
        return b;
    }
};

// Why a proposal chose a particular depth / branch count (inspectable).
struct ProposalRationale {
    std::uint32_t depth{0};
    std::uint32_t branch_count{1};
    std::string depth_reason;
    std::string branch_reason;
    std::string proposer_reason;
    std::string verifier_reason;
};

// Per-tenant speculative accounting for fairness. Weighted service accounting
// prevents one tenant from monopolizing speculative capacity.
struct TenantSpecBudget {
    TenantId tenant{};
    std::uint64_t budget_units{0};
    std::uint64_t used_units{0};
    std::uint32_t current_branches{0};
    double weight{1.0};                 // weighted service share
    std::uint64_t speculative_work_ns{0};

    [[nodiscard]] std::uint64_t remaining_units() const noexcept {
        return budget_units > used_units ? (budget_units - used_units) : 0;
    }
};

// Clear provenance labels for measured / derived / configured / estimated.
enum class ValueOrigin : std::uint8_t {
    Measured = 0,
    Derived = 1,
    Configured = 2,
    Estimated = 3,
};

}  // namespace speculation_fabric
