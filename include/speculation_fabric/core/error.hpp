// Speculation Fabric — error model.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// Speculation Fabric tracks every failure and rejection as a structured
// error rather than relying on exception-driven control flow. Ordinary
// runtime control flow uses Result<T>; exceptions are reserved for
// programmer error and are never used to signal an expected rejection.

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <type_traits>

namespace speculation_fabric {

// A structured, machine-readable error code. Codes are deliberately
// coarse-grained and grouped so an operator can branch on category and
// inspect the human-readable message for the specific cause.
enum class ErrorCode {
    ok = 0,

    // -- General category ----------------------------------------------------
    invalid_argument = 1,
    out_of_range = 2,
    not_found = 3,
    already_exists = 4,
    invalid_state = 5,
    unknown = 6,
    not_implemented = 7,
    internal = 8,
    capacity_exceeded = 9,
    buffer_full = 10,

    // -- Validation ----------------------------------------------------------
    invalid_proposal = 100,
    invalid_candidate = 101,
    invalid_branch = 102,
    invalid_depth = 103,
    invalid_token_encoding = 104,
    token_count_mismatch = 105,
    acceptance_exceeds_candidate = 106,
    invalid_verification_result = 107,
    empty_candidate = 108,
    invalid_authoritative_generation = 109,
    invalid_candidate_generation = 110,
    candidate_length_mismatch = 111,
    invalid_request = 112,
    invalid_identity = 113,
    invalid_dispatch = 114,
    invalid_attempt = 115,
    impossible_candidate_depth = 116,
    invalid_zero_depth = 117,

    // -- Authority / staleness ----------------------------------------------
    stale_epoch = 200,
    stale_worker_boot = 201,
    stale_attempt = 202,
    stale_proposal_generation = 203,
    stale_verification_generation = 204,
    wrong_base_generation = 205,
    duplicate_proposal = 206,
    duplicate_verification = 207,
    completion_after_cancellation = 208,
    completion_after_terminal = 209,
    losing_branch_commit = 210,
    late_proposal = 211,
    late_verification = 212,
    stale_authority = 213,
    obsolete_worker = 214,

    // -- Compatibility -------------------------------------------------------
    incompatible_model_pair = 300,
    incompatible_tokenizer = 301,
    incompatible_vocabulary = 302,
    incompatible_adapter = 303,
    incompatible_protocol = 304,
    incompatible_device = 305,
    incompatible_executor = 306,
    incompatible_state_layout = 307,
    incompatible_revision = 308,
    incompatible_operator_policy = 309,

    // -- Resource / scheduling ----------------------------------------------
    reservation_underflow = 400,
    double_release = 401,
    deadline_exceeded = 402,
    budget_exceeded = 403,
    backpressure = 404,
    request_cancelled = 405,
    expired = 406,
    proposer_unavailable = 407,
    verifier_unavailable = 408,
    reservation_leak = 409,
    memory_pressure = 410,
    shut_down = 411,

    // -- Terminal outcomes ---------------------------------------------------
    rejected = 500,
    rolled_back = 501,
    superseded = 502,
    failed = 503,
    already_terminal = 504,
    cancelled = 505,
    committed = 506,
    fully_accepted = 507,
    partially_accepted = 508,
    retryable_failure = 509,
    non_retryable_failure = 510,

    // -- Proposal / verification results ------------------------------------
    proposal_success = 600,
    proposal_retryable_failure = 601,
    proposal_non_retryable_failure = 602,
    verification_success_full_accept = 603,
    verification_success_partial_accept = 604,
    verification_reject_all = 605,
    verification_retryable_failure = 606,
    verification_non_retryable_failure = 607,

    // -- Protocol / framing --------------------------------------------------
    malformed_frame = 700,
    oversized_frame = 701,
    truncated_frame = 702,
    unknown_protocol_version = 703,
    unknown_message_type = 704,
    malformed_identity = 705,
    invalid_message = 706,
    zero_length_message = 707,

    // -- State machine -------------------------------------------------------
    invalid_transition = 800,
    not_ready = 801,
    wrong_state = 802,
    state_machine_cycle_overflow = 803,
};

// A structured error carries a code and a human-readable, deterministic
// message. It is immutable once constructed.
struct Error {
    ErrorCode code = ErrorCode::ok;
    std::string message;

    Error() = default;
    Error(ErrorCode c, std::string m) : code(c), message(std::move(m)) {}

