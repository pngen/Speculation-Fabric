// Speculation Fabric — runtime implementation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "speculation_fabric/core/fabric.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>

namespace speculation_fabric {

namespace {
std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}
}  // namespace

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------
ProposalPlan SpeculationScheduler::plan_proposal(const SpeculationRequest& req,
                                                 AuthGeneration auth_generation,
                                                 std::uint32_t available_depth_headroom,
                                                 std::uint32_t available_memory_headroom,
                                                 double verifier_saturation,
                                                 ProposalRationale& rationale) const {
    ProposalPlan plan;
    plan.request = req.id;
    plan.sequence = req.sequence;
    plan.attempt = AttemptId{};
    plan.depth = policy_.max_depth;
    if (policy_.latency_class == LatencyClass::Interactive) {
        plan.depth = std::min(2u, policy_.max_depth);
    }
    if (available_depth_headroom != 0) {
        plan.depth = std::min(plan.depth, available_depth_headroom);
    }
    if (policy_.adaptive_depth_enabled) {
        plan.depth = adaptive_.choose_depth(policy_, available_memory_headroom,
                                            verifier_saturation);
    }
    plan.depth = std::max(1u, plan.depth);
    plan.branch_count = std::max(1u, policy_.max_branches);
    plan.proposer = req.draft_model;
    plan.dispatch = DispatchId{req.id.get() * 0x100000001B3ULL ^
                               auth_generation.get() * 0x9E3779B97F4A7C15ULL};
    rationale.depth = plan.depth;
    rationale.branch_count = plan.branch_count;
    rationale.depth_reason =
        policy_.adaptive_depth_enabled
            ? std::string("adaptive depth ") + std::to_string(plan.depth) +
                  " from acceptance history"
            : std::string("fixed policy depth ") + std::to_string(plan.depth);
    rationale.branch_reason =
        "branch count " + std::to_string(plan.branch_count) + " from policy";
    rationale.proposer_reason = "draft model " + req.draft_model.name;
    rationale.verifier_reason = "target model " + req.target_model.name;
    plan.rationale = rationale.depth_reason + "; " + rationale.proposer_reason;
    return plan;
}

