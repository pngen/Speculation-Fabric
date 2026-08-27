// Speculation Fabric benchmark (measured throughput of real completed work).
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "speculation_fabric/core/fabric.hpp"
#include "speculation_fabric/core/cpu_proposer.hpp"
#include "speculation_fabric/core/cpu_verifier.hpp"

using namespace speculation_fabric;

static ModelIdentity mk() {
    ModelIdentity m; m.model = ModelId{101}; m.revision = Revision{1};
    m.tokenizer.id = TokenizerId{9}; m.tokenizer.name = "bench-tok"; m.tokenizer.vocab_size = 4096;
    m.executor.kind = ExecutorKind::CPU; m.executor.protocol_version = 1; m.name = "bench-model";
    return m;
}

struct Wl { std::uint64_t requests; std::uint32_t depth; std::uint32_t branches; std::uint32_t aligned; };

int main(int argc, char** argv) {
    Wl wl;
    wl.requests = 5000; wl.depth = 4; wl.branches = 1; wl.aligned = 4;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--requests" && i + 1 < argc) wl.requests = (std::uint64_t)std::strtoull(argv[++i], nullptr, 10);
        else if (std::string(argv[i]) == "--depth" && i + 1 < argc) wl.depth = (std::uint32_t)std::atoi(argv[++i]);
        else if (std::string(argv[i]) == "--branches" && i + 1 < argc) wl.branches = (std::uint32_t)std::atoi(argv[++i]);
        else if (std::string(argv[i]) == "--aligned" && i + 1 < argc) wl.aligned = (std::uint32_t)std::atoi(argv[++i]);
    }
    std::printf("workload: requests=%llu depth=%u branches=%u aligned=%u\n",
                (unsigned long long)wl.requests, wl.depth, wl.branches, wl.aligned);

    CpuProposerConfig pc; pc.aligned_tokens = wl.aligned;
    auto p = std::make_shared<CpuProposerExecutor>(pc);
    auto v = std::make_shared<CpuVerifierExecutor>();
    SpeculationFabric::Config cfg; cfg.proposer = p; cfg.verifier = v;
    cfg.policy.max_depth = wl.depth; cfg.policy.max_branches = wl.branches;
    cfg.policy.adaptive_depth_enabled = false;
    SpeculationFabric f(cfg);

    std::uint64_t accepted = 0, rejected = 0, proposed = 0, cycles = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < wl.requests; ++i) {
        SpeculationRequest r;
        r.id = RequestId{i + 1}; r.sequence = SequenceId{i + 2}; r.tenant = TenantId{1};
        r.draft_model = mk(); r.target_model = mk();
        r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model; r.pair_key.protocol_version = 1;
        r.policy.max_depth = wl.depth; r.policy.max_branches = wl.branches; r.policy.adaptive_depth_enabled = false;
        (void)f.submit(r);
        auto c = f.run_cycle(r.id);
        if (c.has_value()) {
            ++cycles;
            accepted += c.value().accepted_tokens;
            rejected += c.value().rejected_tokens;
            proposed += c.value().proposed_tokens;
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();

    const double cycles_s = cycles / secs;
    const double accepted_s = (double)accepted / secs;
    const double rejected_s = (double)rejected / secs;
    const double proposed_s = (double)proposed / secs;
    std::printf("measured:\n  elapsed=%.6fs\n  cycles=%llu\n  cycles/s=%.2f\n  proposals/s=%.2f\n  verifications/s=%.2f\n  accepted_tokens/s=%.2f\n  rejected_tokens/s=%.2f\n  end_to_end_accepted_tokens/s=%.2f\n",
                secs, (unsigned long long)cycles, cycles_s, proposed_s, cycles_s,
                accepted_s, rejected_s, accepted_s);
    std::printf("load_origin: measured\n");
    return 0;
}