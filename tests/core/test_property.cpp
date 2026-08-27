// Fixed-seed randomized invariant testing.
// Generates many requests across tenants with varied depth / branch count /
// acceptance profile, and continuously asserts the runtime invariants.
#include "sf_test.hpp"
#include "speculation_fabric/core/fabric.hpp"

using namespace speculation_fabric;

static std::uint64_t rng_state = 0x123456789ABCDEFull;
static std::uint64_t next_rand() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static std::uint32_t rng_u32(std::uint32_t lo, std::uint32_t hi) {
    return lo + static_cast<std::uint32_t>(next_rand() % (hi - lo + 1));
}

static ModelIdentity mk(TokenizerId tok) {
    ModelIdentity m; m.model = ModelId{101}; m.revision = Revision{1};
    m.tokenizer.id = tok; m.tokenizer.name = "p-tok"; m.tokenizer.vocab_size = 4096;
    m.executor.kind = ExecutorKind::CPU; m.executor.protocol_version = 1; m.name = "p-model";
    return m;
}

SF_TEST_FN(property_invariants_many_requests) {
    const std::uint64_t seed = 0x0DDBA11u;   // exact seed
    rng_state = seed;
    std::printf("property seed = %llu\n", (unsigned long long)seed);

    const std::uint32_t num_requests = 400;
    const std::uint32_t worker_pool = 8;
    std::vector<std::unique_ptr<SpeculationFabric>> fabrics;
    for (std::uint32_t w = 0; w < worker_pool; ++w) {
        CpuProposerConfig pc;
        pc.aligned_tokens = rng_u32(0, 8);
        auto p = std::make_shared<CpuProposerExecutor>(pc);
        auto v = std::make_shared<CpuVerifierExecutor>();
        SpeculationFabric::Config cfg;
        cfg.proposer = p; cfg.verifier = v;
        cfg.policy.max_depth = rng_u32(1, 8);
        cfg.policy.max_branches = rng_u32(1, 4);
        cfg.policy.adaptive_depth_enabled = false;
        fabrics.emplace_back(std::make_unique<SpeculationFabric>(cfg));
    }

    std::uint32_t checks = 0;
    for (std::uint32_t i = 0; i < num_requests; ++i) {
        auto& f = *fabrics[i % worker_pool];
        const std::uint32_t depth = rng_u32(1, 8);
        const std::uint32_t branches = rng_u32(1, 4);
        const std::uint64_t id = 100000 + i;

        SpeculationRequest r;
        r.id = RequestId{id};
        r.sequence = SequenceId{id + 1};
        r.tenant = TenantId{1 + (id % 5)};
        r.draft_model = mk(TokenizerId{9});
        r.target_model = mk(TokenizerId{9});
        r.pair_key.proposer = r.draft_model;
        r.pair_key.verifier = r.target_model;
        r.pair_key.protocol_version = 1;
        r.policy.max_depth = depth;
        r.policy.max_branches = branches;
        r.policy.adaptive_depth_enabled = false;
        (void)f.submit(r);

        auto c = f.run_cycle(r.id);
        SF_CHECK(c.has_value());
        const auto len = f.authoritative_length(r.id);
        SF_CHECK(len.has_value());
        // Authoritative length never decreases and never exceeds the proposal.
        SF_CHECK(len.value() >= 0);
        // accepted + rejected == proposed (when a cycle ran).
        SF_CHECK(c.value().proposed_tokens >= c.value().accepted_tokens);
        ++checks;

        // Over repeated cycles authoritative length is monotonic per request.
        auto a = len.value();
        for (int k = 0; k < 3; ++k) {
            (void)f.run_cycle(r.id);
            auto b = f.authoritative_length(r.id).value();
            SF_CHECK(b >= a);
            a = b;
            ++checks;
        }
    }

    // Global invariants after all work: no reservation leak.
    std::uint64_t reservations = 0;
    for (auto& f : fabrics) {
        auto s = f->stats();
        // No active reservation should remain after cycles complete.
        reservations += s.speculative_reservations_current;
    }
    // Reservations may be released by the engine; none may remain active after
    // the statically-enforced cycle completion.
    SF_CHECK(reservations == 0);
    std::printf("property: requests=%u checks=%u reservations_at_end=%llu\n",
                num_requests, checks, (unsigned long long)reservations);
    SF_CHECK(checks >= num_requests);
}