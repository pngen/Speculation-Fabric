// Runtime (SpeculationFabric) end-to-end cycle tests.
#include "sf_test.hpp"
#include <functional>
#include <memory>
#include "speculation_fabric/core/fabric.hpp"

using namespace speculation_fabric;

static ModelIdentity make_model(TokenizerId tok) {
    ModelIdentity m;
    m.model = ModelId{101};
    m.revision = Revision{1};
    m.tokenizer.id = tok;
    m.tokenizer.name = "cpu-tok";
    m.tokenizer.vocab_size = 4096;
    m.executor.id = ExecutorId{1};
    m.executor.name = "cpu";
    m.executor.kind = ExecutorKind::CPU;
    m.executor.protocol_version = 1;
    m.name = "cpu-model";
    return m;
}

static SpeculationRequest make_req(std::uint32_t md, std::uint32_t mb, std::uint32_t pl) {
    SpeculationRequest r;
    r.id = RequestId{100};
    r.sequence = SequenceId{7};
    r.tenant = TenantId{1};
    r.draft_model = make_model(TokenizerId{9});
    r.target_model = make_model(TokenizerId{9});
    r.pair_key.proposer = r.draft_model;
    r.pair_key.verifier = r.target_model;
    r.pair_key.protocol_version = 1;
    for (std::uint32_t i = 0; i < pl; ++i) r.prefix.push_back(1000 + i);
    r.max_attempts = 3;
    r.policy.max_depth = md;
    r.policy.max_branches = mb;
    r.policy.adaptive_depth_enabled = false;
    r.policy.speculating_enabled = true;
    return r;
}

static SpeculationFabric build(std::uint32_t aligned, std::uint32_t md, std::uint32_t mb) {
    CpuProposerConfig pc;
    pc.aligned_tokens = aligned;
    auto proposer = std::make_shared<CpuProposerExecutor>(pc);
    auto verifier = std::make_shared<CpuVerifierExecutor>();
    SpeculationFabric::Config cfg;
    cfg.proposer = proposer;
    cfg.verifier = verifier;
    cfg.policy.max_depth = md;
    cfg.policy.max_branches = mb;
    return SpeculationFabric(cfg);
}

SF_TEST_FN(engine_full_accept_commits_depth) {
    auto fabric = build(5u, 5u, 1u);
    auto req = make_req(5u, 1u, 0u);
    auto sr = fabric.submit(req);
    SF_CHECK(sr.has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    auto c = o.value();
    SF_CHECK(c.outcome == CycleOutcomeKind::VerificationSuccessFullAccept);
    SF_CHECK(c.commit.success);
    SF_CHECK_EQ(c.commit.committed_tokens, 5u);
    SF_CHECK_EQ(c.accepted_tokens, 5u);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 5u);
}

SF_TEST_FN(engine_partial_accept_commit_prefix) {
    auto fabric = build(3u, 6u, 1u);
    auto req = make_req(6u, 1u, 0u);
    auto sr = fabric.submit(req);
    SF_CHECK(sr.has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    auto c = o.value();
    SF_CHECK(c.outcome == CycleOutcomeKind::VerificationSuccessPartialAccept);
    SF_CHECK_EQ(c.accepted_tokens, 3u);
    SF_CHECK_EQ(c.rejected_tokens, 3u);
    SF_CHECK(c.commit.success);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 3u);
    SF_CHECK(c.rollback.success);
    SF_CHECK_EQ(c.rollback.rejected_tokens, 3u);
}

SF_TEST_FN(engine_reject_all_commits_nothing) {
    auto fabric = build(0u, 5u, 1u);
    auto req = make_req(5u, 1u, 0u);
    auto sr = fabric.submit(req);
    SF_CHECK(sr.has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    auto c = o.value();
    SF_CHECK(c.outcome == CycleOutcomeKind::VerificationRejectAll);
    SF_CHECK_EQ(c.accepted_tokens, 0u);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 0u);
    SF_CHECK(c.rollback.success);
}

SF_TEST_FN(engine_authoritative_monotonic_no_decrease) {
    auto fabric = build(3u, 6u, 1u);
    auto req = make_req(6u, 1u, 0u);
    auto sr = fabric.submit(req);
    SF_CHECK(sr.has_value());
    auto a = fabric.authoritative_length(req.id).value();
    for (int i = 0; i < 3; ++i) {
        auto o = fabric.run_cycle(req.id);
        SF_CHECK(o.has_value());
        auto b = fabric.authoritative_length(req.id).value();
        SF_CHECK(b >= a);
        a = b;
    }
}

