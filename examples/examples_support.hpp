// Shared helpers for Speculation Fabric examples.
#pragma once
#include <memory>
#include <cstdio>
#include "speculation_fabric/core/fabric.hpp"
#include "speculation_fabric/core/cpu_proposer.hpp"
#include "speculation_fabric/core/cpu_verifier.hpp"
namespace sfex {
using namespace speculation_fabric;
inline ModelIdentity model(TokenizerId tok = TokenizerId{9}) {
    ModelIdentity m; m.model = ModelId{101}; m.revision = Revision{1};
    m.tokenizer.id = tok; m.tokenizer.name = "ex-tok"; m.tokenizer.vocab_size = 4096;
    m.executor.kind = ExecutorKind::CPU; m.executor.protocol_version = 1; m.name = "ex-model";
    return m;
}
inline SpeculationFabric engine(std::uint32_t depth, std::uint32_t branches, std::uint32_t aligned) {
    CpuProposerConfig pc; pc.aligned_tokens = aligned;
    auto p = std::make_shared<CpuProposerExecutor>(pc);
    auto v = std::make_shared<CpuVerifierExecutor>();
    SpeculationFabric::Config cfg; cfg.proposer = p; cfg.verifier = v;
    cfg.policy.max_depth = depth; cfg.policy.max_branches = branches;
    cfg.policy.adaptive_depth_enabled = false; return SpeculationFabric(cfg);
}
inline SpeculationRequest request(std::uint64_t id, std::uint32_t depth, std::uint32_t branches) {
    SpeculationRequest r; r.id = RequestId{id}; r.sequence = SequenceId{id + 1}; r.tenant = TenantId{1};
    r.draft_model = model(); r.target_model = model();
    r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model;
    r.pair_key.protocol_version = 1;
    r.policy.max_depth = depth; r.policy.max_branches = branches; r.policy.adaptive_depth_enabled = false;
    return r;
}
}
