// CUDA proposer/verifier cross-validation against the CPU reference.
#include "sf_test.hpp"
#include <cuda_runtime.h>
#include "speculation_fabric/core/cpu_proposer.hpp"
#include "speculation_fabric/core/cpu_verifier.hpp"
#include "speculation_fabric/core/cuda_executor.hpp"

using namespace speculation_fabric;

static ModelIdentity make_model() {
    ModelIdentity m;
    m.model = ModelId{101};
    m.revision = Revision{1};
    m.tokenizer.id = TokenizerId{9};
    m.tokenizer.vocab_size = 4096;
    m.executor.kind = ExecutorKind::CUDA;
    m.name = "gpu-model";
    return m;
}

static ProposalInput make_input(std::uint32_t depth, std::uint32_t branch) {
    ProposalInput in;
    in.base_state.id = StateId{1000};
    in.base_state.generation = StateGeneration{5};
    in.base_generation = AuthGeneration{5};
    in.depth = depth;
    in.branch_index = branch;
    in.branch_count = 2;
    in.deterministic_seed = 55;
    in.provenance.proposer_model = make_model();
    in.provenance.base_generation = in.base_generation;
    in.provenance.sequence = SequenceId{7};
    in.provenance.attempt = AttemptId{1};
    in.provenance.request = RequestId{3};
    in.provenance.branch = BranchId{branch + 1};
    return in;
}

static VerificationInput make_verify(const CandidateSequence& cand) {
    VerificationInput v;
    v.authoritative_state.id = StateId{1000};
    v.authoritative_state.generation = StateGeneration{5};
    v.authoritative_generation = AuthGeneration{5};
    v.candidate = cand;
    v.candidate_identity.tokenizer = TokenizerId{9};
    v.candidate_identity.vocab_size = 4096;
    v.proposer = make_model();
    v.sequence = SequenceId{7};
    v.attempt = AttemptId{1};
    v.request = RequestId{3};
    return v;
}

SF_TEST_FN(cuda_proposer_matches_cpu_reference) {
    CpuProposerConfig pc;
    pc.aligned_tokens = 8;
    CpuProposerExecutor cpu(pc);
    CudaProposerExecutor gpu(8u, 0u);
    auto in = make_input(8, 0);
    auto cr = cpu.propose(in);
    auto gr = gpu.propose(in);
    SF_CHECK(cr.has_value());
    SF_CHECK(gr.has_value());
    SF_CHECK(gr.value().success());
    SF_CHECK(cr.value().success());
    SF_CHECK(gr.value().candidate == cr.value().candidate);
}

SF_TEST_FN(cuda_verifier_matches_cpu_reference_full_accept) {
    CpuProposerConfig pc;
    pc.aligned_tokens = 8;
    CpuProposerExecutor cpu(pc);
    auto in = make_input(8, 0);
    auto cr = cpu.propose(in);
    CpuVerifierExecutor cpu_v;
    CudaVerifierExecutor gpu_v;
    auto v = make_verify(*cr.value().candidate);
    auto cv = cpu_v.verify(v);
    auto gv = gpu_v.verify(v);
    SF_CHECK(cv.has_value());
    SF_CHECK(gv.has_value());
    SF_CHECK(gv.value().outcome == AcceptanceOutcome::FullAccept);
    SF_CHECK_EQ(gv.value().accepted_prefix, cv.value().accepted_prefix);
}

SF_TEST_FN(cuda_verifier_reject_all_and_partial) {
    CpuProposerConfig pc0; pc0.aligned_tokens = 0;
    CpuProposerExecutor cpu0(pc0);
    auto in0 = make_input(5, 0);
    auto r0 = cpu0.propose(in0);
    CudaVerifierExecutor gpu_v;
    auto v0 = make_verify(*r0.value().candidate);
    auto g0 = gpu_v.verify(v0);
    SF_CHECK(g0.value().outcome == AcceptanceOutcome::RejectAll);

    CpuProposerConfig pc1; pc1.aligned_tokens = 3;
    CpuProposerExecutor cpu1(pc1);
    auto in1 = make_input(6, 0);
    auto r1 = cpu1.propose(in1);
    auto v1 = make_verify(*r1.value().candidate);
    auto g1 = gpu_v.verify(v1);
    SF_CHECK(g1.value().outcome == AcceptanceOutcome::PartialAccept);
    SF_CHECK_EQ(g1.value().accepted_prefix, 3u);
    SF_CHECK_EQ(g1.value().first_rejection_index, 3u);
}

SF_TEST_FN(cuda_memory_recovery_after_cleanup) {
    // Many fresh allocations must leave the device clean (no leaks/errors).
    for (int k = 0; k < 20; ++k) {
        CudaProposerExecutor gpu(2u, 0u);
        auto in = make_input(4, 0);
        auto gr = gpu.propose(in);
        SF_CHECK(gr.has_value());
        SF_CHECK(gr.value().success());
    }
    SF_CHECK_EQ(static_cast<int>(cudaDeviceSynchronize()), 0);
    SF_CHECK_EQ(static_cast<int>(cudaGetLastError()), 0);
}