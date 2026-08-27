// Speculation Fabric — persistence implementation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "speculation_fabric/core/persistence.hpp"

#include <fstream>
#include <cstdio>

#include <cstring>

namespace speculation_fabric {

namespace {

constexpr std::uint32_t kMaxRequests = 1000000;
constexpr std::uint32_t kMaxTokensPerRequest = 1000000;

// CRC-32 (IEEE) implementation for a checksum over the serialized payload.
std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-(static_cast<int>(crc & 1u)));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

class Writer {
public:
    void u8(std::uint8_t v) { out_.push_back(v); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) out_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }
    [[nodiscard]] const std::vector<std::uint8_t>& data() const { return out_; }

private:
    std::vector<std::uint8_t> out_;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& d) : d_(d), pos_(0) {}
    bool u8(std::uint8_t& v) {
        if (pos_ + 1 > d_.size()) return false;
        v = d_[pos_++];
        return true;
    }
    bool u32(std::uint32_t& v) {
        if (pos_ + 4 > d_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(d_[pos_++]) << (8 * i);
        return true;
    }
    bool u64(std::uint64_t& v) {
        if (pos_ + 8 > d_.size()) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(d_[pos_++]) << (8 * i);
        return true;
    }
    [[nodiscard]] std::size_t remaining() const { return d_.size() - pos_; }

private:
    const std::vector<std::uint8_t>& d_;
    std::size_t pos_;
};

}  // namespace

Result<std::vector<std::uint8_t>> serialize_archive(const StateArchive& archive) {
    Writer w;
    // Magic + version.
    w.u8('S'); w.u8('F'); w.u8('A'); w.u8('R');
    w.u32(archive.format_version);
    w.u64(archive.epoch.get());
    w.u32(static_cast<std::uint32_t>(archive.requests.size()));
    for (const auto& r : archive.requests) {
        w.u64(r.id.get());
        w.u64(r.sequence.get());
        w.u64(r.attempt.get());
        w.u64(r.epoch.get());
        w.u64(r.auth_gen.get());
        w.u64(r.auth_state.id.get());
        w.u64(r.auth_state.generation.get());
        w.u32(static_cast<std::uint32_t>(r.committed.size()));
        for (std::uint32_t t : r.committed) w.u32(t);
    }
    auto payload = w.data();
    const std::uint32_t crc = crc32(payload.data(), payload.size());
    std::vector<std::uint8_t> out = payload;
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((crc >> (8 * i)) & 0xFFu));
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

Result<StateArchive> deserialize_archive(const std::vector<std::uint8_t>& blob) {
    if (blob.size() < 4 + 4 + 8 + 4 + 4) {
        return Result<StateArchive>::err(ErrorCode::truncated_frame, "archive is truncated");
    }
    // Verify checksum over everything except the final 4 bytes.
    if (blob.size() < 4) {
        return Result<StateArchive>::err(ErrorCode::corrupt, "archive has no checksum");
    }
    const std::size_t payload_len = blob.size() - 4;
    const std::uint32_t stored_crc =
        static_cast<std::uint32_t>(blob[payload_len]) |
        (static_cast<std::uint32_t>(blob[payload_len + 1]) << 8) |
        (static_cast<std::uint32_t>(blob[payload_len + 2]) << 16) |
        (static_cast<std::uint32_t>(blob[payload_len + 3]) << 24);
    const std::uint32_t computed_crc = crc32(blob.data(), payload_len);
    if (stored_crc != computed_crc) {
        return Result<StateArchive>::err(ErrorCode::corrupt, "archive checksum mismatch");
    }

    Reader r(blob);
    std::uint8_t m0, m1, m2, m3;
    if (!r.u8(m0) || !r.u8(m1) || !r.u8(m2) || !r.u8(m3) || m0 != 'S' || m1 != 'F' ||
        m2 != 'A' || m3 != 'R') {
        return Result<StateArchive>::err(ErrorCode::malformed, "bad magic");
    }
    std::uint32_t version = 0;
    if (!r.u32(version)) {
        return Result<StateArchive>::err(ErrorCode::truncated_frame, "missing version");
    }
    if (version != 1) {
        return Result<StateArchive>::err(ErrorCode::unknown_protocol_version,
                                         "unsupported archive version " + std::to_string(version));
    }
    StateArchive out;
    out.format_version = version;
    std::uint64_t epoch = 0;
    if (!r.u64(epoch)) {
        return Result<StateArchive>::err(ErrorCode::truncated_frame, "missing epoch");
    }
    out.epoch = CoordinatorEpoch{epoch};
    std::uint32_t count = 0;
    if (!r.u32(count)) {
        return Result<StateArchive>::err(ErrorCode::truncated_frame, "missing count");
    }
    if (count > kMaxRequests) {
        return Result<StateArchive>::err(ErrorCode::out_of_range, "request count too large");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        PersistedRequest p;
        std::uint64_t v = 0;
        if (!r.u64(v)) return Result<StateArchive>::err(ErrorCode::truncated_frame, "request id");
        p.id = RequestId{v};
        if (!r.u64(v)) return Result<StateArchive>::err(ErrorCode::truncated_frame, "sequence");
        p.sequence = SequenceId{v};
        if (!r.u64(v)) return Result<StateArchive>::err(ErrorCode::truncated_frame, "attempt");
        p.attempt = AttemptId{v};
        if (!r.u64(v)) return Result<StateArchive>::err(ErrorCode::truncated_frame, "epoch");
        p.epoch = CoordinatorEpoch{v};
        if (!r.u64(v)) return Result<StateArchive>::err(ErrorCode::truncated_frame, "auth_gen");
        p.auth_gen = AuthGeneration{v};
        if (!r.u64(v)) return Result<StateArchive>::err(ErrorCode::truncated_frame, "state id");
        p.auth_state.id = StateId{v};
        if (!r.u64(v)) return Result<StateArchive>::err(ErrorCode::truncated_frame, "state gen");
        p.auth_state.generation = StateGeneration{v};
        std::uint32_t tcount = 0;
        if (!r.u32(tcount)) return Result<StateArchive>::err(ErrorCode::truncated_frame, "token count");
        if (tcount > kMaxTokensPerRequest) {
            return Result<StateArchive>::err(ErrorCode::out_of_range, "token count too large");
        }
        p.committed.reserve(tcount);
        for (std::uint32_t t = 0; t < tcount; ++t) {
            std::uint32_t tok = 0;
            if (!r.u32(tok)) return Result<StateArchive>::err(ErrorCode::truncated_frame, "token");
            p.committed.push_back(tok);
        }
        out.requests.push_back(std::move(p));
    }
    (void)r.remaining();  // the 4-byte checksum suffix is validated above
    return Result<StateArchive>::ok(std::move(out));
}

Result<void> FileStore::save(const std::string& blob) {
    std::ofstream f(path_, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        return Result<void>::err(ErrorCode::internal, "could not open " + path_ + " for write");
    }
    f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    f.flush();
    if (!f.good()) {
        return Result<void>::err(ErrorCode::internal, "short write to " + path_);
    }
    return Result<void>::ok();
}

Result<std::string> FileStore::load() {
    std::ifstream f(path_, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        return Result<std::string>::err(ErrorCode::not_found, "could not open " + path_ + " for read");
    }
    const std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::string data(static_cast<std::size_t>(sz), '\0');
    f.read(data.data(), sz);
    if (static_cast<std::streamsize>(f.gcount()) != sz) {
        return Result<std::string>::err(ErrorCode::internal, "short read from " + path_);
    }
    return Result<std::string>::ok(std::move(data));
}

}  // namespace speculation_fabric