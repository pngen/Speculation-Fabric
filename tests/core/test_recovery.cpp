// Recovery: rebuild equivalent authoritative state from archives at stages.
#include "sf_test.hpp"
#include "speculation_fabric/core/persistence.hpp"
using namespace speculation_fabric;

static StateArchive archive_for(std::uint32_t stage) {
    StateArchive a; a.epoch = CoordinatorEpoch{1};
    PersistedRequest pr;
    pr.id = RequestId{1}; pr.sequence = SequenceId{2};
    pr.attempt = AttemptId{1}; pr.epoch = CoordinatorEpoch{1};
    pr.auth_gen = AuthGeneration{stage}; pr.auth_state.id = StateId{50};
    pr.auth_state.generation = StateGeneration{stage};
    // stage 0 = pre-proposal; stage 3 = partially accepted; stage 5 = post-commit.
    for (std::uint32_t i = 0; i < stage; ++i) pr.committed.push_back(1000 + i);
    a.requests.push_back(pr);
    return a;
}

SF_TEST_FN(recovery_pre_proposal_state) {
    auto blob = serialize_archive(archive_for(0));
    auto a2 = deserialize_archive(blob.value());
    SF_CHECK(a2.has_value());
    SF_CHECK_EQ(a2.value().requests[0].committed.size(), 0u);
    SF_CHECK_EQ(a2.value().requests[0].auth_gen.get(), 0u);
}

SF_TEST_FN(recovery_partial_accept_state) {
    auto blob = serialize_archive(archive_for(3));
    auto a2 = deserialize_archive(blob.value());
    SF_CHECK(a2.has_value());
    SF_CHECK_EQ(a2.value().requests[0].committed.size(), 3u);
    SF_CHECK_EQ(a2.value().requests[0].auth_gen.get(), 3u);
    // Rejected speculative descendants are not present: only authoritative tokens.
    SF_CHECK(a2.value().requests[0].committed == std::vector<std::uint32_t>({1000u,1001u,1002u}));
}

SF_TEST_FN(recovery_post_commit_state) {
    auto blob = serialize_archive(archive_for(5));
    auto a2 = deserialize_archive(blob.value());
    SF_CHECK(a2.has_value());
    SF_CHECK_EQ(a2.value().requests[0].committed.size(), 5u);
    SF_CHECK_EQ(a2.value().requests[0].auth_state.generation.get(), 5u);
}

SF_TEST_FN(recovery_never_double_commits) {
    // Serialize post-commit twice; deserialize yields identical committed length.
    auto a = archive_for(5);
    auto b1 = deserialize_archive(serialize_archive(a).value());
    auto b2 = deserialize_archive(serialize_archive(b1.value()).value());
    SF_CHECK(b1.value().requests[0].committed == b2.value().requests[0].committed);
}
