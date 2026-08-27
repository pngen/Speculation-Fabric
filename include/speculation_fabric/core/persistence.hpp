// Speculation Fabric — persistence and recovery.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Authoritative state is persisted in a versioned, checksummed, deterministic
// binary format. On recovery the runtime reconstructs an equivalent
// authoritative state and reconciles formerly in-flight work conservatively:
// pre-crash proposal/verification work is never treated as automatically
// authoritative.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "speculation_fabric/core/error.hpp"
#include "speculation_fabric/core/fabric.hpp"
#include "speculation_fabric/core/id.hpp"
#include "speculation_fabric/core/state.hpp"

namespace speculation_fabric {

// One request's recoverable authoritative state.
struct PersistedRequest {
    RequestId id{};
    SequenceId sequence{};
    AttemptId attempt{};
    CoordinatorEpoch epoch{};
    AuthGeneration auth_gen{};
    StateRef auth_state{};
    std::vector<std::uint32_t> committed{};

    bool operator==(const PersistedRequest&) const = default;
};

// A closed archive of authoritative state, exactly as it should be restored.
struct StateArchive {
    std::uint32_t format_version{1};
    CoordinatorEpoch epoch{};
    std::vector<PersistedRequest> requests{};

    bool operator==(const StateArchive&) const = default;
};

// Serializes an archive into a deterministic binary blob (little-endian,
// fixed-width integers, trailing CRC-32 checksum).
Result<std::vector<std::uint8_t>> serialize_archive(const StateArchive& archive);

// Deserializes and fully validates a blob. Rejects unknown versions,
// truncation, corruption (checksum mismatch), and over- or under-long counts.
Result<StateArchive> deserialize_archive(const std::vector<std::uint8_t>& blob);

// A file-backed Store implementing the Persistence interface.
class FileStore final : public Persistence {
public:
    explicit FileStore(std::string path) : path_(std::move(path)) {}

    Result<void> save(const std::string& blob) override;
    Result<std::string> load() override;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
};

}  // namespace speculation_fabric
