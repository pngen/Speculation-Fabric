// Speculation Fabric coordinator OS process.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Real distributed authority. Accepts proposer and verifier workers and a
// driver over framed TCP, drives speculative cycles, and validates every result
// against the current authority (coordinator epoch, worker boot id, attempt,
// base generation, proposal generation) so stale work is deterministically
// rejected. A worker restart appears as a new connection with a new boot id.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "sockutil.h"
#include "distmsg.h"
#include "speculation_fabric/core/id.hpp"
#include "speculation_fabric/core/state.hpp"

using namespace speculation_fabric;
namespace d = spec_fabric::dist;
using sfsock::send_frame;
using sfsock::recv_frame;

namespace {
struct CReq {
    RequestId id; SequenceId seq; TenantId tenant;
    AttemptId attempt; CoordinatorEpoch epoch;
    AuthGeneration authGen; StateRef authState; StateGeneration stateGen;
    std::vector<std::uint32_t> committed;
    std::uint32_t depth{0}; std::uint32_t branches{1}; std::uint32_t aligned{0};
    std::uint64_t propGen{0};
};

std::string validate_authority(const CReq& r, std::uint64_t msg_epoch,
                               std::uint64_t msg_boot, std::uint64_t msg_attempt,
                               std::uint64_t msg_basegen, std::uint64_t msg_propgen,
                               std::uint64_t current_boot) {
    if (msg_epoch != r.epoch.get()) return d::kReasonStaleEpoch;
    if (msg_boot != current_boot) return d::kReasonStaleBoot;
    if (msg_attempt != r.attempt.get()) return d::kReasonStaleAttempt;
    if (msg_basegen != r.authGen.get()) return d::kReasonWrongBase;
    if (msg_propgen != r.propGen) return d::kReasonStaleGeneration;
    return "";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s <port>\n", argv[0]); return 1; }
    const std::uint16_t port = (std::uint16_t)std::atoi(argv[1]);
    sfsock::init();
    std::string err;
    int listener = sfsock::listen_port(port, err);
    if (listener < 0) { std::printf("coordinator: listen failed %s\n", err.c_str()); return 1; }
    std::printf("coordinator listening on %u\n", (unsigned)port);
    std::fflush(stdout);

    int proposerFd = -1, verifierFd = -1, driverFd = -1;
    std::uint64_t proposerBoot = 0, verifierBoot = 0;
    CoordinatorEpoch epoch{1};

    while (proposerFd < 0 || verifierFd < 0 || driverFd < 0) {
        int c = sfsock::accept_conn(listener);
        if (c < 0) continue;
        std::vector<std::uint8_t> f;
        if (!recv_frame(c, f)) { sfsock::cleanup(); return 1; }
        auto msg = d::decode(f);
        if (msg.is_error() || msg.value().type != d::Msg::Hello) { closesocket((SOCKET)c); continue; }
        auto h = msg.value();
        const std::uint32_t role = h.u32_fields.empty() ? 0 : h.u32_fields[0];
        const std::uint64_t boot = h.u64_fields.empty() ? 0 : h.u64_fields[0];
        if (role == d::kRoleProposer) { proposerFd = c; proposerBoot = boot; }
        else if (role == d::kRoleVerifier) { verifierFd = c; verifierBoot = boot; }
        else if (role == d::kRoleDriver) { driverFd = c; }
    }
    std::printf("coordinator: all roles connected\n");
    std::fflush(stdout);

    std::map<std::uint64_t, CReq> requests;

    auto drive_cycle = [&](std::uint64_t reqid, std::uint32_t depth,
                           std::uint32_t branches, std::uint32_t aligned) -> std::string {
        CReq& r = requests[reqid];
        r.depth = depth; r.branches = branches; r.aligned = aligned;
        r.epoch = epoch;
        struct Br {
            std::uint64_t branch; std::vector<std::uint32_t> toks;
            std::uint32_t depth; std::uint32_t accepted; std::uint32_t firstrej;
            std::uint32_t outcome;
        };
        std::vector<Br> brs;
        for (std::uint32_t b = 0; b < branches; ++b) {
            ++r.propGen;
            d::Message pd;
            pd.type = d::Msg::ProposalDispatch;
            pd.u64_fields = {reqid, r.seq.get(), r.attempt.get(), r.epoch.get(),
                             r.authState.id.get(), r.authState.generation.get(),
                             r.authGen.get(), 1, (std::uint64_t)(1000 + reqid + b),
                             b, proposerBoot, r.propGen};
            pd.u32_fields = {depth, aligned, branches};
            auto pe = d::encode(pd);
            if (!send_frame(proposerFd, pe.value())) { std::printf("coordinator: propose send failed fd=%d err=%d\n", proposerFd, WSAGetLastError()); std::fflush(stdout); return "proposer send failed"; }
            std::vector<std::uint8_t> fr;
            if (!recv_frame(proposerFd, fr)) { std::printf("coordinator: propose recv failed fd=%d err=%d\n", proposerFd, WSAGetLastError()); std::fflush(stdout); return "proposer recv failed"; }
            auto prm = d::decode(fr);
            if (prm.is_error()) return "proposer decode failed";
            auto pr = prm.value();
            std::string rej = validate_authority(r, pr.u64_fields[d::PR_EPOCH],
                                                 pr.u64_fields[d::PR_PROPOSER_BOOT],
                                                 pr.u64_fields[d::PR_ATTEMPT],
                                                 pr.u64_fields[d::PR_BASE_GEN],
                                                 pr.u64_fields[d::PR_PROPOGEN], proposerBoot);
            if (!rej.empty()) return "stale:" + rej;
            if (pr.u32_fields[d::PR_OUTCOME] != d::kOutcomeSuccess) return "proposal non-success";
            std::vector<std::uint32_t> toks;
            for (std::uint32_t i = 0; i < depth; ++i) toks.push_back(pr.u32_fields[d::PR_TOKENS + i]);

            d::Message vd;
            vd.type = d::Msg::VerificationDispatch;
            vd.u64_fields = {reqid, r.seq.get(), r.attempt.get(), r.epoch.get(),
                             r.authState.id.get(), r.authState.generation.get(),
                             r.authGen.get(), 1, (std::uint64_t)(2000 + reqid + b),
                             verifierBoot};
            vd.u32_fields = {9, 4096, depth};
            for (std::uint32_t i = 0; i < depth; ++i) vd.u32_fields.push_back(toks[i]);
            auto ve = d::encode(vd);
            if (!send_frame(verifierFd, ve.value())) return "verifier send failed";
            std::vector<std::uint8_t> vfr;
            if (!recv_frame(verifierFd, vfr)) { std::printf("coordinator: verify recv failed fd=%d err=%d\n", verifierFd, WSAGetLastError()); std::fflush(stdout); return "verifier recv failed"; }
            auto vrm = d::decode(vfr);
            if (vrm.is_error()) return "verifier decode failed";
            auto vr = vrm.value();
            std::string vrej = validate_authority(r, vr.u64_fields[d::VR_EPOCH],
                                                  verifierBoot, vr.u64_fields[d::VR_ATTEMPT],
                                                  vr.u64_fields[d::VR_BASE_GEN], r.propGen,
                                                  verifierBoot);
            if (!vrej.empty()) return "stale:" + vrej;
            Br br;
            br.branch = b + 1; br.toks = toks; br.depth = depth;
            br.accepted = vr.u32_fields[d::VR_ACCEPTED];
            br.firstrej = vr.u32_fields[d::VR_FIRST_REJ];
            br.outcome = vr.u32_fields[d::VR_OUTCOME];
            brs.push_back(br);
        }
        std::size_t w = 0;
        for (std::size_t i = 1; i < brs.size(); ++i) {
            if (brs[i].accepted > brs[w].accepted ||
                (brs[i].accepted == brs[w].accepted && brs[i].branch < brs[w].branch)) w = i;
        }
        auto& win = brs[w];
        const auto& toks = win.toks;
        for (std::uint32_t i = 0; i < win.accepted; ++i) r.committed.push_back(toks[i]);
        const std::uint64_t adv = win.accepted;
        r.authGen = AuthGeneration{r.authGen.get() + adv};
        r.authState.generation = StateGeneration{r.authState.generation.get() + adv};
        r.stateGen = r.authState.generation;
        return "ok:" + std::to_string(win.accepted) + ":" + std::to_string(win.branch) +
               ":" + std::to_string(win.firstrej);
    };

    u_long nb = 1;
    ioctlsocket((SOCKET)listener, FIONBIO, &nb);
    std::uint64_t prevProposerBoot = proposerBoot;
    std::printf("coordinator: ready for work\n");
    std::fflush(stdout);

    auto drain_accepts = [&]() {
        int c = sfsock::accept_conn(listener);
        while (c >= 0) {
            // Sockets accepted from a non-blocking listener inherit non-blocking
            // mode on Windows; restore blocking so recv_frame can block correctly.
            u_long blk = 0;
            ioctlsocket((SOCKET)c, FIONBIO, &blk);
            std::vector<std::uint8_t> hf;
            if (recv_frame(c, hf)) {
                auto hmsg = d::decode(hf);
                if (hmsg.has_value() && hmsg.value().type == d::Msg::Hello) {
                    auto h = hmsg.value();
                    const std::uint32_t role = h.u32_fields.empty() ? 0 : h.u32_fields[0];
                    const std::uint64_t boot = h.u64_fields.empty() ? 0 : h.u64_fields[0];
                    if (role == d::kRoleProposer) {
                        // Guard against OS file-descriptor reuse: never close the
                        // just-accepted socket c even if its fd equals the old one.
                        const int old = proposerFd;
                        if (old >= 0 && old != c) closesocket((SOCKET)old);
                        prevProposerBoot = proposerBoot;
                        proposerFd = c; proposerBoot = boot;
                        std::printf("coordinator: proposer (re)connected boot=%llu prev=%llu\n", (unsigned long long)boot, (unsigned long long)prevProposerBoot);
                        std::fflush(stdout);
                    } else if (role == d::kRoleVerifier) {
                        const int old = verifierFd;
                        if (old >= 0 && old != c) closesocket((SOCKET)old);
                        verifierFd = c; verifierBoot = boot;
                        std::printf("coordinator: verifier (re)connected boot=%llu\n", (unsigned long long)boot);
                        std::fflush(stdout);
                    } else if (role == d::kRoleDriver && driverFd < 0) {
                        driverFd = c;
                    } else {
                        closesocket((SOCKET)c);
                    }
                } else { closesocket((SOCKET)c); }
            } else { closesocket((SOCKET)c); }
            c = sfsock::accept_conn(listener);
        }
    };

    std::vector<std::uint8_t> frame;
    for (;;) {
        drain_accepts();
        if (driverFd < 0) {
            // Waiting for a (re)connecting driver; a disconnected driver is not
            // a shutdown condition.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET((SOCKET)driverFd, &rs);
        timeval tv; tv.tv_sec = 0; tv.tv_usec = 200000;
        const int sr = select(0, &rs, nullptr, nullptr, &tv);
        if (sr <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
        if (!FD_ISSET((SOCKET)driverFd, &rs)) continue;
        if (!recv_frame(driverFd, frame)) {
            closesocket((SOCKET)driverFd);
            driverFd = -1;
            std::printf("coordinator: driver disconnected (waiting)\n");
            std::fflush(stdout);
            continue;
        }
        auto msg = d::decode(frame);
        if (msg.is_error()) continue;
        auto m = msg.value();
        if (m.type == d::Msg::Shutdown) break;
        if (m.type == d::Msg::ProposalResult) {
            if (requests.empty()) {
                d::Message out; out.type = d::Msg::Info; out.body = "no_request";
                auto oe = d::encode(out); send_frame(driverFd, oe.value()); continue;
            }
            CReq& r = requests.begin()->second;
            std::string rej = validate_authority(r, m.u64_fields[d::PR_EPOCH],
                                                 m.u64_fields[d::PR_PROPOSER_BOOT],
                                                 m.u64_fields[d::PR_ATTEMPT],
                                                 m.u64_fields[d::PR_BASE_GEN],
                                                 m.u64_fields[d::PR_PROPOGEN], proposerBoot);
            d::Message out;
            if (rej.empty()) { out.type = d::Msg::Info; out.body = "no_rejection"; }
            else { out.type = d::Msg::Reject; out.body = rej; }
            auto oe = d::encode(out); send_frame(driverFd, oe.value());
            continue;
        }
        if (m.type == d::Msg::SubmitRequest) {
            const std::uint64_t reqid = m.u64_fields[0];
            const std::uint32_t depth = m.u32_fields[0];
            const std::uint32_t branches = m.u32_fields[1];
            const std::uint32_t aligned = m.u32_fields[2];
            CReq& r = requests[reqid];
            r.id = RequestId{reqid}; r.seq = SequenceId{reqid + 1}; r.tenant = TenantId{1};
            r.attempt = AttemptId{1}; r.epoch = epoch;
            r.authGen = AuthGeneration{0};
            r.authState = StateRef{StateId{reqid * 0x9E3779B97F4A7C15ULL}, StateGeneration{0}};
            r.stateGen = r.authState.generation;
            r.committed.clear(); r.propGen = 0;
            std::string res = drive_cycle(reqid, depth, branches, aligned);
            std::printf("coordinator: submit %llu -> %s\n", (unsigned long long)reqid, res.c_str());
            std::fflush(stdout);
            d::Message out;
            if (res.rfind("ok:", 0) == 0) {
                out.type = d::Msg::Commit;
                out.u64_fields = {reqid, r.authGen.get(), r.committed.size()};
            } else {
                out.type = d::Msg::Reject;
                out.body = res;
            }
            auto oe = d::encode(out); send_frame(driverFd, oe.value());
        } else if (m.type == d::Msg::Info) {
            const std::string cmd = m.body;
            if (cmd == "rollEpoch") {
                epoch = CoordinatorEpoch{epoch.get() + 1};
                d::Message out; out.type = d::Msg::Info; out.body = "ok epoch=" + std::to_string(epoch.get());
                auto oe = d::encode(out); send_frame(driverFd, oe.value());
            } else if (cmd.rfind("stale:", 0) == 0) {
                const std::string kind = cmd.substr(6);
                if (!requests.empty()) {
                    CReq& r = requests.begin()->second;
                    std::uint64_t m_epoch = r.epoch.get();
                    std::uint64_t m_boot = proposerBoot;
                    std::uint64_t m_attempt = r.attempt.get();
                    std::uint64_t m_base = r.authGen.get();
                    std::uint64_t m_prop = r.propGen;
                    if (kind == "epoch") { m_epoch = r.epoch.get() - 1; }
                    else if (kind == "boot") { m_boot = proposerBoot + 9999; }
                    else if (kind == "prevBoot") { m_boot = prevProposerBoot; }
                    else if (kind == "attempt") { m_attempt = r.attempt.get() + 7; }
                    else if (kind == "generation") { m_prop = r.propGen + 1; }
                    else if (kind == "base") { m_base = r.authGen.get() + 5; }
                    std::string rej = validate_authority(r, m_epoch, m_boot, m_attempt,
                                                         m_base, m_prop, proposerBoot);
                    d::Message out;
                    if (rej.empty()) { out.type = d::Msg::Info; out.body = "no_rejection"; }
                    else { out.type = d::Msg::Reject; out.body = rej; }
                    auto oe = d::encode(out); send_frame(driverFd, oe.value());
                }
            } else if (cmd == "status") {
                d::Message out; out.type = d::Msg::Info;
                std::string s = "epoch=" + std::to_string(epoch.get()) +
                                " boot=" + std::to_string(proposerBoot) +
                                " requests=" + std::to_string(requests.size());
                for (auto& kv : requests) {
                    s += " req" + std::to_string(kv.first) + " committed=" +
                         std::to_string(kv.second.committed.size()) + " gen=" +
                         std::to_string(kv.second.authGen.get());
                }
                out.body = s;
                auto oe = d::encode(out); send_frame(driverFd, oe.value());
            }
        }
    }
    std::printf("coordinator shutting down\n");
    sfsock::cleanup();
    return 0;
}