// Example 11: CUDA proposer/verifier speculation (built only when CUDA enabled).
#include "examples_support.hpp"
using namespace speculation_fabric;
#include "speculation_fabric/core/cuda_executor.hpp"
int main() {
    auto p = std::make_shared<CudaProposerExecutor>(5u, 0u);   // aligned=5 => full accept
    auto v = std::make_shared<CudaVerifierExecutor>();
    SpeculationFabric::Config cfg; cfg.proposer = p; cfg.verifier = v;
    cfg.policy.max_depth = 5; cfg.policy.max_branches = 1;
    cfg.policy.adaptive_depth_enabled = false;
    SpeculationFabric f(cfg);
    auto r = sfex::request(11, 5, 1);
    (void)f.submit(r);
    auto c = f.run_cycle(r.id);
    std::printf("example11: outcome=%d accepted=%u committed=%llu\n",
                (int)c.value().outcome, c.value().accepted_tokens,
                (unsigned long long)f.authoritative_length(r.id).value());
    return c.value().accepted_tokens == 5 ? 0 : 1;
}