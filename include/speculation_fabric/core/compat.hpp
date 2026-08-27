// Speculation Fabric — model-pair compatibility.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// A speculative pair is never treated as valid merely because two model
// names are supplied. Compatibility is a typed, deterministic decision over
// the full identity of both participants. A mismatched tokenizer/vocabulary
// or candidate interpretation is a correctness rejection; incompatible
// candidates are never silently coerced.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "speculation_fabric/core/error.hpp"
#include "speculation_fabric/core/model.hpp"

namespace speculation_fabric {

// The key that uniquely identifies a compatibility question. Two questions
// with the same key MUST produce the same decision (deterministic).
struct ModelPairCompatibilityKey {
    ModelIdentity proposer;
    ModelIdentity verifier;
    std::uint32_t protocol_version{1};

    bool operator==(const ModelPairCompatibilityKey&) const = default;

    std::string canonical() const {
        return proposer.canonical() + "|" + verifier.canonical() + "|proto=" +
               std::to_string(protocol_version);
    }
};

struct std_hash_model_pair {
    std::size_t operator()(const ModelPairCompatibilityKey&) const noexcept;
};

// Why a pair is or is not compatible, in an explainable, deterministic form.
struct CompatReason {
    // A stable code, e.g. "tokenizer_mismatch".
    std::string code;
    // A human-readable, deterministic explanation.
    std::string detail;
    bool fatal{true};

    bool operator==(const CompatReason&) const = default;
};

// The decision. Semantics:
//   * compatible == true  => identical key resolves to the same decision.
//   * compatible == false => at least one fatal reason is present.
struct ModelPairCompatibilityDecision {
    bool compatible{false};
    bool cache_hit{false};
    std::vector<CompatReason> reasons;   // non-empty when incompatible (or informative)
    std::string canonical_key;           // the key this decision was computed for

    bool operator==(const ModelPairCompatibilityDecision&) const = default;
};

// A small, deterministic table of the known compatibility checks. The
// decision is computed from the identity values and is always reproducible.
inline bool same_executor_kind(ExecutorKind a, ExecutorKind b) noexcept {
    return a == b || a == ExecutorKind::Unknown || b == ExecutorKind::Unknown;
}


// Computes the deterministic compatibility decision for a key. The decision
// depends only on the key, so an identical key always yields an identical
// decision and can be cached trivially.
ModelPairCompatibilityDecision evaluate_compatibility(const ModelPairCompatibilityKey& key);

}  // namespace speculation_fabric