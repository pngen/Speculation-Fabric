// Speculation Fabric — candidate token and sequence model.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "speculation_fabric/core/error.hpp"
#include "speculation_fabric/core/id.hpp"

namespace speculation_fabric {

// A single candidate token. The integer id is the lossless canonical form;
// an optional byte sequence may carry the token's text representation. Token
// identity for compatibility is the integer id interpreted by the verifier's
// vocabulary.
struct Token {
    TokenId id{0};
    std::optional<std::string> text{};

    bool operator==(const Token&) const = default;
};

// The maximum speculative depth the runtime will ever admit, and the depth
// chosen for a specific proposal. Depth is per-proposal and may vary.
using CandidateDepth = std::uint32_t;

// An immutable candidate token sequence. After a proposal is finalized, the
// candidate is not mutated. A candidate carries its intended depth (which
// equals the token count at finalization) and a content hash.
struct CandidateSequence {
    std::vector<Token> tokens{};

    CandidateSequence() = default;
    explicit CandidateSequence(std::vector<Token> t) : tokens(std::move(t)) {}

    [[nodiscard]] std::size_t size() const noexcept { return tokens.size(); }
    [[nodiscard]] bool empty() const noexcept { return tokens.empty(); }
    [[nodiscard]] CandidateDepth depth() const noexcept {
        return static_cast<CandidateDepth>(tokens.size());
    }

    // A deterministic FNV-1a content hash over the token ids, used for
    // cheap identity checks and explaining.
    [[nodiscard]] std::uint64_t content_hash() const noexcept {
        std::uint64_t h = 1469598103934665603ULL;
        for (const auto& tok : tokens) {
            h ^= static_cast<std::uint64_t>(tok.id);
            h *= 1099511628211ULL;
        }
        return h;
    }

    // Validates a candidate against a maximum depth. Empty candidates are a
    // correctness error for a proposal, but a verifier may report a
    // candidate with a truncated length.
    [[nodiscard]] Result<CandidateDepth> validate(CandidateDepth max_depth) const {
        if (empty()) {
            return Result<CandidateDepth>::err(ErrorCode::empty_candidate,
                                               "candidate sequence is empty");
        }
        if (size() > max_depth) {
            return Result<CandidateDepth>::err(
                ErrorCode::impossible_candidate_depth,
                "candidate depth " + std::to_string(size()) +
                    " exceeds maximum " + std::to_string(max_depth));
        }
        return Result<CandidateDepth>::ok(depth());
    }

    bool operator==(const CandidateSequence&) const = default;
};

// The identity carried by a candidate so a verifier can confirm the token
// ids were interpreted against the expected vocabulary.
struct CandidateTokenIdentity {
    TokenizerId tokenizer{};             // vocabulary identity
    std::uint32_t vocab_size{0};
    std::string format{"int32"};         // lossless integer encoding

    bool operator==(const CandidateTokenIdentity&) const = default;
};

}  // namespace speculation_fabric
