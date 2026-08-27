// Speculation Fabric — version header.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Single source of truth for the library version and the protocol version.

#pragma once

#include <cstdint>

namespace speculation_fabric {

// The software version of the Speculation Fabric runtime.
struct Version {
    static constexpr int major = 1;
    static constexpr int minor = 0;
    static constexpr int patch = 0;
    static constexpr const char* string = "1.0.0";
};

// The wire protocol version used by the framed distributed control plane.
// Increment on any incompatible change to the frame layout or to the
// semantics of a distributed message.
struct ProtocolVersion {
    static constexpr std::uint32_t value = 1;
};

}  // namespace speculation_fabric
