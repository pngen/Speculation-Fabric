// Persistence + recovery tests.
#include "sf_test.hpp"
#include "speculation_fabric/core/persistence.hpp"

using namespace speculation_fabric;

static StateArchive make_archive() {
    StateArchive a;
    a.format_version = 1;
    a.epoch = CoordinatorEpoch{7};
    for (int i = 0; i < 3; ++i) {
        PersistedRequest r;
        r.id = RequestId{100 + static_cast<std::uint64_t>(i)};
        r.sequence = SequenceId{1000 + static_cast<std::uint64_t>(i)};
        r.attempt = AttemptId{2};
        r.epoch = CoordinatorEpoch{7};
        r.auth_gen = AuthGeneration{5 + static_cast<std::uint64_t>(i)};
        r.auth_state.id = StateId{50 + static_cast<std::uint64_t>(i)};
        r.auth_state.generation = StateGeneration{5 + static_cast<std::uint64_t>(i)};
        for (int t = 0; t < 4 + i; ++t) {
            r.committed.push_back(static_cast<std::uint32_t>(1000 + t + i * 100));
        }
        a.requests.push_back(r);
    }
    return a;
}

SF_TEST_FN(persistence_roundtrip) {
    auto a = make_archive();
    auto blob = serialize_archive(a);
    SF_CHECK(blob.has_value());
    auto b = deserialize_archive(blob.value());
    SF_CHECK(b.has_value());
    SF_CHECK(b.value() == a);
    SF_CHECK_EQ(b.value().epoch.get(), 7u);
    SF_CHECK_EQ(b.value().requests.size(), 3u);
}

SF_TEST_FN(persistence_checksum_rejects_corruption) {
    auto a = make_archive();
    auto blob = serialize_archive(a);
    auto bytes = blob.value();
    // Flip a byte in the middle.
    bytes[bytes.size() / 2] ^= 0x40u;
    auto b = deserialize_archive(bytes);
    SF_CHECK(b.is_error());
    SF_CHECK(b.error_code() == ErrorCode::corrupt);
}

SF_TEST_FN(persistence_truncation_rejected) {
    auto a = make_archive();
    auto blob = serialize_archive(a);
    auto bytes = blob.value();
    bytes.resize(bytes.size() / 2);
    auto b = deserialize_archive(bytes);
    SF_CHECK(b.is_error());
}

SF_TEST_FN(persistence_unknown_version_rejected) {
    auto a = make_archive();
    a.format_version = 999;   // valid checksum, but unknown version
    auto blob = serialize_archive(a);
    auto b = deserialize_archive(blob.value());
    SF_CHECK(b.is_error());
    SF_CHECK(b.error_code() == ErrorCode::unknown_protocol_version);
}

SF_TEST_FN(persistence_filestore_roundtrip) {
    const std::string path = ".sf_persist_test.bin";
    FileStore store(path);
    auto a = make_archive();
    auto blob = serialize_archive(a);
    std::string s(blob.value().begin(), blob.value().end());
    auto sv = store.save(s);
    SF_CHECK(sv.has_value());
    auto loaded = store.load();
    SF_CHECK(loaded.has_value());
    auto b = deserialize_archive(std::vector<std::uint8_t>(loaded.value().begin(), loaded.value().end()));
    SF_CHECK(b.has_value());
    SF_CHECK(b.value() == a);
    std::remove(path.c_str());
    SF_CHECK(true);
}

// ---------------------------------------------------------------------------
// Regression tests: an archive whose length does not match its declared
// contents is rejected even when its checksum is valid, and the serializer
// never produces an archive the decoder would reject.
// ---------------------------------------------------------------------------
static std::uint32_t test_crc32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }
    return ~crc;
}

// Recomputes the trailing checksum over everything before it.
static void reseal(std::vector<std::uint8_t>& blob) {
    const std::size_t n = blob.size() - 4;
    const std::uint32_t crc = test_crc32(blob.data(), n);
    for (std::size_t i = 0; i < 4; ++i) {
        blob[n + i] = static_cast<std::uint8_t>((crc >> (8 * i)) & 0xFFu);
    }
}

SF_TEST_FN(persistence_reseal_matches_the_archive_checksum) {
    // The helper reproduces the format's checksum, so resealed blobs below are
    // rejected for their contents and not for a checksum mismatch.
    auto bytes = serialize_archive(make_archive()).value();
    auto resealed = bytes;
    reseal(resealed);
    SF_CHECK(resealed == bytes);
}

SF_TEST_FN(persistence_trailing_bytes_rejected) {
    auto bytes = serialize_archive(make_archive()).value();
    const std::uint8_t junk[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
    bytes.insert(bytes.end() - 4, junk, junk + 4);   // after the last request, before the checksum
    reseal(bytes);
    auto b = deserialize_archive(bytes);
    SF_CHECK(b.is_error());
    SF_CHECK(b.error_code() == ErrorCode::malformed);
}

SF_TEST_FN(persistence_count_reading_into_the_checksum_rejected) {
    StateArchive a;
    a.epoch = CoordinatorEpoch{1};
    PersistedRequest r;
    r.id = RequestId{1};
    r.sequence = SequenceId{2};
    r.committed = {10u, 11u, 12u};
    a.requests.push_back(r);
    auto bytes = serialize_archive(a).value();
    // Token-count field of the only request: 20-byte header + seven u64 fields.
    const std::size_t tcount_at = 4 + 4 + 8 + 4 + 7 * 8;
    SF_CHECK_EQ(static_cast<std::uint32_t>(bytes[tcount_at]), 3u);
    bytes[tcount_at] = static_cast<std::uint8_t>(4);   // declares one more token than the archive holds
    reseal(bytes);
    auto b = deserialize_archive(bytes);
    SF_CHECK(b.is_error());
}

SF_TEST_FN(persistence_serializer_refuses_what_the_decoder_rejects) {
    StateArchive a;
    a.epoch = CoordinatorEpoch{1};
    PersistedRequest r;
    r.id = RequestId{1};
    r.sequence = SequenceId{2};
    r.committed.assign(1000001u, 7u);   // one past the format's per-request token bound
    a.requests.push_back(r);
    auto over = serialize_archive(a);
    SF_CHECK(over.is_error());
    SF_CHECK(over.error_code() == ErrorCode::out_of_range);

    // At the bound the archive still round-trips.
    a.requests[0].committed.resize(1000000u);
    auto at = serialize_archive(a);
    SF_CHECK(at.has_value());
    auto back = deserialize_archive(at.value());
    SF_CHECK(back.has_value());
    SF_CHECK(back.value() == a);
}