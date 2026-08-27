// Framed wire protocol tests.
#include "sf_test.hpp"
#include "speculation_fabric/core/wire.hpp"

using namespace speculation_fabric;
using namespace speculation_fabric::wire;

SF_TEST_FN(wire_roundtrip_full_identity) {
    Message m;
    m.type = Msg::ProposalResult;
    m.u64_fields = {1ull, 2ull, 0xFFFFFFFFFFFFFFFFull, 0x123456789ABCDEF0ull};
    m.u32_fields = {5u, 4096u};
    m.body = "ok";
auto enc = encode(m);
SF_CHECK(enc.has_value());
auto dec = decode(enc.value());
SF_CHECK(dec.has_value());
    auto d = dec.value();
    SF_CHECK(d.type == Msg::ProposalResult);
    SF_CHECK(d.u64_fields == m.u64_fields);
    SF_CHECK(d.u32_fields == m.u32_fields);
    SF_CHECK(d.body == m.body);
    // 64-bit identity survives losslessly.
    SF_CHECK_EQ(d.u64_fields[2], 0xFFFFFFFFFFFFFFFFull);
    SF_CHECK_EQ(d.u64_fields[3], 0x123456789ABCDEF0ull);
}

SF_TEST_FN(wire_frame_roundtrip) {
    Message m;
    m.type = Msg::Hello;
    auto enc = encode(m);
    auto prefix = add_frame_prefix(enc.value());
    SF_CHECK(prefix.has_value());
    auto payload = extract_frame(prefix.value());
    SF_CHECK(payload.has_value());
    auto dec = decode(payload.value());
    SF_CHECK(dec.has_value());
    SF_CHECK(dec.value().type == Msg::Hello);
}

SF_TEST_FN(wire_rejects_unknown_version) {
    Message m;
    m.type = Msg::Hello;
    auto enc = encode(m);
    auto bytes = enc.value();
    bytes[0] = 0x63; bytes[1] = 0x00; bytes[2] = 0x00; bytes[3] = 0x00;
    auto dec = decode(bytes);
    SF_CHECK(dec.is_error());
    SF_CHECK(dec.error_code() == ErrorCode::unknown_protocol_version);
}

SF_TEST_FN(wire_rejects_unknown_type) {
    Message m;
    m.type = Msg::Hello;
    auto enc = encode(m);
    auto bytes = enc.value();
    bytes[4] = 0xE7; bytes[5] = 0x03; bytes[6] = 0x00; bytes[7] = 0x00;
    auto dec = decode(bytes);
    SF_CHECK(dec.is_error());
    SF_CHECK(dec.error_code() == ErrorCode::unknown_message_type);
}

SF_TEST_FN(wire_rejects_zero_length) {
    std::vector<std::uint8_t> stream = {0, 0, 0, 0};
    auto f = extract_frame(stream);
    SF_CHECK(f.is_error());
    SF_CHECK(f.error_code() == ErrorCode::zero_length_message);
}

SF_TEST_FN(wire_rejects_oversized_frame) {
    // Declared length 0x02000000 = 32 MiB > kMaxFrameSize.
    std::vector<std::uint8_t> stream = {0x00, 0x00, 0x00, 0x02};
    stream.resize(kFrameLenWidth + 2);
    auto f = extract_frame(stream);
    SF_CHECK(f.is_error());
    SF_CHECK(f.error_code() == ErrorCode::oversized_frame);
}

SF_TEST_FN(wire_rejects_truncated_frame) {
    std::vector<std::uint8_t> stream = {0x02, 0x00, 0x00, 0x00, 0x41};
    auto f = extract_frame(stream);
    SF_CHECK(f.is_error());
    SF_CHECK(f.error_code() == ErrorCode::truncated_frame);
}