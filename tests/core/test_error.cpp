// Unit tests for the error model.
#include "sf_test.hpp"
#include "speculation_fabric/core/error.hpp"

using namespace speculation_fabric;

SF_TEST_FN(result_ok_value_and_error) {
    auto r = Result<int>::ok(42);
    SF_CHECK(r.has_value());
    SF_CHECK(!r.is_error());
    SF_CHECK_EQ(r.value(), 42);
    SF_CHECK(r.error_code() == ErrorCode::ok);
}

SF_TEST_FN(result_err_struct) {
    auto r = Result<int>::err(ErrorCode::not_found, "missing");
    SF_CHECK(r.is_error());
    SF_CHECK(!r.has_value());
    SF_CHECK(r.error_code() == ErrorCode::not_found);
}

SF_TEST_FN(result_void) {
    auto ok = Result<void>::ok();
    SF_CHECK(ok.has_value());
    auto bad = Result<void>::err(ErrorCode::invalid_argument, "bad");
    SF_CHECK(bad.is_error());
    SF_CHECK(bad.error_code() == ErrorCode::invalid_argument);
}

SF_TEST_FN(result_map_preserves_error) {
    auto ok = Result<int>::ok(21).map([](int x) { return x * 2; });
    SF_CHECK(ok.has_value());
    SF_CHECK_EQ(ok.value(), 42);
    auto bad = Result<int>::err(ErrorCode::internal, "bad").map([](int x) { return x + 1; });
    SF_CHECK(bad.is_error());
    SF_CHECK(bad.error_code() == ErrorCode::internal);  // placeholder
}

SF_TEST_FN(error_to_string_stable) {
    SF_CHECK(std::string(to_string(ErrorCode::stale_epoch)) == "stale_epoch");
    SF_CHECK(std::string(describe(ErrorCode::stale_epoch)) == "stale coordinator epoch");
}

SF_TEST_FN(error_terminal_classification) {
    SF_CHECK(is_terminal(ErrorCode::committed));
    SF_CHECK(is_terminal(ErrorCode::cancelled));
    SF_CHECK(!is_terminal(ErrorCode::invalid_argument));
    SF_CHECK(!is_terminal(ErrorCode::stale_epoch));
}