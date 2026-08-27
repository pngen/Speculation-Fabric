// Runtime (SpeculationFabric) end-to-end cycle tests.
#include "sf_test.hpp"
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
