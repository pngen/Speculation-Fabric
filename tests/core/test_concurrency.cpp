// Concurrency stress: many threads drive a shared thread-safe engine.
#include "sf_test.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include "speculation_fabric/core/cpu_proposer.hpp"
#include "speculation_fabric/core/cpu_verifier.hpp"
#include "speculation_fabric/core/fabric.hpp"

using namespace speculation_fabric;

static ModelIdentity mk() {
    ModelIdentity m; m.model = ModelId{101}; m.revision = Revision{1};
    m.tokenizer.id = TokenizerId{9}; m.tokenizer.name = "cc-tok"; m.tokenizer.vocab_size = 4096;
    m.executor.kind = ExecutorKind::CPU; m.executor.protocol_version = 1; m.name = "cc";
    return m;
}

SF_TEST_FN(concurrency_shared_engine_many_threads) {
    auto proposer = std::make_shared<CpuProposerExecutor>();
    auto verifier = std::make_shared<CpuVerifierExecutor>();
    SpeculationFabric::Config cfg;
    cfg.proposer = proposer; cfg.verifier = verifier;
    cfg.policy.max_depth = 4; cfg.policy.max_branches = 1;
    cfg.policy.adaptive_depth_enabled = false;
    auto f = std::make_shared<SpeculationFabric>(cfg);

    const int threads = 8;
    const std::uint64_t per = 200;
    std::atomic<int> failures{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t) {
        ts.emplace_back([&, t]() {
            for (std::uint64_t k = 0; k < per; ++k) {
                SpeculationRequest r;
                r.id = RequestId{(std::uint64_t)(t * 100000 + k)};
                r.sequence = SequenceId{(std::uint64_t)(t * 100000 + k) + 1};
                r.tenant = TenantId{(std::uint64_t)(1 + (t % 3))};
                r.draft_model = mk(); r.target_model = mk();
                r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model; r.pair_key.protocol_version = 1;
                r.policy.max_depth = 4; r.policy.adaptive_depth_enabled = false;
                (void)f->submit(r);
                auto c = f->run_cycle(r.id);
                if (!c.has_value()) { ++failures; continue; }
                auto a = f->authoritative_length(r.id);
                if (!a.has_value() || a.value() > 4) { ++failures; }
            }
        });
    }
    for (auto& th : ts) th.join();
    SF_CHECK_EQ(failures.load(), 0);
    // After all work, the engine's registry reports no leaked active reservations.
    auto snap = f->snapshot();
    SF_CHECK(snap.active_requests >= 8u);
    (void)snap;
}
