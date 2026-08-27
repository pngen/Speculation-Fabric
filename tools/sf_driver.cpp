// Speculation Fabric distributed scenario driver (single persistent connection).
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
#include <chrono>
#include <winsock2.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <string>
#include <vector>
#include "sockutil.h"
#include "distmsg.h"

using namespace speculation_fabric;
namespace d = spec_fabric::dist;

static int failures = 0;
static void expect(bool cond, const char* what, const std::string& got) {
    std::printf("%s %s [%s]\n", cond ? "[PASS]" : "[FAIL]", what, got.c_str());
    std::fflush(stdout);
    if (!cond) ++failures;
}
static bool sendm(int fd, const d::Message& m) {
    auto e = d::encode(m); if (e.is_error()) return false;
    return sfsock::send_frame(fd, e.value());
}
static bool recvm(int fd, d::Message& out) {
    std::vector<std::uint8_t> f;
    if (!sfsock::recv_frame(fd, f)) return false;
    auto m = d::decode(f); if (m.is_error()) return false;
    out = m.value(); return true;
}
static bool file_exists(const std::string& p) {
    std::ifstream f(p); return f.good();
}

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: %s <host> <port> <tempdir>\n", argv[0]); return 1; }
    const std::string host = argv[1];
    const std::uint16_t port = (std::uint16_t)std::atoi(argv[2]);
    const std::string tmp = argv[3];
    sfsock::init();
    std::string err;
    int fd = sfsock::connect_port(host, port, err);
    if (fd < 0) { std::printf("driver: connect failed %s\n", err.c_str()); return 1; }
    d::Message hello; hello.type = d::Msg::Hello;
    hello.u64_fields = {12345}; hello.u32_fields = {d::kRoleDriver};
    sendm(fd, hello);
    { DWORD to = 8000; setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to)); }

    auto submit = [&](std::uint64_t reqid, std::uint32_t depth, std::uint32_t branches,
                      std::uint32_t aligned) -> std::uint64_t {
        d::Message s; s.type = d::Msg::SubmitRequest; s.u64_fields = {reqid};
        s.u32_fields = {depth, branches, aligned}; sendm(fd, s);
        d::Message r; recvm(fd, r);
        return r.type == d::Msg::Commit ? r.u64_fields[2] : 0xFFFFFFFFFFFFFFFFull;
    };

    const std::uint64_t a = submit(42, 5, 1, 5);
    expect(a == 5, "submit A full-accept committed=5", "committed=" + std::to_string(a));
    const std::uint64_t b = submit(43, 6, 1, 3);
    expect(b == 3, "submit B partial-accept committed=3", "committed=" + std::to_string(b));
    const std::uint64_t c = submit(44, 5, 1, 0);
    expect(c == 0, "submit C reject-all committed=0", "committed=" + std::to_string(c));
    const std::uint64_t d = submit(45, 5, 2, 2);
    expect(d == 2, "submit D multi-branch committed=2", "committed=" + std::to_string(d));

    // Tell the script we are ready for the worker kill + restart, then wait.
    std::printf("driver: restart-waiting\n");
    std::fflush(stdout);
    std::remove((tmp + "\\sf_restart_done").c_str());
    std::ofstream ready(tmp + "\\sf_restart_ready");
    ready << "ready"; ready.close();
    int waited = 0;
    while (!file_exists(tmp + "\\sf_restart_done") && waited < 300) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++waited;
    }

    // Over-protocol stale-epoch replay.
    {
        d::Message m; m.type = d::Msg::ProposalResult;
        m.u64_fields.assign(11, 0);
        m.u64_fields[d::PR_EPOCH] = 0;
        m.u64_fields[d::PR_PROPOSER_BOOT] = 999999;
        m.u64_fields[d::PR_ATTEMPT] = 1;
        m.u64_fields[d::PR_BASE_GEN] = 0;
        m.u64_fields[d::PR_PROPOGEN] = 1;
        m.u32_fields = {d::kOutcomeSuccess, 5, 1, 2, 3, 4, 5};
        sendm(fd, m);
        d::Message r; recvm(fd, r);
        expect(r.type == d::Msg::Reject && r.body.find("stale_epoch") != std::string::npos,
               "over-protocol stale-epoch replay rejected", "reply=" + r.body);
    }

    auto probe = [&](const std::string& kind) -> std::string {
        d::Message m; m.type = d::Msg::Info; m.body = "stale:" + kind; sendm(fd, m);
        d::Message r; recvm(fd, r);
        return r.type == d::Msg::Reject ? r.body : (r.type == d::Msg::Info ? r.body : "?");
    };
    const std::string pe = probe("epoch");
    expect(pe.find("stale_epoch") != std::string::npos, "stale:epoch rejected", "got=" + pe);
    const std::string pb = probe("boot");
    expect(pb.find("stale_worker_boot") != std::string::npos, "stale:boot rejected", "got=" + pb);
    const std::string pp = probe("prevBoot");
    expect(pp.find("stale_worker_boot") != std::string::npos,
           "stale:prevBoot (restarted worker) rejected", "got=" + pp);
    const std::string pa = probe("attempt");
    expect(pa.find("stale_attempt") != std::string::npos, "stale:attempt rejected", "got=" + pa);
    const std::string pg = probe("generation");
    expect(pg.find("stale_generation") != std::string::npos, "stale:generation rejected", "got=" + pg);
    const std::string pbase = probe("base");
    expect(pbase.find("wrong_base_generation") != std::string::npos, "stale:base rejected", "got=" + pbase);

    {
        d::Message m; m.type = d::Msg::Info; m.body = "rollEpoch"; sendm(fd, m);
        d::Message r; recvm(fd, r);
        expect(r.body.find("epoch=2") != std::string::npos, "epoch rolled to 2", "got=" + r.body);
    }
    const std::string pe2 = probe("epoch");
    expect(pe2.find("stale_epoch") != std::string::npos, "post-roll stale:epoch rejected", "got=" + pe2);

    {
        d::Message m; m.type = d::Msg::SubmitRequest; m.u64_fields = {46};
        m.u32_fields = {5, 1, 5}; sendm(fd, m);
        d::Message r; recvm(fd, r);
        expect(r.type == d::Msg::Commit && r.u64_fields[2] == 5,
               "fresh submit commits 5", "committed=" + std::to_string(r.u64_fields[2]));
    }
    {
        d::Message m; m.type = d::Msg::Info; m.body = "status"; sendm(fd, m);
        d::Message r; recvm(fd, r);
        std::printf("coordinator status: %s\n", r.body.c_str());
    }
    d::Message sd; sd.type = d::Msg::Shutdown; sendm(fd, sd);
    sfsock::cleanup();
    std::printf("driver: %s (%d failures)\n", failures == 0 ? "ALL_OK" : "HAD_FAILURES", failures);
    return failures == 0 ? 0 : 1;
}