SF_TEST_FN(engine_multi_branch_single_winner) {
    auto fabric = build(2u, 5u, 2u);
    auto req = make_req(5u, 2u, 0u);
    auto sr = fabric.submit(req);
    SF_CHECK(sr.has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    auto c = o.value();
    SF_CHECK_EQ(c.accepted_tokens, 2u);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 2u);
    auto brs = fabric.branches(req.id);
    SF_CHECK(brs.has_value());
    SF_CHECK(brs.value().size() >= 2u);
}

SF_TEST_FN(engine_cancel_releases_and_blocks) {
    auto fabric = build(5u, 5u, 1u);
    auto req = make_req(5u, 1u, 0u);
    auto sr = fabric.submit(req);
    SF_CHECK(sr.has_value());
    auto c = fabric.cancel(req.id, "test cancel");
    SF_CHECK(c.has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    SF_CHECK(o.value().outcome == CycleOutcomeKind::Cancelled);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 0u);
}

SF_TEST_FN(engine_retry_new_attempt) {
    auto fabric = build(0u, 5u, 1u);
    auto req = make_req(5u, 1u, 0u);
    req.max_attempts = 2u;
    auto sr = fabric.submit(req);
    SF_CHECK(sr.has_value());
    auto tr = fabric.retry(req.id);
    SF_CHECK(tr.has_value());
    SF_CHECK_EQ(tr.value().get(), 2u);
}

SF_TEST_FN(engine_incompatible_pair_rejected) {
    auto fabric = build(5u, 5u, 1u);
    auto req = make_req(5u, 1u, 0u);
    req.target_model = make_model(TokenizerId{10});
    req.draft_model = make_model(TokenizerId{9});
    auto sr = fabric.submit(req);
    SF_CHECK(sr.has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    SF_CHECK(o.value().outcome == CycleOutcomeKind::ProposalNonRetryableFailure);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 0u);
}

// ---------------------------------------------------------------------------
// Regression tests: commit / authority / accounting boundaries of run_cycle.
// ---------------------------------------------------------------------------
namespace {

// A proposer that can run a hook before proposing, choose the aligned prefix per
// call, return a deeper candidate than dispatched, and rewrite the candidate.
// The candidate itself always comes from the real CPU proposer.
class HookedProposer final : public ProposalExecutor {
public:
    std::function<void(const ProposalInput&)> before;
    std::function<std::uint32_t(const ProposalInput&)> aligned;
    std::function<void(ProposalResult&)> after;
    std::uint32_t extra_depth{0};

    [[nodiscard]] ExecutorIdentity identity() const override {
        return CpuProposerExecutor().identity();
    }
    [[nodiscard]] ExecutorKind kind() const noexcept override { return ExecutorKind::CPU; }

    Result<ProposalResult> propose(const ProposalInput& input) override {
        if (before) before(input);
        CpuProposerConfig pc;
        pc.aligned_tokens = aligned ? aligned(input) : 1000u;
        CpuProposerExecutor inner(pc);
        ProposalInput in = input;
        in.depth += extra_depth;
        auto r = inner.propose(in);
        if (r.has_value() && after) after(r.value());
        return r;
    }
};

// A verifier whose acceptance is defined by the test.
class StubVerifier final : public VerificationExecutor {
public:
    std::function<AcceptanceResult(const VerificationInput&)> fn;

    [[nodiscard]] ExecutorIdentity identity() const override {
        return CpuVerifierExecutor().identity();
    }
    [[nodiscard]] ExecutorKind kind() const noexcept override { return ExecutorKind::CPU; }

    Result<AcceptanceResult> verify(const VerificationInput& input) override {
        return Result<AcceptanceResult>::ok(fn(input));
    }
};

SpeculationFabric::Config cfg_with(std::shared_ptr<ProposalExecutor> p,
                                   std::shared_ptr<VerificationExecutor> v,
                                   std::uint32_t md, std::uint32_t mb) {
    SpeculationFabric::Config cfg;
    cfg.proposer = std::move(p);
    cfg.verifier = std::move(v);
    cfg.policy.max_depth = md;
    cfg.policy.max_branches = mb;
    cfg.policy.adaptive_depth_enabled = false;
    return cfg;
}

AcceptanceResult accept_everything(const VerificationInput& in) {
    AcceptanceResult a;
    a.outcome = AcceptanceOutcome::FullAccept;
    a.candidate_length = static_cast<std::uint32_t>(in.candidate.size());
    a.accepted_prefix = a.candidate_length;
    a.first_rejection_index = a.candidate_length;
    return a;
}

}  // namespace

