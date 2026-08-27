// Speculation Fabric proposer/verifier worker OS process.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Runs as a real OS process, connects to the coordinator over framed TCP,
// announces its role, and serves proposal/verification work using the CPU
// executors.

#include <cstdio>
#include <process.h>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "sockutil.h"
#include "distmsg.h"
#include "speculation_fabric/core/cpu_proposer.hpp"
#include "speculation_fabric/core/cpu_verifier.hpp"

using namespace speculation_fabric;
namespace d = spec_fabric::dist;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: %s <host> <port> <proposer|verifier>\n", argv[0]);
        return 1;
    }
    const std::string host = argv[1];
    const std::uint16_t port = (std::uint16_t)std::atoi(argv[2]);
    const std::string role = argv[3];
    sfsock::init();

    std::string err;
    int fd = sfsock::connect_port(host, port, err);
    if (fd < 0) { std::printf("worker: connect failed: %s\n", err.c_str()); return 1; }

    d::Message hello;
    hello.type = d::Msg::Hello;
    const std::uint64_t boot_id = (std::uint64_t)_getpid();
    hello.u64_fields = {boot_id};
    hello.u32_fields = {role == "proposer" ? d::kRoleProposer : d::kRoleVerifier};
    auto enc = d::encode(hello);
    sfsock::send_frame(fd, enc.value());

    std::vector<std::uint8_t> frame;
    while (sfsock::recv_frame(fd, frame)) {
        auto msg = d::decode(frame);
        if (msg.is_error()) continue;
        auto m = msg.value();
        if (m.type == d::Msg::Shutdown) break;
        if (m.type == d::Msg::ProposalDispatch) {
            CpuProposerConfig pc;
            pc.aligned_tokens = m.u32_fields[d::PD_ALIGNED];
            CpuProposerExecutor prop(pc);
            ProposalInput in;
            in.base_state.id = StateId{m.u64_fields[d::PD_BASE_STATE_ID]};
            in.base_state.generation = StateGeneration{m.u64_fields[d::PD_BASE_STATE_GEN]};
            in.base_generation = AuthGeneration{m.u64_fields[d::PD_BASE_GEN]};
            in.depth = m.u32_fields[d::PD_DEPTH];
            in.branch_index = (std::uint32_t)m.u64_fields[d::PD_BRANCH];
            in.branch_count = m.u32_fields[d::PD_BRANCHCOUNT];
            in.deterministic_seed = m.u64_fields[d::PD_DISPATCH] ^ m.u64_fields[d::PD_ATTEMPT];
            in.provenance.epoch = CoordinatorEpoch{m.u64_fields[d::PD_EPOCH]};
            in.provenance.request = RequestId{m.u64_fields[d::PD_REQUEST]};
            in.provenance.sequence = SequenceId{m.u64_fields[d::PD_SEQUENCE]};
            in.provenance.attempt = AttemptId{m.u64_fields[d::PD_ATTEMPT]};
            in.provenance.branch = BranchId{m.u64_fields[d::PD_BRANCH] + 1};
            in.provenance.proposer_model.revision = Revision{m.u64_fields[d::PD_PROPOSER_REV]};
            in.provenance.proposer_boot = WorkerBootId{m.u64_fields[d::PD_PROPOSER_BOOT]};
            in.provenance.base_generation = AuthGeneration{m.u64_fields[d::PD_BASE_GEN]};
            in.provenance.dispatch = DispatchId{m.u64_fields[d::PD_DISPATCH]};
            in.provenance.proposer_worker = WorkerId{};
            auto r = prop.propose(in);
            d::Message out;
            out.type = d::Msg::ProposalResult;
            out.u64_fields = {m.u64_fields[d::PD_REQUEST], m.u64_fields[d::PD_SEQUENCE],
                              m.u64_fields[d::PD_ATTEMPT], m.u64_fields[d::PD_EPOCH],
                              m.u64_fields[d::PD_DISPATCH], m.u64_fields[d::PD_BRANCH],
                              m.u64_fields[d::PD_BASE_GEN], m.u64_fields[d::PD_PROPOSER_BOOT],
                              m.u64_fields[d::PD_PROPOSER_REV],
                              m.u64_fields[d::PD_PROPOGEN],
                              r.value().execution_measured_ns};
            out.u32_fields = {};
            out.u32_fields.push_back(r.value().outcome == ProposalOutcome::Success ? d::kOutcomeSuccess
                            : (r.value().retryable() ? d::kOutcomeRetryable : d::kOutcomeNonRetryable));
            out.u32_fields.push_back(in.depth);
            if (r.value().candidate) {
                for (const auto& t : r.value().candidate->tokens) out.u32_fields.push_back(t.id);
            } else {
                for (std::uint32_t i = 0; i < in.depth; ++i) out.u32_fields.push_back(0);
            }
            auto oe = d::encode(out);
            sfsock::send_frame(fd, oe.value());
        } else if (m.type == d::Msg::VerificationDispatch) {
            CpuVerifierExecutor ver;
            VerificationInput v;
            v.authoritative_state.id = StateId{m.u64_fields[d::VD_STATE_ID]};
            v.authoritative_state.generation = StateGeneration{m.u64_fields[d::VD_STATE_GEN]};
            v.authoritative_generation = AuthGeneration{m.u64_fields[d::VD_BASE_GEN]};
            v.epoch = CoordinatorEpoch{m.u64_fields[d::VD_EPOCH]};
            v.request = RequestId{m.u64_fields[d::VD_REQUEST]};
            v.sequence = SequenceId{m.u64_fields[d::VD_SEQUENCE]};
            v.attempt = AttemptId{m.u64_fields[d::VD_ATTEMPT]};
            v.dispatch = DispatchId{m.u64_fields[d::VD_DISPATCH]};
            v.verifier_worker = WorkerId{};
            v.verifier_boot = WorkerBootId{m.u64_fields[d::VD_PROPOSER_BOOT]};
            v.authoritative_state_generation = StateGeneration{m.u64_fields[d::VD_STATE_GEN]};
            v.proposer.revision = Revision{m.u64_fields[d::VD_PROPOSER_REV]};
            v.candidate_identity.tokenizer = TokenizerId{m.u32_fields[d::VD_TOKENIZER]};
            v.candidate_identity.vocab_size = m.u32_fields[d::VD_VOCAB];
            const std::uint32_t len = m.u32_fields[d::VD_LEN];
            for (std::uint32_t i = 0; i < len; ++i) {
                v.candidate.tokens.push_back(Token{m.u32_fields[d::VD_TOKENS + i]});
            }
            auto r = ver.verify(v);
            d::Message out;
            out.type = d::Msg::VerificationResult;
            out.u64_fields = {m.u64_fields[d::VD_REQUEST], m.u64_fields[d::VD_SEQUENCE],
                              m.u64_fields[d::VD_ATTEMPT], m.u64_fields[d::VD_EPOCH],
                              m.u64_fields[d::VD_BASE_GEN], m.u64_fields[d::VD_DISPATCH],
                              m.u64_fields[d::VD_STATE_ID], m.u64_fields[d::VD_STATE_GEN]};
            std::uint32_t code = d::kAccFailure;
            if (r.has_value()) {
                if (r.value().outcome == AcceptanceOutcome::FullAccept) code = d::kAccFull;
                else if (r.value().outcome == AcceptanceOutcome::PartialAccept) code = d::kAccPartial;
                else if (r.value().outcome == AcceptanceOutcome::RejectAll) code = d::kAccReject;
            }
            out.u32_fields = {code, r.has_value() ? r.value().accepted_prefix : 0,
                              len, r.has_value() ? r.value().first_rejection_index : len};
            auto oe = d::encode(out);
            sfsock::send_frame(fd, oe.value());
        }
    }
    sfsock::cleanup();
    return 0;
}