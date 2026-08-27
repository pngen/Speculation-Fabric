// Speculation Fabric - framed wire protocol for the distributed control plane.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Real framed TCP. Every frame is: [4-byte little-endian length][payload].
// The payload is a binary message: an 8-byte header (protocol version u32,
// message type u32) followed by fixed-width integer fields, so 64-bit
// identities are carried losslessly (never as floating-point JSON numbers).
//
// The decoder performs strict validation: zero-length messages, oversized
// frames, truncated frames, unknown protocol versions, unknown message types,
// and malformed identities are all rejected.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "speculation_fabric/core/error.hpp"

namespace speculation_fabric {
namespace wire {

// Hard maximum frame size.
constexpr std::uint32_t kMaxFrameSize = 16 * 1024 * 1024;  // 16 MiB
// The protocol version carried in every message header.
constexpr std::uint32_t kProtocolVersion = 1;
// Fixed width of the frame length prefix.
constexpr std::size_t kFrameLenWidth = 4;

enum class Msg : std::uint32_t {
    Null = 0,
    Hello = 1,
    SubmitRequest = 2,
    ProposalDispatch = 3,
    ProposalResult = 4,
    VerificationDispatch = 5,
    VerificationResult = 6,
    Commit = 7,
    Reject = 8,
    Cancel = 9,
    Info = 10,
    Shutdown = 11,
};

struct Message {
    std::uint32_t protocol_version{kProtocolVersion};
    Msg type{Msg::Null};
    std::vector<std::uint64_t> u64_fields;
    std::vector<std::uint32_t> u32_fields;
    std::string body;

    [[nodiscard]] bool valid() const noexcept {
        return protocol_version == kProtocolVersion && type != Msg::Null;
    }
};

Result<std::vector<std::uint8_t>> encode(const Message& msg);
Result<Message> decode(const std::vector<std::uint8_t>& payload);
Result<std::vector<std::uint8_t>> add_frame_prefix(const std::vector<std::uint8_t>& payload);
Result<std::vector<std::uint8_t>> extract_frame(const std::vector<std::uint8_t>& stream);

}  // namespace wire
}  // namespace speculation_fabric