BackpressureDecision SpeculationScheduler::check_backpressure(
    const SpeculationRequest& req, std::uint32_t active_branches,
    std::uint32_t outstanding_speculative, std::uint64_t memory_headroom,
    double acceptance_ratio) const {
    if (!policy_.speculating_enabled) {
        return BackpressureDecision::deny(BackpressureReason::None,
                                          "speculation disabled by policy");
    }
    if (outstanding_speculative >= policy_.max_outstanding_speculative_work &&
        policy_.max_outstanding_speculative_work != 0) {
        return BackpressureDecision::deny(BackpressureReason::OutstandingLimit,
                                          "outstanding speculative work limit");
    }
    if (memory_headroom == 0) {
        return BackpressureDecision::deny(BackpressureReason::MemoryHeadroom,
                                          "no memory headroom");
    }
    if (policy_.min_economic_acceptance_ratio > 0.0 &&
        acceptance_ratio < policy_.min_economic_acceptance_ratio) {
        return BackpressureDecision::deny(BackpressureReason::EconomicsBelowThreshold,
                                          "acceptance economics below threshold");
    }
    (void)req;
    (void)active_branches;
    return BackpressureDecision::allow();
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct SpeculationFabric::Impl {
    explicit Impl(SpeculationFabric::Config cfg)
        : cfg(std::move(cfg)), scheduler(std::make_shared<SpeculationScheduler>(cfg.policy)) {}

    SpeculationFabric::Config cfg;
    std::shared_ptr<SpeculationScheduler> scheduler;

    mutable std::mutex mtx;

    struct RequestState {
        SpeculationRequest req;
        AttemptId attempt;
        CoordinatorEpoch epoch;
        AuthGeneration auth_gen;
        StateRef auth_state;
        StateGeneration auth_state_gen;
        std::vector<std::uint32_t> committed;
        std::vector<Branch> branches;
        std::vector<Proposal> proposals;
        std::vector<Reservation> reservations;
        SpeculativeStateMachine cycle;
        bool cancelled{false};
        bool terminal{false};
        std::uint32_t completed_cycles{0};
        std::uint32_t retry_count{0};
        std::deque<Event> events;
    };

    std::map<RequestId, RequestState> requests;
    Stats stats;
    std::deque<Event> global_events;

    void push_event(RequestId id, const std::string& type, const std::string& detail,
                    const std::string& json) {
        const std::uint64_t now = cfg.clock->monotonic_ns();
        Event e;
        e.id = EventId{static_cast<std::uint64_t>(global_events.size() + 1)};
        e.timestamp_ns = now;
        e.request = id;
        e.type = type;
        e.detail = detail;
        e.json = json;
        global_events.push_back(e);
        if (global_events.size() > 2000) global_events.pop_front();
        auto it = requests.find(id);
        if (it != requests.end()) {
            it->second.events.push_back(e);
            if (it->second.events.size() > 512) it->second.events.pop_front();
        }
    }

    Result<void> check_authority(const RequestState& rs, CoordinatorEpoch epoch,
                                 AttemptId attempt, AuthGeneration base_gen,
                                 std::uint64_t prop_gen) {
        if (rs.terminal) {
            return Result<void>::err(ErrorCode::completion_after_terminal,
                                     "request is terminal");
        }
        if (rs.cancelled) {
            return Result<void>::err(ErrorCode::completion_after_cancellation,
                                     "request was cancelled");
        }
        if (!epoch.is_null() && epoch.get() < rs.epoch.get()) {
            ++stats.stale_rejections_by_epoch;
            return Result<void>::err(ErrorCode::stale_epoch, "message epoch is stale");
        }
        if (attempt != rs.attempt) {
            ++stats.stale_rejections_by_attempt;
            return Result<void>::err(ErrorCode::stale_attempt,
                                     "message belongs to an obsolete attempt");
        }
        if (base_gen != rs.auth_gen) {
            ++stats.stale_rejections_by_generation;
            return Result<void>::err(ErrorCode::wrong_base_generation,
                                     "message targets a different authoritative base");
        }
        if (prop_gen != 0) {
            for (const auto& p : rs.proposals) {
                if (p.generation.get() == prop_gen) {
                    return Result<void>::ok();
                }
            }
            ++stats.stale_rejections_by_generation;
            return Result<void>::err(ErrorCode::stale_proposal_generation,
                                     "proposal generation is stale");
        }
        return Result<void>::ok();
    }

    std::uint32_t active_reservation_bytes(const RequestState& rs) const {
        std::uint32_t total = 0;
        for (const auto& r : rs.reservations) {
            if (r.is_active()) total += static_cast<std::uint32_t>(r.bytes);
        }
        return total;
    }
};

SpeculationFabric::SpeculationFabric(Config cfg)
    : cfg_(std::move(cfg)),
      impl_(std::make_unique<Impl>(cfg_)),
      scheduler_(std::make_shared<SpeculationScheduler>(cfg_.policy)) {}

SpeculationFabric::~SpeculationFabric() = default;

Result<void> SpeculationFabric::submit(const SpeculationRequest& req) {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto& rs = impl_->requests[req.id];
    if (!rs.req.id.is_null()) {
        // A duplicate RequestId is a duplicate submission and is rejected;
        // it is never silently treated as an overwrite.
        return Result<void>::err(ErrorCode::already_exists,
                                 "request id already registered");
    }
    rs.req = req;
    rs.attempt = AttemptId{1};
    rs.epoch = cfg_.epoch;
    rs.auth_gen = AuthGeneration{0};
    rs.auth_state = StateRef{StateId{req.sequence.get() * 0x9E3779B97F4A7C15ULL},
                             StateGeneration{0}};
    rs.auth_state_gen = StateGeneration{0};
    rs.committed = req.prefix;
    rs.terminal = false;
    rs.cancelled = false;
    rs.completed_cycles = 0;
    rs.cycle = SpeculativeStateMachine(SpeculativePhase::Ready);
    ++impl_->stats.requests_eligible_for_speculation;
    impl_->push_event(req.id, "request.submit", "request admitted", "{}");
    return Result<void>::ok();
}

Result<void> SpeculationFabric::cancel(RequestId id, std::string why) {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        return Result<void>::err(ErrorCode::not_found, "unknown request");
    }
    auto& rs = it->second;
    if (rs.cancelled) {
        return Result<void>::ok();  // idempotent
    }
    if (rs.terminal) {
        return Result<void>::err(ErrorCode::already_terminal, "request already terminal");
    }
    const auto r = rs.cycle.apply(SpeculationEvent::Cancel);
    (void)r;
    // Release all active reservations and retire all active branches.
    for (auto& res : rs.reservations) res.state = ReservationState::Released;
    for (auto& b : rs.branches) b.retired = true;
    rs.cancelled = true;
    ++impl_->stats.cancel_count;
    impl_->push_event(id, "request.cancel", std::move(why), "{}");
    return Result<void>::ok();
}