// A candidate that is wrong at position 1 and right again afterwards: only the
// one-token prefix is accepted and committed; the rejected token never becomes
// authoritative.
SF_TEST_FN(regression_mid_candidate_rejection_commits_only_the_prefix) {
    auto proposer = std::make_shared<HookedProposer>();
    proposer->after = [](ProposalResult& r) {
        r.candidate->tokens[1].id = (r.candidate->tokens[1].id + 1u) % 4096u;
    };
    SpeculationFabric fabric(cfg_with(proposer, std::make_shared<CpuVerifierExecutor>(), 5u, 1u));
    auto req = make_req(5u, 1u, 0u);
    SF_CHECK(fabric.submit(req).has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    SF_CHECK(o.value().outcome == CycleOutcomeKind::VerificationSuccessPartialAccept);
    SF_CHECK_EQ(o.value().accepted_tokens, 1u);
    SF_CHECK_EQ(o.value().rejected_tokens, 4u);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 1u);
}

// A new attempt starts while a cycle is in flight: the completion from the
// earlier attempt is stale and commits nothing; the current attempt still works.
SF_TEST_FN(regression_completion_from_earlier_attempt_commits_nothing) {
    SpeculationFabric* fab = nullptr;
    const RequestId rid{100};
    bool fired = false;
    auto proposer = std::make_shared<HookedProposer>();
    proposer->before = [&](const ProposalInput&) {
        if (!fired) {
            fired = true;
            (void)fab->retry(rid);
        }
    };
    SpeculationFabric fabric(cfg_with(proposer, std::make_shared<CpuVerifierExecutor>(), 5u, 1u));
    fab = &fabric;
    auto req = make_req(5u, 1u, 0u);
    SF_CHECK(fabric.submit(req).has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    SF_CHECK(o.value().outcome == CycleOutcomeKind::StaleAuthorityRejected);
    SF_CHECK(o.value().error == ErrorCode::stale_attempt);
    SF_CHECK(!o.value().commit.success);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 0u);
    SF_CHECK_EQ(fabric.stats().stale_rejections_by_attempt, 1u);

    auto o2 = fabric.run_cycle(req.id);
    SF_CHECK(o2.has_value());
    SF_CHECK(o2.value().outcome == CycleOutcomeKind::VerificationSuccessFullAccept);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 5u);
}

// The authoritative base advances while a cycle is in flight: the completion
// targets the old base and commits nothing, even when the verifier accepts it.
SF_TEST_FN(regression_completion_on_an_advanced_base_commits_nothing) {
    SpeculationFabric* fab = nullptr;
    const RequestId rid{100};
    int calls = 0;
    auto proposer = std::make_shared<HookedProposer>();
    proposer->before = [&](const ProposalInput&) {
        if (calls++ == 0) (void)fab->run_cycle(rid);   // a whole cycle commits meanwhile
    };
    auto verifier = std::make_shared<StubVerifier>();
    verifier->fn = accept_everything;
    SpeculationFabric fabric(cfg_with(proposer, verifier, 5u, 1u));
    fab = &fabric;
    auto req = make_req(5u, 1u, 0u);
    SF_CHECK(fabric.submit(req).has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    SF_CHECK(o.value().outcome == CycleOutcomeKind::StaleAuthorityRejected);
    SF_CHECK(o.value().error == ErrorCode::wrong_base_generation);
    SF_CHECK(!o.value().commit.success);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 5u);   // the inner cycle only
    SF_CHECK_EQ(fabric.stats().stale_rejections_by_generation, 1u);
}

