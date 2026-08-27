// Speculation Fabric - wire protocol implementation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "speculation_fabric/core/wire.hpp"

namespace speculation_fabric {
namespace wire {

namespace {
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}
void put_u64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}
}  // namespace

Result<std::vector<std::uint8_t>> encode(const Message& msg) {
    if (!msg.valid()) {
        return Result<std::vector<std::uint8_t>>::err(
            ErrorCode::invalid_message, "cannot encode invalid message");
    }
    if (msg.u64_fields.size() > 64 || msg.u32_fields.size() > 64 ||
        msg.body.size() > 1024 * 1024) {
        return Result<std::vector<std::uint8_t>>::err(
            ErrorCode::oversized_frame, "message fields exceed protocol limits");
    }
    std::vector<std::uint8_t> out;
    out.reserve(4 + 4 + 8 * msg.u64_fields.size() + 4 * msg.u32_fields.size() +
                msg.body.size() + 4);
    put_u32(out, msg.protocol_version);
    put_u32(out, static_cast<std::uint32_t>(msg.type));
    put_u32(out, static_cast<std::uint32_t>(msg.u64_fields.size()));
    put_u32(out, static_cast<std::uint32_t>(msg.u32_fields.size()));
    put_u32(out, static_cast<std::uint32_t>(msg.body.size()));
    for (std::uint64_t v : msg.u64_fields) put_u64(out, v);
    for (std::uint32_t v : msg.u32_fields) put_u32(out, v);
    out.insert(out.end(), msg.body.begin(), msg.body.end());
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

Result<Message> decode(const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 20) {
        return Result<Message>::err(ErrorCode::truncated_frame, "payload too short");
    }
    std::size_t pos = 0;
    auto read_u32 = [&](std::uint32_t& v) -> bool {
        if (pos + 4 > payload.size()) return false;
        v = static_cast<std::uint32_t>(payload[pos]) |
            (static_cast<std::uint32_t>(payload[pos + 1]) << 8) |
            (static_cast<std::uint32_t>(payload[pos + 2]) << 16) |
            (static_cast<std::uint32_t>(payload[pos + 3]) << 24);
        pos += 4;
        return true;
    };
    auto read_u64 = [&](std::uint64_t& v) -> bool {
        if (pos + 8 > payload.size()) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(payload[pos + i]) << (8 * i);
        }
        pos += 8;
        return true;
    };

    Message msg;
    if (!read_u32(msg.protocol_version)) {
        return Result<Message>::err(ErrorCode::truncated_frame, "missing protocol version");
    }
    if (msg.protocol_version != kProtocolVersion) {
        return Result<Message>::err(ErrorCode::unknown_protocol_version,
                                    "unknown protocol version");
    }
    std::uint32_t t = 0;
    if (!read_u32(t)) {
        return Result<Message>::err(ErrorCode::truncated_frame, "missing message type");
    }
    msg.type = static_cast<Msg>(t);
    if (msg.type == Msg::Null || t > static_cast<std::uint32_t>(Msg::Shutdown)) {
        return Result<Message>::err(ErrorCode::unknown_message_type,
                                    "unknown message type " + std::to_string(t));
    }
    std::uint32_t n64 = 0, n32 = 0, blen = 0;
    if (!read_u32(n64) || !read_u32(n32) || !read_u32(blen)) {
        return Result<Message>::err(ErrorCode::truncated_frame, "missing field counts");
    }
    if (n64 > 64 || n32 > 64 || blen > 1024 * 1024) {
        return Result<Message>::err(ErrorCode::oversized_frame, "field counts exceed limits");
    }
    msg.u64_fields.resize(n64);
    for (std::uint32_t i = 0; i < n64; ++i) {
        if (!read_u64(msg.u64_fields[i])) {
            return Result<Message>::err(ErrorCode::truncated_frame, "truncated u64 field");
        }
    }
    msg.u32_fields.resize(n32);
    for (std::uint32_t i = 0; i < n32; ++i) {
        if (!read_u32(msg.u32_fields[i])) {
            return Result<Message>::err(ErrorCode::truncated_frame, "truncated u32 field");
        }
    }
    if (blen > payload.size() - pos) {
        return Result<Message>::err(ErrorCode::truncated_frame, "truncated body");
    }
    msg.body.assign(reinterpret_cast<const char*>(payload.data() + pos), blen);
    pos += blen;
    if (pos != payload.size()) {
        return Result<Message>::err(ErrorCode::malformed, "trailing bytes in payload");
    }
    return Result<Message>::ok(std::move(msg));
}

Result<std::vector<std::uint8_t>> add_frame_prefix(const std::vector<std::uint8_t>& payload) {
    if (payload.size() > kMaxFrameSize) {
        return Result<std::vector<std::uint8_t>>::err(ErrorCode::oversized_frame,
                                                      "payload exceeds max frame");
    }
    std::vector<std::uint8_t> out;
    out.reserve(kFrameLenWidth + payload.size());
    const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((len >> (8 * i)) & 0xFFu));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

Result<std::vector<std::uint8_t>> extract_frame(const std::vector<std::uint8_t>& stream) {
    if (stream.size() < kFrameLenWidth) {
        return Result<std::vector<std::uint8_t>>::err(ErrorCode::truncated_frame,
                                                      "stream too short for frame length");
    }
    const std::uint32_t len = static_cast<std::uint32_t>(stream[0]) |
                              (static_cast<std::uint32_t>(stream[1]) << 8) |
                              (static_cast<std::uint32_t>(stream[2]) << 16) |
                              (static_cast<std::uint32_t>(stream[3]) << 24);
    if (len == 0) {
        return Result<std::vector<std::uint8_t>>::err(ErrorCode::zero_length_message,
                                                      "zero-length frame");
    }
    if (len > kMaxFrameSize) {
        return Result<std::vector<std::uint8_t>>::err(ErrorCode::oversized_frame,
                                                      "frame exceeds max size");
    }
    if (stream.size() < kFrameLenWidth + len) {
        return Result<std::vector<std::uint8_t>>::err(ErrorCode::truncated_frame,
                                                      "stream shorter than frame");
    }
    return Result<std::vector<std::uint8_t>>::ok(
        std::vector<std::uint8_t>(stream.begin() + kFrameLenWidth,
                                  stream.begin() + kFrameLenWidth + len));
}

}  // namespace wire
}  // namespace speculation_fabric