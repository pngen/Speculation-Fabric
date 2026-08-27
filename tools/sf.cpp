// Speculation Fabric command-line tool.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "speculation_fabric/core/fabric.hpp"
#include "speculation_fabric/core/cpu_proposer.hpp"
#include "speculation_fabric/core/cpu_verifier.hpp"
#include "speculation_fabric/core/persistence.hpp"

using namespace speculation_fabric;

static ModelIdentity make_model(TokenizerId tok) {
    ModelIdentity m;
    m.model = ModelId{101};
    m.revision = Revision{1};
    m.tokenizer.id = tok;
    m.tokenizer.name = "cli-tok";
    m.tokenizer.vocab_size = 4096;
    m.executor.kind = ExecutorKind::CPU;
    m.executor.protocol_version = 1;
    m.name = "cli-model";
    return m;
}

static SpeculationFabric make_engine(std::uint32_t depth, std::uint32_t branches,
                                     std::uint32_t aligned) {
    CpuProposerConfig pc;
    pc.aligned_tokens = aligned;
    auto proposer = std::make_shared<CpuProposerExecutor>(pc);
    auto verifier = std::make_shared<CpuVerifierExecutor>();
    SpeculationFabric::Config cfg;
    cfg.proposer = proposer;
    cfg.verifier = verifier;
    cfg.policy.max_depth = depth;
    cfg.policy.max_branches = branches;
    cfg.policy.adaptive_depth_enabled = false;
    return SpeculationFabric(cfg);
}

static int cmd_submit(std::uint32_t depth, std::uint32_t branches, std::uint32_t aligned,
                      std::uint64_t id) {
    auto f = make_engine(depth, branches, aligned);
    SpeculationRequest r;
    r.id = RequestId{id};
    r.sequence = SequenceId{id + 1};
    r.tenant = TenantId{1};
    r.draft_model = make_model(TokenizerId{9});
    r.target_model = r.draft_model;
    r.pair_key.proposer = r.draft_model;
    r.pair_key.verifier = r.target_model;
    r.pair_key.protocol_version = 1;
    r.policy.max_depth = depth;
    r.policy.max_branches = branches;
    r.policy.adaptive_depth_enabled = false;
    auto s = f.submit(r);
    if (s.is_error()) { std::printf("submit failed: %d\n", (int)s.error_code()); return 1; }
    auto c = f.run_cycle(r.id);
    if (c.is_error()) { std::printf("run_cycle failed: %d\n", (int)c.error_code()); return 1; }
    auto o = c.value();
    std::printf("request=%llu outcome=%d proposed=%u accepted=%u rejected=%u committed=%llu phase=%s\n",
                (unsigned long long)id, (int)o.outcome, o.proposed_tokens,
                o.accepted_tokens, o.rejected_tokens,
                (unsigned long long)f.authoritative_length(r.id).value(),
                to_string(o.phase));
    return 0;
}

static int cmd_status(std::uint32_t depth, std::uint32_t branches, std::uint32_t aligned) {
    auto f = make_engine(depth, branches, aligned);
    SpeculationRequest r;
    r.id = RequestId{1};
    r.sequence = SequenceId{2};
    r.tenant = TenantId{1};
    r.draft_model = make_model(TokenizerId{9});
    r.target_model = r.draft_model;
    r.pair_key.proposer = r.draft_model;
    r.pair_key.verifier = r.target_model;
    r.pair_key.protocol_version = 1;
    r.policy.max_depth = depth;
    r.policy.max_branches = branches;
    r.policy.adaptive_depth_enabled = false;
    (void)(void)f.submit(r);
    for (int i = 0; i < 3; ++i) { (void)f.run_cycle(r.id); }
    auto snap = f.snapshot();
    auto st = f.stats();
    std::printf("epoch=%llu active_requests=%zu branches=%zu reservations=%zu cycles=%llu tokens_accepted=%llu tokens_proposed=%llu acceptance_ratio=%.3f\n",
                (unsigned long long)snap.epoch.get(), snap.active_requests,
                snap.active_branches, snap.active_reservations,
                (unsigned long long)st.speculative_cycles_started,
                (unsigned long long)st.tokens_accepted,
                (unsigned long long)st.tokens_proposed, st.acceptance_ratio);
    std::printf("auth_len=%llu\n", (unsigned long long)f.authoritative_length(r.id).value());
    return 0;
}

static int cmd_explain(std::uint32_t depth) {
    auto f = make_engine(depth, 1, depth);
    SpeculationRequest r;
    r.id = RequestId{1};
    r.sequence = SequenceId{2};
    r.tenant = TenantId{1};
    r.draft_model = make_model(TokenizerId{9});
    r.target_model = r.draft_model;
    r.pair_key.proposer = r.draft_model;
    r.pair_key.verifier = r.target_model;
    r.pair_key.protocol_version = 1;
    r.policy.max_depth = depth;
    r.policy.adaptive_depth_enabled = false;
    (void)(void)f.submit(r);
    (void)f.run_cycle(r.id);
    auto ex = f.explain(r.id, "why did this sequence speculate?");
    std::printf("%s\n", ex.answer_text.c_str());
    return 0;
}