// A cycle ends in the lifecycle phase of its outcome, and reports its acceptance.
SF_TEST_FN(regression_cycle_reaches_its_final_lifecycle_phase) {
    {
        auto fabric = build(5u, 5u, 1u);
        auto req = make_req(5u, 1u, 0u);
        SF_CHECK(fabric.submit(req).has_value());
        auto o = fabric.run_cycle(req.id);
        SF_CHECK(o.has_value());
        SF_CHECK(o.value().phase == SpeculativePhase::Committed);
        SF_CHECK(o.value().acceptance == AcceptanceOutcome::FullAccept);
    }
    {
        auto fabric = build(3u, 6u, 1u);
        auto req = make_req(6u, 1u, 0u);
        SF_CHECK(fabric.submit(req).has_value());
        auto o = fabric.run_cycle(req.id);
        SF_CHECK(o.has_value());
        SF_CHECK(o.value().phase == SpeculativePhase::Committed);
        SF_CHECK(o.value().acceptance == AcceptanceOutcome::PartialAccept);
    }
    {
        auto fabric = build(0u, 5u, 1u);
        auto req = make_req(5u, 1u, 0u);
        SF_CHECK(fabric.submit(req).has_value());
        auto o = fabric.run_cycle(req.id);
        SF_CHECK(o.has_value());
        SF_CHECK(o.value().phase == SpeculativePhase::RolledBack);
        SF_CHECK(o.value().acceptance == AcceptanceOutcome::RejectAll);
    }
}

// A proposer result that did not succeed carries no candidate: the cycle
// reports the proposal failure instead of verifying or committing anything.
SF_TEST_FN(regression_failed_proposal_is_reported_and_commits_nothing) {
    auto fabric = build(300u, 300u, 1u);   // beyond the CPU proposer's depth bound
    auto req = make_req(300u, 1u, 0u);
    SF_CHECK(fabric.submit(req).has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    SF_CHECK(o.value().outcome == CycleOutcomeKind::ProposalNonRetryableFailure);
    SF_CHECK(o.value().error == ErrorCode::invalid_depth);
    SF_CHECK(!o.value().commit.success);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 0u);
}

// A candidate deeper than the dispatched depth is an impossible depth.
SF_TEST_FN(regression_candidate_deeper_than_dispatched_is_rejected) {
    auto proposer = std::make_shared<HookedProposer>();
    proposer->extra_depth = 2u;
    SpeculationFabric fabric(cfg_with(proposer, std::make_shared<CpuVerifierExecutor>(), 5u, 1u));
    auto req = make_req(5u, 1u, 0u);
    SF_CHECK(fabric.submit(req).has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    SF_CHECK(o.value().outcome == CycleOutcomeKind::ProposalNonRetryableFailure);
    SF_CHECK(o.value().error == ErrorCode::impossible_candidate_depth);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 0u);
}

// A verifier failure is neither an accept nor a reject.
SF_TEST_FN(regression_verifier_failure_is_not_an_acceptance_result) {
    auto verifier = std::make_shared<StubVerifier>();
    verifier->fn = [](const VerificationInput& in) {
        AcceptanceResult a;
        a.outcome = AcceptanceOutcome::VerifierFailure;
        a.retryable = true;
        a.candidate_length = static_cast<std::uint32_t>(in.candidate.size());
        a.verifier_note = "verifier unavailable";
        return a;
    };
    SpeculationFabric fabric(cfg_with(std::make_shared<CpuProposerExecutor>(), verifier, 5u, 1u));
    auto req = make_req(5u, 1u, 0u);
    SF_CHECK(fabric.submit(req).has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    SF_CHECK(o.value().outcome == CycleOutcomeKind::VerificationRetryableFailure);
    SF_CHECK(!o.value().commit.success);
    SF_CHECK(!o.value().rollback.success);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 0u);
    const auto st = fabric.stats();
    SF_CHECK_EQ(st.full_reject_count, 0u);
    SF_CHECK_EQ(st.full_accept_count, 0u);
    SF_CHECK_EQ(st.tokens_rejected, 0u);
}

// An acceptance that covers more positions than the candidate holds is
// malformed and commits nothing.
SF_TEST_FN(regression_acceptance_beyond_the_candidate_commits_nothing) {
    auto verifier = std::make_shared<StubVerifier>();
    verifier->fn = [](const VerificationInput& in) {
        AcceptanceResult a;
        a.outcome = AcceptanceOutcome::PartialAccept;
        a.candidate_length = static_cast<std::uint32_t>(in.candidate.size());
        a.accepted_prefix = a.candidate_length + 3u;
        return a;
    };
    SpeculationFabric fabric(cfg_with(std::make_shared<CpuProposerExecutor>(), verifier, 5u, 1u));
    auto req = make_req(5u, 1u, 0u);
    SF_CHECK(fabric.submit(req).has_value());
    auto o = fabric.run_cycle(req.id);
    SF_CHECK(o.has_value());
    SF_CHECK(o.value().outcome == CycleOutcomeKind::VerificationNonRetryableFailure);
    SF_CHECK(o.value().error == ErrorCode::acceptance_exceeds_candidate);
    SF_CHECK(!o.value().commit.success);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 0u);
}