    bool is_ok() const noexcept { return code == ErrorCode::ok; }
};

// Renders an ErrorCode as a stable, identifier-like string (the enum name).
const char* to_string(ErrorCode code) noexcept;

// Renders an ErrorCode as a short human-readable phrase.
const char* describe(ErrorCode code) noexcept;

// Returns true when the code represents a terminal outcome for a
// speculative cycle or request.
bool is_terminal(ErrorCode code) noexcept;

// ---------------------------------------------------------------------------
// Result<T> — a value-or-error container used for ordinary control flow.
// ---------------------------------------------------------------------------
//
//   Result<int> r = Result<int>::ok(42);
//   if (r.has_value()) { auto v = r.value(); }
//   else { auto e = r.error(); }
//
// value() is only valid when has_value() is true. The caller checks the
// state first; the library never throws for an expected rejection.

template<class T>
class [[nodiscard]] Result {
public:
    using value_type = T;
    using error_type = Error;

    Result(const Result&) = default;
    Result(Result&&) noexcept = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) noexcept = default;
    ~Result() = default;

    static Result ok(T value) noexcept(noexcept(T(std::move(value)))) {
        Result r;
        r.value_.emplace(std::move(value));
        return r;
    }
    static Result err(ErrorCode code, std::string message) noexcept {
        Result r;
        r.error_.code = code;
        r.error_.message = std::move(message);
        return r;
    }
    static Result err(Error error) noexcept {
        Result r;
        r.error_ = std::move(error);
        return r;
    }

    [[nodiscard]] bool has_value() const noexcept { return value_.has_value(); }
    [[nodiscard]] bool is_error() const noexcept { return !value_.has_value(); }

    [[nodiscard]] T& value() & noexcept { return *value_; }
    [[nodiscard]] const T& value() const& noexcept { return *value_; }
    [[nodiscard]] T&& value() && noexcept { return std::move(*value_); }

    [[nodiscard]] T value_or(T fallback) const& noexcept(std::is_nothrow_copy_assignable_v<T>) {
        return has_value() ? *value_ : std::move(fallback);
    }

    [[nodiscard]] const Error& error() const& noexcept { return error_; }
    [[nodiscard]] Error& error() & noexcept { return error_; }
    [[nodiscard]] ErrorCode error_code() const noexcept { return error_.code; }

    // Transforms the payload while preserving the error path.
    template<class F>
    [[nodiscard]] auto map(F&& f) const& -> Result<std::invoke_result_t<F, const T&>> {
        using U = std::invoke_result_t<F, const T&>;
        if (has_value()) {
            return Result<U>::ok(std::invoke(std::forward<F>(f), *value_));
        }
        return Result<U>::err(error_);
    }

    // Chains a fallible continuation on success.
    template<class F>
    [[nodiscard]] auto and_then(F&& f) const& -> std::invoke_result_t<F, const T&> {
        if (has_value()) {
            return std::invoke(std::forward<F>(f), *value_);
        }
        return std::remove_cvref_t<std::invoke_result_t<F, const T&>>::err(error_);
    }

    // Produces an alternative on error.
    template<class F>
    [[nodiscard]] Result or_else(F&& f) const& {
        if (has_value()) {
            return *this;
        }
        return std::invoke(std::forward<F>(f), error_);
    }

private:
    Result() : error_{ErrorCode::ok, ""} {}
    std::optional<T> value_;
    Error error_;
};

// Result<void> — success carries no payload, only the error path is meaningful.
template<>
class [[nodiscard]] Result<void> {
public:
    using value_type = void;
    using error_type = Error;

    Result(const Result&) = default;
    Result(Result&&) noexcept = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) noexcept = default;
    ~Result() = default;

    static Result ok() noexcept {
        Result r;
        r.ok_ = true;
        return r;
    }
    static Result err(ErrorCode code, std::string message) noexcept {
        Result r;
        r.ok_ = false;
        r.error_.code = code;
        r.error_.message = std::move(message);
        return r;
    }
    static Result err(Error error) noexcept {
        Result r;
        r.ok_ = false;
        r.error_ = std::move(error);
        return r;
    }

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    [[nodiscard]] bool is_error() const noexcept { return !ok_; }
    [[nodiscard]] const Error& error() const& noexcept { return error_; }
    [[nodiscard]] ErrorCode error_code() const noexcept { return error_.code; }

private:
    Result() : ok_(false), error_{ErrorCode::ok, ""} {}
    bool ok_;
    Error error_;
};

template<class T>
using Errored = Result<T>;

}  // namespace speculation_fabric
