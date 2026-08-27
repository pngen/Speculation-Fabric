// Speculation Fabric — clock semantics.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
#include <string>
// Deadlines and latency budgets are expressed against a pluggable clock, so
// the runtime has deterministic time semantics for testing and a monotonic
// steady clock for production. The runtime never depends on wall-clock or on
// externally injected test timeouts for correctness.

#pragma once

#include <chrono>
#include <cstdint>

namespace speculation_fabric {

// Abstraction over time used for latency class, deadlines, and expiry.
class Clock {
public:
    virtual ~Clock() = default;

    // Monotonic nanoseconds (never decreases is guaranteed for a steady clock).
    [[nodiscard]] virtual std::uint64_t monotonic_ns() const noexcept = 0;
    // Wall-clock nanoseconds (may be used for persistence timestamps).
    [[nodiscard]] virtual std::uint64_t wall_ns() const noexcept = 0;
};

// The default steady clock backed by std::chrono::steady_clock.
class SteadyClock final : public Clock {
public:
    [[nodiscard]] std::uint64_t monotonic_ns() const noexcept override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }
    [[nodiscard]] std::uint64_t wall_ns() const noexcept override {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }
};

// Renders a duration of nanoseconds as a scale-appropriate string.
std::string format_ns(std::uint64_t ns);

}  // namespace speculation_fabric