Result<void> SpeculationFabric::complete(RequestId id) {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        return Result<void>::err(ErrorCode::not_found, "unknown request");
    }
    it->second.terminal = true;
    const auto r = it->second.cycle.apply(SpeculationEvent::EnterTerminal);
    (void)r;
    impl_->push_event(id, "request.complete", "request terminal", "{}");
    return Result<void>::ok();
}

Result<std::uint64_t> SpeculationFabric::authoritative_length(RequestId id) const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        return Result<std::uint64_t>::err(ErrorCode::not_found, "unknown request");
    }
    return Result<std::uint64_t>::ok(it->second.committed.size());
}

Result<std::vector<Proposal>> SpeculationFabric::proposals(RequestId id) const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        return Result<std::vector<Proposal>>::err(ErrorCode::not_found, "unknown request");
    }
    return Result<std::vector<Proposal>>::ok(it->second.proposals);
}

Result<std::vector<Branch>> SpeculationFabric::branches(RequestId id) const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        return Result<std::vector<Branch>>::err(ErrorCode::not_found, "unknown request");
    }
    return Result<std::vector<Branch>>::ok(it->second.branches);
}

Stats SpeculationFabric::stats() const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto s = impl_->stats;
    s.acceptance_ratio = (s.tokens_proposed == 0)
                             ? 0.0
                             : static_cast<double>(s.tokens_accepted) /
                                   static_cast<double>(s.tokens_proposed);
    s.speculative_reservations_current = 0;
    s.slots_outstanding = 0;
    for (const auto& kv : impl_->requests) {
        for (const auto& r : kv.second.reservations) {
            if (r.is_active()) ++s.speculative_reservations_current;
        }
    }
    return s;
}

Snapshot SpeculationFabric::snapshot() const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    Snapshot s;
    s.monotonic_ns = cfg_.clock->monotonic_ns();
    s.epoch = cfg_.epoch;
    s.active_requests = impl_->requests.size();
    for (const auto& kv : impl_->requests) {
        s.active_branches += kv.second.branches.size();
        s.active_proposals += kv.second.proposals.size();
        for (const auto& r : kv.second.reservations) {
            if (r.is_active()) ++s.active_reservations;
        }
        s.summaries.push_back("seq=" + kv.second.req.sequence.str() +
                              " len=" + std::to_string(kv.second.committed.size()));
    }
    return s;
}

std::vector<Event> SpeculationFabric::events(RequestId id) const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        return {};
    }
    return std::vector<Event>(it->second.events.begin(), it->second.events.end());
}

void SpeculationFabric::emit_event(RequestId id, const std::string& type,
                                   const std::string& detail, const std::string& json) {
    std::lock_guard<std::mutex> g(impl_->mtx);
    impl_->push_event(id, type, detail, json);
}

