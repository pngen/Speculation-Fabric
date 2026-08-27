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