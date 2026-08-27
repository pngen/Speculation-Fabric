// Adversarial tests: invalid inputs, incompatible pairs, bounds, duplicates.
#include "sf_test.hpp"
#include "speculation_fabric/core/cpu_proposer.hpp"
#include "speculation_fabric/core/cpu_verifier.hpp"
#include "speculation_fabric/core/fabric.hpp"

using namespace speculation_fabric;

static ModelIdentity mk(TokenizerId tok) {
    ModelIdentity m; m.model = ModelId{101}; m.revision = Revision{1};
    m.tokenizer.id = tok; m.tokenizer.name = "adv-tok"; m.tokenizer.vocab_size = 4096;
    m.executor.kind = ExecutorKind::CPU; m.executor.protocol_version = 1; m.name = "adv";
    return m;
}

SF_TEST_FN(adversarial_zero_depth_proposal_rejected) {
    CpuProposerExecutor prop;
    ProposalInput in;
    in.base_state.id = StateId{1}; in.base_generation = AuthGeneration{0};
    in.depth = 0; in.provenance.request = RequestId{1};
    auto r = prop.propose(in);
    SF_CHECK(r.value().outcome == ProposalOutcome::NonRetryableFailure);
    SF_CHECK(r.value().error == ErrorCode::invalid_zero_depth);
}

SF_TEST_FN(adversarial_absurd_depth_rejected) {
    CpuProposerExecutor prop;
    ProposalInput in; in.base_state.id = StateId{1};
    in.depth = 1000; in.provenance.request = RequestId{1};
    auto r = prop.propose(in);
    SF_CHECK(r.value().outcome == ProposalOutcome::NonRetryableFailure);
    SF_CHECK(r.value().error == ErrorCode::invalid_depth);
}

SF_TEST_FN(adversarial_incompatible_pair_rejected) {
    auto f = [&]() { auto p = std::make_shared<CpuProposerExecutor>(); auto v = std::make_shared<CpuVerifierExecutor>();
        SpeculationFabric::Config c; c.proposer = p; c.verifier = v; c.policy.adaptive_depth_enabled=false; return SpeculationFabric(c); }();
    SpeculationRequest r; r.id = RequestId{1}; r.sequence = SequenceId{2};
    r.draft_model = mk(TokenizerId{9}); r.target_model = mk(TokenizerId{10});
    r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model; r.pair_key.protocol_version = 1;
    r.policy.max_depth = 4; r.policy.adaptive_depth_enabled = false;
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    SF_CHECK(c.value().outcome == CycleOutcomeKind::ProposalNonRetryableFailure);
    SF_CHECK_EQ(f.authoritative_length(r.id).value(), 0u);
}

SF_TEST_FN(adversarial_duplicate_request_id_rejected) {
    auto f = [&]() { auto p = std::make_shared<CpuProposerExecutor>(); auto v = std::make_shared<CpuVerifierExecutor>();
        SpeculationFabric::Config c; c.proposer = p; c.verifier = v; c.policy.adaptive_depth_enabled=false; return SpeculationFabric(c); }();
    SpeculationRequest r; r.id = RequestId{9}; r.sequence = SequenceId{2};
    r.draft_model = mk(TokenizerId{9}); r.target_model = mk(TokenizerId{9});
    r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model; r.pair_key.protocol_version = 1;
    r.policy.max_depth = 4;
    (void)f.submit(r);
    auto second = f.submit(r);
    SF_CHECK(second.is_error());
    SF_CHECK(second.error_code() == ErrorCode::already_exists);
}

SF_TEST_FN(adversarial_empty_candidate_verifier_retryable) {
    CpuVerifierExecutor ver;
    VerificationInput v;
    v.candidate_identity.tokenizer = TokenizerId{9};
    v.candidate_identity.vocab_size = 4096;
    auto r = ver.verify(v);   // empty candidate
    SF_CHECK(r.value().outcome == AcceptanceOutcome::VerifierFailure);
    SF_CHECK(r.value().retryable == true);
}

SF_TEST_FN(adversarial_acceptance_cannot_exceed_candidate) {
    // The verifier returns a prefix that never exceeds the candidate length.
    auto f = [&]() { auto p = std::make_shared<CpuProposerExecutor>(); auto v = std::make_shared<CpuVerifierExecutor>();
        SpeculationFabric::Config c; c.proposer = p; c.verifier = v; c.policy.adaptive_depth_enabled=false; return SpeculationFabric(c); }();
    SpeculationRequest r; r.id = RequestId{5}; r.sequence = SequenceId{2};
    r.draft_model = mk(TokenizerId{9}); r.target_model = mk(TokenizerId{9});
    r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model; r.pair_key.protocol_version = 1;
    r.policy.max_depth = 4;
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    // accepted_tokens never exceeds proposed_tokens.
    SF_CHECK(c.value().accepted_tokens <= c.value().proposed_tokens);
    SF_CHECK(c.value().proposed_tokens == 4u);
}

SF_TEST_FN(adversarial_reject_then_no_commit) {
    auto f = [&]() { CpuProposerConfig pc; pc.aligned_tokens = 0; auto p = std::make_shared<CpuProposerExecutor>(pc);
        auto v = std::make_shared<CpuVerifierExecutor>();
        SpeculationFabric::Config c; c.proposer = p; c.verifier = v; c.policy.adaptive_depth_enabled=false; return SpeculationFabric(c); }();
    SpeculationRequest r; r.id = RequestId{7}; r.sequence = SequenceId{2};
    r.draft_model = mk(TokenizerId{9}); r.target_model = mk(TokenizerId{9});
    r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model; r.pair_key.protocol_version = 1;
    r.policy.max_depth = 4;
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    SF_CHECK(c.value().outcome == CycleOutcomeKind::VerificationRejectAll);
    SF_CHECK_EQ(f.authoritative_length(r.id).value(), 0u);
}