Explain SpeculationFabric::explain(RequestId id, const std::string& question) const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    Explain ex;
    ex.question = question;
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        ex.answer_text = "unknown request " + id.str();
        ex.answer_json = "{\"request\":\"unknown\"}";
        return ex;
    }
    const auto& rs = it->second;
    std::ostringstream ss;
    ss << "sequence " << rs.req.sequence.str() << " (request " << id.str()
       << "): authoritative generation " << rs.auth_gen.get()
       << ", committed " << rs.committed.size() << " tokens, attempt "
       << rs.attempt.get() << ", epoch " << rs.epoch.get()
       << ", cycle phase " << to_string(rs.cycle.phase())
       << ", branches " << rs.branches.size() << ", proposals "
       << rs.proposals.size() << ", active reservations "
       << impl_->active_reservation_bytes(rs);
    ex.answer_text = ss.str();
    ex.answer_json =
        "{\"request\":\"" + id.str() + "\",\"gen\":" +
        std::to_string(rs.auth_gen.get()) +
        ",\"len\":" + std::to_string(rs.committed.size()) +
        ",\"phase\":\"" + to_string(rs.cycle.phase()) + "\"}";
    ex.reasons.push_back(ss.str());
    return ex;
}

Result<RollbackResult> SpeculationFabric::rollback(RequestId id, std::string why) {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        return Result<RollbackResult>::err(ErrorCode::not_found, "unknown request");
    }
    auto& rs = it->second;
    RollbackResult rb;
    rb.rejected_tokens = 0;
    for (const auto& p : rs.proposals) {
        if (p.finalized && !p.committed) {
            rb.rejected_tokens += static_cast<std::uint32_t>(p.candidate.size());
        }
    }
    rb.accepted_prefix = static_cast<std::uint32_t>(rs.committed.size());
    rb.restored_generation = rs.auth_gen;
    rb.restored_state = rs.auth_state;
    rb.restored_state_generation = rs.auth_state_gen;
    for (auto& b : rs.branches) {
        if (!b.retired) { b.retired = true; ++rb.retired_branches; }
    }
    for (auto& r : rs.reservations) {
        if (r.is_active()) { r.state = ReservationState::Released; ++rb.released_reservations; }
    }
    rb.success = true;
    rb.detail = std::move(why);
    ++impl_->stats.rollback_count;
    impl_->push_event(id, "request.rollback", rb.detail, "{}");
    return Result<RollbackResult>::ok(rb);
}

Result<AttemptId> SpeculationFabric::retry(RequestId id) {
    std::lock_guard<std::mutex> g(impl_->mtx);
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        return Result<AttemptId>::err(ErrorCode::not_found, "unknown request");
    }
    auto& rs = it->second;
    if (rs.req.max_attempts != 0 && rs.retry_count >= rs.req.max_attempts) {
        return Result<AttemptId>::err(ErrorCode::retryable_failure,
                                      "retry limit reached");
    }
    if (rs.terminal || rs.cancelled) {
        return Result<AttemptId>::err(ErrorCode::invalid_state, "cannot retry a terminal request");
    }
    // A retry uses a NEW attempt identity and restarts from the current
    // authoritative state (committed tokens are preserved).
    rs.attempt = AttemptId{rs.attempt.get() + 1};
    ++rs.retry_count;
    rs.cycle = SpeculativeStateMachine(SpeculativePhase::Ready);
    ++impl_->stats.retry_count;
    impl_->push_event(id, "request.retry", "new attempt " + rs.attempt.str(), "{}");
    return Result<AttemptId>::ok(rs.attempt);
}