// Completed cycles are history, not outstanding speculative work: a request
// keeps decoding past the outstanding-work limit (8 by default).
SF_TEST_FN(regression_completed_cycles_are_not_outstanding_work) {
    auto fabric = build(2u, 2u, 1u);
    auto req = make_req(2u, 1u, 0u);
    SF_CHECK(fabric.submit(req).has_value());
    for (int i = 0; i < 12; ++i) {
        auto o = fabric.run_cycle(req.id);
        SF_CHECK(o.has_value());
        SF_CHECK(o.value().outcome == CycleOutcomeKind::VerificationSuccessFullAccept);
    }
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 24u);
    SF_CHECK_EQ(fabric.stats().speculation_bypasses, 0u);
}

// A duplicate submission is rejected for every id value and never resets the
// authoritative state of the registered request.
SF_TEST_FN(regression_duplicate_submit_never_overwrites_authoritative_state) {
    auto fabric = build(5u, 5u, 1u);
    auto req = make_req(5u, 1u, 0u);
    req.id = RequestId{};
    SF_CHECK(fabric.submit(req).has_value());
    SF_CHECK(fabric.run_cycle(req.id).has_value());
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 5u);
    auto again = fabric.submit(req);
    SF_CHECK(again.is_error());
    SF_CHECK(again.error_code() == ErrorCode::already_exists);
    SF_CHECK_EQ(fabric.authoritative_length(req.id).value(), 5u);
}

// Every losing branch is counted once, and a branch record describes the
// branch of the current cycle: only the committed branch stays active.
SF_TEST_FN(regression_losing_branches_counted_once_and_records_follow_the_cycle) {
    auto proposer = std::make_shared<HookedProposer>();
    proposer->aligned = [](const ProposalInput& in) -> std::uint32_t {
        // Cycle 1: branch 1 is the perfect draft. Cycle 2: branch 2 is.
        const std::uint32_t best = (in.provenance.cycle.get() == 1u) ? 0u : 1u;
        return in.branch_index == best ? 1000u : 0u;
    };
    SpeculationFabric fabric(cfg_with(proposer, std::make_shared<CpuVerifierExecutor>(), 5u, 3u));
    auto req = make_req(5u, 3u, 0u);
    SF_CHECK(fabric.submit(req).has_value());

    auto c1 = fabric.run_cycle(req.id);
    SF_CHECK(c1.has_value());
    SF_CHECK(c1.value().branch == BranchId{1});
    SF_CHECK_EQ(c1.value().accepted_tokens, 5u);
    SF_CHECK_EQ(c1.value().rollback.retired_branches, 2u);

    auto c2 = fabric.run_cycle(req.id);
    SF_CHECK(c2.has_value());
    SF_CHECK(c2.value().branch == BranchId{2});
    SF_CHECK_EQ(c2.value().accepted_tokens, 5u);
    SF_CHECK_EQ(c2.value().rollback.retired_branches, 2u);

    const auto brs = fabric.branches(req.id).value();
    SF_CHECK_EQ(brs.size(), 3u);
    for (const auto& b : brs) {
        SF_CHECK_EQ(b.retired, b.id != BranchId{2});
        SF_CHECK_EQ(b.cycle.get(), 2u);
        SF_CHECK_EQ(b.base_generation.get(), 5u);
    }
}

// Event identities stay unique after the bounded event history starts
// dropping old events.
SF_TEST_FN(regression_event_ids_stay_unique_past_the_history_bound) {
    auto fabric = build(5u, 5u, 1u);
    auto req = make_req(5u, 1u, 0u);
    SF_CHECK(fabric.submit(req).has_value());
    for (int i = 0; i < 2100; ++i) fabric.emit_event(req.id, "probe", "probe", "{}");
    const auto ev = fabric.events(req.id);
    SF_CHECK(ev.size() >= 2u);
    bool increasing = true;
    for (std::size_t i = 1; i < ev.size(); ++i) {
        if (!(ev[i - 1].id < ev[i].id)) increasing = false;
    }
    SF_CHECK(increasing);
}
