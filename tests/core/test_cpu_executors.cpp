// Deterministic CPU proposer + verifier tests.
#include "sf_test.hpp"
#include "speculation_fabric/core/cpu_proposer.hpp"
#include "speculation_fabric/core/cpu_verifier.hpp"

using namespace speculation_fabric;

static ModelIdentity make_proposer_model() {
    ModelIdentity m;
    m.model = ModelId{101};
    m.revision = Revision{1};
    m.tokenizer.id = TokenizerId{9};
    m.tokenizer.vocab_size = 4096;
    m.executor.kind = ExecutorKind::CPU;
    m.name = "cpu-draft";
    return m;
}

static ProposalInput make_input(std::uint32_t depth, std::uint32_t branch = 0) {
    ProposalInput in;
    in.base_state.id = StateId{1000};
    in.base_state.generation = StateGeneration{5};
    in.base_generation = AuthGeneration{5};
    in.depth = depth;
    in.branch_index = branch;
    in.branch_count = 2;
    in.deterministic_seed = 55;
    in.provenance.proposer_model = make_proposer_model();
    in.provenance.base_generation = in.base_generation;
    in.provenance.sequence = SequenceId{7};
    in.provenance.attempt = AttemptId{1};
    in.provenance.request = RequestId{3};
    in.provenance.branch = BranchId{branch + 1};
    in.provenance.proposer_worker = WorkerId{11};
    in.provenance.proposer_boot = WorkerBootId{111};
    return in;
}

static VerificationInput make_verify(const ProposalResult& pr, std::uint32_t depth) {
    (void)depth;
    VerificationInput v;
    v.authoritative_state.id = StateId{1000};
    v.authoritative_state.generation = StateGeneration{5};
    v.authoritative_generation = AuthGeneration{5};
    v.candidate = *pr.candidate;
    v.candidate_identity.tokenizer = TokenizerId{9};
    v.candidate_identity.vocab_size = 4096;
    v.proposer = make_proposer_model();
    v.sequence = SequenceId{7};
    v.attempt = AttemptId{1};
    v.request = RequestId{3};
    return v;
}

SF_TEST_FN(cpu_proposer_full_accept) {
    CpuProposerConfig pc; pc.aligned_tokens = 8;
    CpuProposerExecutor prop(pc);
    auto in = make_input(8);
    auto r = prop.propose(in);
    SF_CHECK(r.has_value());
    SF_CHECK(r.value().success());
    SF_CHECK(r.value().candidate->size() == 8);

    CpuVerifierExecutor ver;
    auto vr = ver.verify(make_verify(r.value(), 8));
    SF_CHECK(vr.has_value());
    SF_CHECK(vr.value().outcome == AcceptanceOutcome::FullAccept);
    SF_CHECK(vr.value().accepted_prefix == 8);
}

SF_TEST_FN(cpu_proposer_reject_all) {
    CpuProposerConfig pc; pc.aligned_tokens = 0;
    CpuProposerExecutor prop(pc);
    auto in = make_input(5);
    auto r = prop.propose(in);
    CpuVerifierExecutor ver;
    auto vr = ver.verify(make_verify(r.value(), 5));
    SF_CHECK(vr.has_value());
    SF_CHECK(vr.value().outcome == AcceptanceOutcome::RejectAll);
    SF_CHECK(vr.value().accepted_prefix == 0);
}

SF_TEST_FN(cpu_proposer_partial_accept) {
    CpuProposerConfig pc; pc.aligned_tokens = 2;
    CpuProposerExecutor prop(pc);
    auto in = make_input(6);
    auto r = prop.propose(in);
    CpuVerifierExecutor ver;
    auto vr = ver.verify(make_verify(r.value(), 6));
    SF_CHECK(vr.has_value());
    // The aligned prefix matches the target; the suffix diverges.
    SF_CHECK(vr.value().outcome == AcceptanceOutcome::PartialAccept);
    SF_CHECK(vr.value().accepted_prefix == 2);
    SF_CHECK(vr.value().first_rejection_index == 2);
}

SF_TEST_FN(cpu_proposer_deterministic_replay) {
    CpuProposerConfig pc; pc.aligned_tokens = 4;
    CpuProposerExecutor prop(pc);
    auto in = make_input(6);
    auto a = prop.propose(in);
    auto b = prop.propose(in);
    SF_CHECK(a.value().candidate == b.value().candidate);
}

SF_TEST_FN(cpu_proposer_prior_step_dependence) {
    CpuProposerConfig pc; pc.aligned_tokens = 3;
    CpuProposerExecutor prop(pc);
    auto in = make_input(4);
    auto r = prop.propose(in);
    auto tokens = r.value().candidate->tokens;
    SF_CHECK(tokens.size() == 4);
    // A different base generation must produce different tokens: this proves
    // the candidate is a function of the input state, not a constant vector.
    auto in2 = make_input(4);
    in2.base_generation = AuthGeneration{99};
    auto r2 = prop.propose(in2);
    auto tokens2 = r2.value().candidate->tokens;
    bool differ = false;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].id != tokens2[i].id) { differ = true; break; }
    }
    SF_CHECK(differ);
}

SF_TEST_FN(cpu_proposer_branches_distinct) {
    CpuProposerConfig pc; pc.aligned_tokens = 2;
    CpuProposerExecutor prop(pc);
    auto in0 = make_input(6, 0);
    auto in1 = make_input(6, 1);
    auto a = prop.propose(in0);
    auto b = prop.propose(in1);
    // After the shared aligned prefix the branches must be distinct.
    SF_CHECK(a.value().candidate->size() == 6);
    SF_CHECK(b.value().candidate->size() == 6);
    // The full vectors differ across distinct branches.
    bool differ = false;
    for (std::size_t i = 0; i < 6; ++i) {
        if (a.value().candidate->tokens[i].id != b.value().candidate->tokens[i].id) {
            differ = true; break;
        }
    }
    SF_CHECK(differ);
}

SF_TEST_FN(verifier_retryable_failure) {
    CpuVerifierExecutor ver;
    auto v = make_verify(ProposalResult{}, 4);
    v.candidate_identity.tokenizer = TokenizerId{};   // null tokenizer
    v.candidate = CandidateSequence(std::vector<Token>{{1},{2},{3},{4}});
    auto vr = ver.verify(v);
    SF_CHECK(vr.value().outcome == AcceptanceOutcome::VerifierFailure);
    SF_CHECK(vr.value().retryable == true);
}

SF_TEST_FN(verifier_incompatible_vocab_retryable_false) {
    CpuVerifierExecutor ver;
    auto v = make_verify(ProposalResult{}, 4);
    v.candidate_identity.tokenizer = TokenizerId{9};
    v.candidate_identity.vocab_size = 100;   // wrong vocabulary
    v.candidate = CandidateSequence(std::vector<Token>{{1},{2},{3},{4}});
    auto vr = ver.verify(v);
    SF_CHECK(vr.value().outcome == AcceptanceOutcome::VerifierFailure);
    SF_CHECK(vr.value().retryable == false);
}