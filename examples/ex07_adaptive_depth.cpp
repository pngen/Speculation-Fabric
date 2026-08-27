// Example 7: adaptive proposal depth varies with recent acceptance history.
#include "examples_support.hpp"
using namespace speculation_fabric;
int main() {
    CpuProposerConfig pc; pc.aligned_tokens = 5;
    auto p = std::make_shared<CpuProposerExecutor>(pc);
    auto v = std::make_shared<CpuVerifierExecutor>();
    SpeculationFabric::Config cfg; cfg.proposer = p; cfg.verifier = v;
    cfg.policy.max_depth = 8; cfg.policy.max_branches = 1;
    cfg.policy.adaptive_depth_enabled = true; cfg.policy.adaptive_window = 8;
    SpeculationFabric f(cfg);
    auto r = sfex::request(7, 5, 1);
    (void)f.submit(r);
    // A good draft (aligned=5, depth grows/shrinks deterministically).
    for (int i = 0; i < 5; ++i) (void)f.run_cycle(r.id);
    auto st = f.stats();
    std::printf("example7: cycles=%llu tokens_proposed=%llu tokens_accepted=%llu ratio=%.3f\n",
                (unsigned long long)st.speculative_cycles_started,
                (unsigned long long)st.tokens_proposed,
                (unsigned long long)st.tokens_accepted, st.acceptance_ratio);
    return st.acceptance_ratio > 0.0 ? 0 : 1;
}