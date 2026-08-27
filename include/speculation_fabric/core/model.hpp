// Speculation Fabric — model / device / executor identity.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Model identity is deliberately vendor-neutral. A ModelIdentity names a
// model, its revision, its tokenizer/vocabulary, an optional adapter stack,
// and the executor/device that can run it. No inference framework's type
// leaks into the runtime's semantic definition.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "speculation_fabric/core/id.hpp"

namespace speculation_fabric {

// The kind of numerical executor a model runs on.
enum class ExecutorKind {
    CPU,
    CUDA,
    ROCm,
    Metal,
    Custom,
    Unknown,
};

// Describes a device capable of executing speculative work. Device identity
// is opaque; the runtime only compares the identity, never assumes a device
// API. The canonical string is the stable identity used for comparisons.
struct DeviceDescriptor {
    DeviceId id{};
    std::string name;          // human-readable, vendor-neutral label
    std::string canonical;     // stable identity string, e.g. "cuda:0"
    ExecutorKind kind{ExecutorKind::Unknown};
    std::uint32_t compute_capability_major{0};
    std::uint32_t compute_capability_minor{0};
    std::uint64_t memory_bytes{0};

    bool operator==(const DeviceDescriptor&) const = default;
};

// A tokenizer/vocabulary identity. Two models may only share candidate
// tokens when their vocabulary identity matches exactly; a mismatch is a
// correctness rejection, never a silent coercion.
struct TokenizerIdentity {
    TokenizerId id{};
    std::string name;      // canonical tokenizer name
    std::uint32_t vocab_size{0};
    std::string hash;      // deterministic content hash of the vocabulary

    bool operator==(const TokenizerIdentity&) const = default;
};

// Identity of a numerical executor (the thing that actually runs a model).
struct ExecutorIdentity {
    ExecutorId id{};
    std::string name;      // canonical executor name
    ExecutorKind kind{ExecutorKind::Unknown};
    std::uint32_t protocol_version{0};  // candidate protocol version

    bool operator==(const ExecutorIdentity&) const = default;
};

// An adapter stack applied over a base model (LoRA, prefix, etc.). Adapters
// participate in compatibility because the same base model with different
// adapters does not produce interchangeable candidate tokens.
struct AdapterIdentity {
    AdapterId id{};
    std::string name;
    std::string revision;

    bool operator==(const AdapterIdentity&) const = default;
};

// The full identity of a model as a speculative participant.
struct ModelIdentity {
    ModelId model{};
    Revision revision{};
    TokenizerIdentity tokenizer{};
    std::optional<AdapterIdentity> adapter{};
    ExecutorIdentity executor{};
    DeviceDescriptor device{};
    std::string name;

    bool operator==(const ModelIdentity&) const = default;

    // Stable canonical string form used for explaining and hashing.
    std::string canonical() const {
        std::string s = "model=" + name + ";rev=" + revision.str() +
                        ";tok=" + tokenizer.name + ";";
        if (adapter) s += "adapter=" + adapter->name + ";";
        s += "exec=" + executor.name + ";dev=" + device.canonical;
        return s;
    }
};

// A proposer/verifier model pair under evaluation.
struct ModelPair {
    ModelIdentity proposer;
    ModelIdentity verifier;

    bool operator==(const ModelPair&) const = default;
};

// Identifies a specific executor instance (a worker) registered with the
// runtime. A WorkerDescriptor carries the boot identity so that a restarted
// worker is distinguishable from the previous incarnation.
struct WorkerDescriptor {
    WorkerId id{};
    WorkerBootId boot_id{};
    std::string role_name;    // "coordinator", "proposer", "verifier", "client"
    ExecutorIdentity executor{};
    DeviceDescriptor device{};
    std::string address;      // host or endpoint for the distributed protocol
    std::uint16_t port{0};

    bool operator==(const WorkerDescriptor&) const = default;
};

// Capability a worker offers for the distributed control plane.
enum class WorkerCapability : std::uint8_t {
    None = 0,
    Propose = 1,
    Verify = 2,
    ProposeAndVerify = Propose | Verify,
};

inline WorkerCapability operator|(WorkerCapability a, WorkerCapability b) {
    return static_cast<WorkerCapability>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
inline bool has_capability(WorkerCapability value, WorkerCapability cap) {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(cap)) != 0;
}

}  // namespace speculation_fabric
