// Speculation Fabric — adaptive depth + policy implementation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "speculation_fabric/core/policy.hpp"

#include <algorithm>
#include <cmath>

namespace speculation_fabric {

std::uint32_t AdaptiveDepth::choose_depth(const ProposalPolicy& policy,
                                          std::uint32_t available_memory_headroom,
                                          double verifier_saturation) const {
    if (!policy.adaptive_depth_enabled || history_.empty()) {
        return policy.max_depth;
    }

    // Deterministic scaling from the recent acceptance history.
    const double r = average_ratio();
    double f;
    if (r >= 0.80) {
        f = 1.0;
    } else if (r >= 0.50) {
        f = 0.60 + 0.40 * ((r - 0.50) / 0.30);
    } else {
        f = 0.30 + 0.30 * (r / 0.50);
    }

    double depth = static_cast<double>(policy.max_depth) * f;
    // Memory headroom: if less than one token per depth slot is available,
    // scale down proportionally.
    if (available_memory_headroom != 0) {
        const double memory_ratio =
            std::min(1.0, static_cast<double>(available_memory_headroom) /
                              (128.0 * static_cast<double>(policy.max_depth)));
        depth *= memory_ratio;
    }
    // Verifier saturation: reduce depth when the verifier is saturated.
    const double sat = std::clamp(verifier_saturation, 0.0, 1.0);
    depth *= (1.0 - 0.5 * sat);

    const std::uint32_t d = static_cast<std::uint32_t>(std::llround(depth));
    return static_cast<std::uint32_t>(
        std::clamp(static_cast<int>(d), 1, static_cast<int>(policy.max_depth)));
}

double AdaptiveDepth::average_ratio() const {
    if (history_.empty()) return 0.0;
    double sum = 0.0;
    for (double v : history_) sum += v;
    return sum / static_cast<double>(history_.size());
}

}  // namespace speculation_fabric