static int cmd_bench(std::uint32_t depth, std::uint32_t requests) {
    auto f = make_engine(depth, 1, depth);
    // Real completed speculative work: submit + run a cycle per request.
    std::uint64_t committed = 0;
    std::uint64_t cycles = 0;
    for (std::uint64_t i = 0; i < requests; ++i) {
        SpeculationRequest r;
        r.id = RequestId{i + 1};
        r.sequence = SequenceId{i + 2};
        r.tenant = TenantId{1};
        r.draft_model = make_model(TokenizerId{9});
        r.target_model = r.draft_model;
        r.pair_key.proposer = r.draft_model;
        r.pair_key.verifier = r.target_model;
        r.pair_key.protocol_version = 1;
        r.policy.max_depth = depth;
        r.policy.adaptive_depth_enabled = false;
        (void)(void)f.submit(r);
        auto c = f.run_cycle(r.id);
        if (c.has_value()) {
            committed += c.value().accepted_tokens;
            ++cycles;
        }
    }
    std::printf("bench: depth=%u requests=%llu cycles=%llu accepted=%llu\n", depth,
                (unsigned long long)requests, (unsigned long long)cycles,
                (unsigned long long)committed);
    return 0;
}


static int cmd_inspect(std::uint32_t depth, std::uint32_t branches, std::uint32_t aligned, std::uint64_t id) {
    auto f = make_engine(depth, branches, aligned);
    SpeculationRequest r; r.id = RequestId{id}; r.sequence = SequenceId{id + 1}; r.tenant = TenantId{1};
    r.draft_model = make_model(TokenizerId{9}); r.target_model = r.draft_model;
    r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model; r.pair_key.protocol_version = 1;
    r.policy.max_depth = depth; r.policy.max_branches = branches; r.policy.adaptive_depth_enabled = false;
    (void)f.submit(r);
    for (int i = 0; i < 2; ++i) (void)f.run_cycle(r.id);
    auto pr = f.proposals(r.id); auto br = f.branches(r.id);
    auto ex = f.explain(r.id, "inspect");
    std::printf("inspect request=%llu\n  gen=%s\n  proposals=%zu branches=%zu\n  %s\n",
                (unsigned long long)id, ex.answer_json.c_str(), pr.value().size(),
                br.value().size(), ex.answer_text.c_str());
    return 0;
}

static int cmd_cancel(std::uint32_t depth, std::uint32_t branches, std::uint32_t aligned, std::uint64_t id) {
    auto f = make_engine(depth, branches, aligned);
    SpeculationRequest r; r.id = RequestId{id}; r.sequence = SequenceId{id + 1}; r.tenant = TenantId{1};
    r.draft_model = make_model(TokenizerId{9}); r.target_model = r.draft_model;
    r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model; r.pair_key.protocol_version = 1;
    r.policy.max_depth = depth; r.policy.max_branches = branches; r.policy.adaptive_depth_enabled = false;
    (void)f.submit(r);
    auto c = f.cancel(r.id, "cli cancel");
    auto o = f.run_cycle(r.id);
    std::printf("cancel %s: outcome=%d\n", c.has_value() ? "ok" : "fail", (int)o.value().outcome);
    return (c.has_value() && o.value().outcome == CycleOutcomeKind::Cancelled) ? 0 : 1;
}

static int cmd_recover(std::uint32_t depth) {
    auto f = make_engine(depth, 1, depth);
    SpeculationRequest r; r.id = RequestId{1}; r.sequence = SequenceId{2}; r.tenant = TenantId{1};
    r.draft_model = make_model(TokenizerId{9}); r.target_model = r.draft_model;
    r.pair_key.proposer = r.draft_model; r.pair_key.verifier = r.target_model; r.pair_key.protocol_version = 1;
    r.policy.max_depth = depth; r.policy.adaptive_depth_enabled = false;
    (void)f.submit(r);
    (void)f.run_cycle(r.id);
    StateArchive a; a.epoch = CoordinatorEpoch{1};
    PersistedRequest pr; pr.id = r.id; pr.sequence = r.sequence; pr.attempt = AttemptId{1};
    pr.epoch = CoordinatorEpoch{1}; pr.auth_gen = AuthGeneration{depth};
    pr.auth_state.id = StateId{77}; pr.auth_state.generation = StateGeneration{depth};
    pr.committed.assign(f.authoritative_length(r.id).value(), 1u);
    a.requests.push_back(pr);
    auto blob = serialize_archive(a);
    auto b = deserialize_archive(blob.value());
    std::printf("recover: ok=%d tokens=%zu\n", (int)b.has_value(), b.value().requests[0].committed.size());
    return b.has_value() ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Speculation Fabric CLI\n"
                    "usage: sf <command> [options]\n"
                    "commands: submit, status, explain, bench\n");
        return 0;
    }
    const std::string cmd = argv[1];
    std::uint32_t depth = 4, branches = 1, aligned = 4;
    std::uint64_t id = 1;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--depth") == 0 && i + 1 < argc) depth = (std::uint32_t)std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--branches") == 0 && i + 1 < argc) branches = (std::uint32_t)std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--aligned") == 0 && i + 1 < argc) aligned = (std::uint32_t)std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--id") == 0 && i + 1 < argc) id = (std::uint64_t)std::strtoull(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--requests") == 0 && i + 1 < argc) id = (std::uint64_t)std::strtoull(argv[++i], nullptr, 10);
    }
    if (cmd == "submit") return cmd_submit(depth, branches, aligned, id);
    if (cmd == "status") return cmd_status(depth, branches, aligned);
    if (cmd == "explain") return cmd_explain(depth);
    if (cmd == "bench") return cmd_bench(depth, (std::uint32_t)id);
    if (cmd == "inspect") return cmd_inspect(depth, branches, aligned, id);
    if (cmd == "cancel") return cmd_cancel(depth, branches, aligned, id);
    if (cmd == "recover") return cmd_recover(depth);
    std::printf("unknown command: %s\n", cmd.c_str());
    return 1;
}