Result<CycleOutcome> SpeculationFabric::run_cycle(RequestId id) {
    std::shared_ptr<ProposalExecutor> proposer;
    std::shared_ptr<VerificationExecutor> verifier;
    ProposalPlan plan;
    ProposalRationale rationale;
    std::uint32_t branch_count = 1;
    std::uint32_t depth = 0;

    // Phase 1: validate + prepare under the lock.
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        auto it = impl_->requests.find(id);
        if (it == impl_->requests.end()) {
            return Result<CycleOutcome>::err(ErrorCode::not_found, "unknown request");
        }
        auto& rs = it->second;
        if (rs.terminal) {
            auto o = CycleOutcome::make(CycleOutcomeKind::Superseded);
            o.phase = SpeculativePhase::Superseded;
            return Result<CycleOutcome>::ok(o);
        }
        if (rs.cancelled) {
            auto o = CycleOutcome::make(CycleOutcomeKind::Cancelled);
            o.phase = SpeculativePhase::Cancelled;
            return Result<CycleOutcome>::ok(o);
        }
        if (rs.req.deadline_ns && impl_->cfg.clock->monotonic_ns() > *rs.req.deadline_ns) {
            auto o = CycleOutcome::make(CycleOutcomeKind::Expired);
            o.phase = SpeculativePhase::Expired;
            impl_->push_event(id, "cycle.expired", "deadline passed", "{}");
            return Result<CycleOutcome>::ok(o);
        }
        if (is_terminal_phase(rs.cycle.phase())) {
            rs.cycle = SpeculativeStateMachine(SpeculativePhase::Ready);
        }
        ModelPairCompatibilityKey key{rs.req.draft_model, rs.req.target_model, 1};
        ModelPairCompatibilityDecision compat = evaluate_compatibility(key);
        if (!compat.compatible) {
            auto o = CycleOutcome::make(CycleOutcomeKind::ProposalNonRetryableFailure);
            o.error = ErrorCode::incompatible_model_pair;
            o.summary = "incompatible pair: " +
                        (compat.reasons.empty() ? std::string("unspecified")
                                                : compat.reasons.front().detail);
            impl_->push_event(id, "cycle.incompatible", o.summary, "{}");
            return Result<CycleOutcome>::ok(o);
        }
        const auto bp = impl_->scheduler->check_backpressure(
            rs.req, static_cast<std::uint32_t>(rs.branches.size()),
            static_cast<std::uint32_t>(rs.proposals.size()),
            impl_->active_reservation_bytes(rs) ? 0 : 1,
            impl_->stats.tokens_proposed == 0
                ? 1.0
                : static_cast<double>(impl_->stats.tokens_accepted) /
                      static_cast<double>(impl_->stats.tokens_proposed));
        if (bp.blocked) {
            ++impl_->stats.speculation_bypasses;
            auto o = CycleOutcome::make(CycleOutcomeKind::ProposalNonRetryableFailure);
            o.summary = "bypassed: " + bp.detail;
            impl_->push_event(id, "cycle.bypass", bp.detail, "{}");
            return Result<CycleOutcome>::ok(o);
        }

        plan = impl_->scheduler->plan_proposal(rs.req, rs.auth_gen, 0, 0, 0.0,
                                               rationale);
        depth = std::max(1u, plan.depth);
        branch_count = std::max(1u, plan.branch_count);
        proposer = impl_->cfg.proposer;
        verifier = impl_->cfg.verifier;
        if (!proposer || !verifier) {
            auto o = CycleOutcome::make(CycleOutcomeKind::ProposalNonRetryableFailure);
            o.error = ErrorCode::not_implemented;
            o.summary = "no proposer/verifier executor registered";
            return Result<CycleOutcome>::ok(o);
        }

        (void)rs.cycle.apply(SpeculationEvent::PlanProposal);
        (void)rs.cycle.apply(SpeculationEvent::ReserveProposal);
        (void)rs.cycle.apply(SpeculationEvent::DispatchProposal);
    }

    struct BranchResult {
        ProposalResult pr;
        BranchId branch;
        ModelIdentity proposer_model;
    };
    std::vector<BranchResult> proposed;
    for (std::uint32_t b = 0; b < branch_count; ++b) {
        ProposalInput pin;
        {
            std::lock_guard<std::mutex> g(impl_->mtx);
            auto& rs = impl_->requests[id];
            pin.base_state = rs.auth_state;
            pin.base_generation = rs.auth_gen;
            pin.depth = depth;
            pin.branch_index = b;
            pin.branch_count = branch_count;
            pin.deterministic_seed = rs.req.id.get() ^ (rs.attempt.get() * 0x100000001B3ULL);
            pin.provenance.epoch = rs.epoch;
            pin.provenance.request = id;
            pin.provenance.sequence = rs.req.sequence;
            pin.provenance.attempt = rs.attempt;
            pin.provenance.branch = BranchId{b + 1};
            pin.provenance.cycle = CycleId{rs.completed_cycles + 1};
            pin.provenance.proposer_worker = WorkerId{};
            pin.provenance.proposer_boot = WorkerBootId{};
            pin.provenance.proposer_model = rs.req.draft_model;
            pin.provenance.base_generation = rs.auth_gen;
            pin.provenance.dispatch = plan.dispatch;
        }
        auto pr = proposer->propose(pin);
        if (pr.is_error()) {
            return Result<CycleOutcome>::err(pr.error_code(), pr.error().message);
        }
        proposed.push_back(BranchResult{pr.value(), BranchId{b + 1}, pin.provenance.proposer_model});
    }

    std::vector<AcceptanceResult> accepted;
    for (const auto& br : proposed) {
        VerificationInput vin;
        {
            std::lock_guard<std::mutex> g(impl_->mtx);
            auto& rs = impl_->requests[id];
            vin.request = id;
            vin.sequence = rs.req.sequence;
            vin.attempt = rs.attempt;
            vin.branch = br.branch;
            vin.candidate = *br.pr.candidate;
            vin.candidate_identity.tokenizer = rs.req.draft_model.tokenizer.id;
            vin.candidate_identity.vocab_size = 4096;
            vin.proposer = br.proposer_model;
            vin.verifier = rs.req.target_model;
            vin.compatibility.proposer = br.proposer_model;
            vin.compatibility.verifier = rs.req.target_model;
            vin.compatibility.protocol_version = 1;
            vin.authoritative_state = rs.auth_state;
            vin.authoritative_generation = rs.auth_gen;
            vin.authoritative_state_generation = rs.auth_state_gen;
            vin.dispatch = plan.dispatch;
            vin.epoch = rs.epoch;
        }
        auto vr = verifier->verify(vin);
        if (vr.is_error()) {
            return Result<CycleOutcome>::err(vr.error_code(), vr.error().message);
        }
        accepted.push_back(vr.value());
    }

    std::lock_guard<std::mutex> g(impl_->mtx);
    auto it = impl_->requests.find(id);
    if (it == impl_->requests.end()) {
        return Result<CycleOutcome>::err(ErrorCode::not_found, "unknown request");
    }
    auto& rs = it->second;

    if (rs.terminal || rs.cancelled) {
        auto o = CycleOutcome::make(CycleOutcomeKind::StaleAuthorityRejected);
        o.error = rs.terminal ? ErrorCode::completion_after_terminal
                              : ErrorCode::completion_after_cancellation;
        o.summary = "authority changed while speculative work was in flight";
        ++impl_->stats.proposals_rejected_stale;
        impl_->push_event(id, "cycle.stale", o.summary, "{}");
        return Result<CycleOutcome>::ok(o);
    }

    std::size_t winner = 0;
    for (std::size_t i = 1; i < accepted.size(); ++i) {
        if (accepted[i].accepted_prefix > accepted[winner].accepted_prefix) {
            winner = i;
        } else if (accepted[i].accepted_prefix == accepted[winner].accepted_prefix &&
                   proposed[i].branch.get() < proposed[winner].branch.get()) {
            winner = i;
        }
    }

    const BranchId win_branch = proposed[winner].branch;
    const AcceptanceResult& win_accept = accepted[winner];
    const ProposalResult& win_pr = proposed[winner].pr;
    const std::uint32_t candidate_len = static_cast<std::uint32_t>(win_pr.candidate->size());
    const std::uint32_t accepted_prefix = win_accept.accepted_prefix;
    const std::uint32_t rejected_tokens = candidate_len - accepted_prefix;

    Proposal prop;
    prop.id = ProposalId{static_cast<std::uint64_t>(rs.proposals.size() + 1)};
    prop.generation = ProposalGeneration{rs.completed_cycles + 1};
    prop.provenance.epoch = rs.epoch;
    prop.provenance.request = id;
    prop.provenance.sequence = rs.req.sequence;
    prop.provenance.attempt = rs.attempt;
    prop.provenance.branch = win_branch;
    prop.candidate = *win_pr.candidate;
    prop.token_identity.tokenizer = rs.req.draft_model.tokenizer.id;
    prop.token_identity.vocab_size = 4096;
    prop.base_state = rs.auth_state;
    prop.base_state_generation = rs.auth_state_gen;
    prop.speculative_state = win_pr.speculative_state.value_or(StateDescriptor{}).ref;
    prop.compute_spent = win_pr.compute_spent;
    prop.memory_held = win_pr.memory_held;
    prop.finalized = true;
    prop.eligible_for_verification = true;
    prop.committed = accepted_prefix > 0;

    CycleOutcome outcome;
    outcome.proposal = prop.id;
    outcome.branch = win_branch;
    outcome.depth = depth;
    outcome.proposed_tokens = candidate_len;

    (void)rs.cycle.apply(SpeculationEvent::ProposalFinished);
    (void)rs.cycle.apply(SpeculationEvent::PlanVerification);
    (void)rs.cycle.apply(SpeculationEvent::ReserveVerification);
    (void)rs.cycle.apply(SpeculationEvent::DispatchVerification);
    (void)rs.cycle.apply(SpeculationEvent::VerificationFinished);

    auto retire_others = [&](std::size_t keep_index) {
        RollbackResult rb;
        rb.accepted_prefix = accepted_prefix;
        rb.rejected_tokens = rejected_tokens;
        rb.restored_generation = rs.auth_gen;
        rb.restored_state = rs.auth_state;
        rb.restored_state_generation = rs.auth_state_gen;
        rb.success = true;
        const std::uint64_t keep_branch = proposed[keep_index].branch.get();
        std::uint64_t loser_count = 0;
        // Record every branch that was proposed; the winner stays active, all
        // others are retired. Each branch has independent identity.
        for (const auto& br : proposed) {
            bool found = false;
            for (auto& bb : rs.branches) {
                if (bb.id == br.branch) { found = true; break; }
            }
            if (!found) {
                Branch nb;
                nb.id = br.branch;
                nb.request = rs.req.id;
                nb.sequence = rs.req.sequence;
                nb.attempt = rs.attempt;
                nb.cycle = CycleId{rs.completed_cycles + 1};
                nb.base_generation = rs.auth_gen;
                nb.proposer = rs.req.draft_model;
                nb.retired = (br.branch.get() != keep_branch);
                rs.branches.push_back(nb);
                if (br.branch.get() != keep_branch) ++loser_count;
            }
            if (br.branch.get() != keep_branch) {
                bool already_retired = false;
                for (auto& bb : rs.branches) {
                    if (bb.id == br.branch) {
                        if (!bb.retired) { bb.retired = true; }
                        already_retired = bb.retired;
                        break;
                    }
                }
                if (already_retired) ++loser_count;
            }
        }
        rb.retired_branches = static_cast<std::uint32_t>(loser_count);
        for (auto& res : rs.reservations) {
            if (res.is_active()) { res.state = ReservationState::Released; ++rb.released_reservations; }
        }
        return rb;
    };

    if (accepted_prefix == candidate_len) {
        for (std::uint32_t i = 0; i < accepted_prefix; ++i) {
            rs.committed.push_back(win_pr.candidate->tokens[i].id);
        }
        rs.auth_gen = AuthGeneration{rs.auth_gen.get() + accepted_prefix};
        rs.auth_state.generation = StateGeneration{rs.auth_state.generation.get() + accepted_prefix};
        rs.auth_state_gen = rs.auth_state.generation;
        rs.proposals.push_back(prop);
        (void)rs.cycle.apply(SpeculationEvent::AcceptFull);
        auto r2 = rs.cycle.apply(SpeculationEvent::Commit);
        if (r2.has_value()) outcome.phase = r2.value();
        outcome.rollback = retire_others(winner);
        outcome.outcome = CycleOutcomeKind::VerificationSuccessFullAccept;
        outcome.commit.success = true;
        outcome.commit.committed_tokens = accepted_prefix;
        outcome.commit.accepted_prefix = accepted_prefix;
        outcome.commit.rejected_tokens = 0;
        outcome.commit.new_generation = rs.auth_gen;
        outcome.commit.new_state = rs.auth_state;
        outcome.commit.new_state_generation = rs.auth_state_gen;
        outcome.accepted_tokens = accepted_prefix;
        outcome.rejected_tokens = 0;
        ++impl_->stats.full_accept_count;
    } else if (accepted_prefix > 0) {
        for (std::uint32_t i = 0; i < accepted_prefix; ++i) {
            rs.committed.push_back(win_pr.candidate->tokens[i].id);
        }
        rs.auth_gen = AuthGeneration{rs.auth_gen.get() + accepted_prefix};
        rs.auth_state.generation = StateGeneration{rs.auth_state.generation.get() + accepted_prefix};
        rs.auth_state_gen = rs.auth_state.generation;
        rs.proposals.push_back(prop);
        (void)rs.cycle.apply(SpeculationEvent::AcceptPartial);
        auto r2 = rs.cycle.apply(SpeculationEvent::Commit);
        if (r2.has_value()) outcome.phase = r2.value();
        outcome.rollback = retire_others(winner);
        outcome.commit.success = true;
        outcome.commit.committed_tokens = accepted_prefix;
        outcome.commit.accepted_prefix = accepted_prefix;
        outcome.commit.rejected_tokens = rejected_tokens;
        outcome.commit.new_generation = rs.auth_gen;
        outcome.commit.new_state = rs.auth_state;
        outcome.commit.new_state_generation = rs.auth_state_gen;
        outcome.outcome = CycleOutcomeKind::VerificationSuccessPartialAccept;
        outcome.accepted_tokens = accepted_prefix;
        outcome.rejected_tokens = rejected_tokens;
        ++impl_->stats.partial_accept_count;
        ++impl_->stats.rollback_count;
    } else {
        rs.proposals.push_back(prop);
        (void)rs.cycle.apply(SpeculationEvent::RejectAll);
        auto r2 = rs.cycle.apply(SpeculationEvent::Rollback);
        if (r2.has_value()) outcome.phase = r2.value();
        outcome.rollback = retire_others(winner);
        outcome.outcome = CycleOutcomeKind::VerificationRejectAll;
        outcome.accepted_tokens = 0;
        outcome.rejected_tokens = candidate_len;
        ++impl_->stats.full_reject_count;
        ++impl_->stats.rollback_count;
    }

    rs.cycle = SpeculativeStateMachine(SpeculativePhase::Ready);
    ++rs.completed_cycles;

    impl_->stats.tokens_proposed += candidate_len;
    impl_->stats.tokens_accepted += accepted_prefix;
    impl_->stats.tokens_rejected += rejected_tokens;
    ++impl_->stats.speculative_cycles_started;
    ++impl_->stats.proposals_produced;
    ++impl_->stats.verifications_run;
    impl_->scheduler->adaptive().record(
        candidate_len == 0 ? 0.0
                           : static_cast<double>(accepted_prefix) /
                                 static_cast<double>(candidate_len));

    impl_->push_event(id, "cycle.completed",
                      outcome.outcome == CycleOutcomeKind::VerificationSuccessFullAccept
                          ? "full accept"
                          : outcome.outcome == CycleOutcomeKind::VerificationSuccessPartialAccept
                                ? "partial accept"
                                : "reject all",
                      "{}");

    return Result<CycleOutcome>::ok(outcome);
}

}  // namespace speculation_fabric