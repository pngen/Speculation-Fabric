// External downstream consumer: exercises real public API behavior.
#include <cstdio>
#include <memory>

#include <speculation_fabric/core/fabric.hpp>
#include <speculation_fabric/core/cpu_proposer.hpp>
#include <speculation_fabric/core/cpu_verifier.hpp>

using namespace speculation_fabric;

int main() {
    CpuProposerConfig pc;
    pc.aligned_tokens = 5;   // full acceptance for depth-5 proposal
    auto proposer = std::make_shared<CpuProposerExecutor>(pc);
    auto verifier = std::make_shared<CpuVerifierExecutor>();

    SpeculationFabric::Config cfg;
    cfg.proposer = proposer;
    cfg.verifier = verifier;
    cfg.policy.max_depth = 5;
    cfg.policy.max_branches = 1;
    SpeculationFabric fabric(cfg);

    SpeculationRequest req;
    req.id = RequestId{1};
    req.sequence = SequenceId{2};
    req.tenant = TenantId{3};
    req.draft_model.model = ModelId{10};
    req.draft_model.revision = Revision{1};
    req.draft_model.tokenizer.id = TokenizerId{9};
    req.draft_model.tokenizer.vocab_size = 4096;
    req.draft_model.executor.kind = ExecutorKind::CPU;
    req.draft_model.name = "draft";
    req.target_model = req.draft_model;
    req.pair_key.proposer = req.draft_model;
    req.pair_key.verifier = req.target_model;
    req.pair_key.protocol_version = 1;
    req.policy.max_depth = 5;
    req.policy.adaptive_depth_enabled = false;

    auto sub = fabric.submit(req);
    if (!sub.has_value()) {
        std::printf("consumer FAIL: submit error %d\n", (int)sub.error_code());
        return 1;
    }
    auto cyc = fabric.run_cycle(req.id);
    if (!cyc.has_value()) {
        std::printf("consumer FAIL: run_cycle error %d\n", (int)cyc.error_code());
        return 1;
    }
    auto len = fabric.authoritative_length(req.id);
    std::printf("consumer OK: outcome=%d committed=%llu accepted=%u\n",
                (int)cyc.value().outcome,
                (unsigned long long)len.value(),
                cyc.value().accepted_tokens);
    if (len.value() != 5) {
        std::printf("consumer FAIL: expected 5 committed tokens\n");
        return 1;
    }
    return 0;
}
