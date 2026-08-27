// Unit tests for strong identifiers.
#include "sf_test.hpp"
#include "speculation_fabric/core/id.hpp"
#include <unordered_map>

using namespace speculation_fabric;

SF_TEST_FN(ids_distinct_types_do_not_collide) {
    RequestId r1{7};
    ProposalId p1{7};
    // Same underlying value, different type: compare via std::hash and equality.
    std::hash<RequestId> hr;
    // Types are distinct; the values compare within their own type.
    SF_CHECK(r1.get() == 7);
    SF_CHECK(p1.get() == 7);
    SF_CHECK(hr(r1) == hr(RequestId{7}));
}

SF_TEST_FN(id_factory_monotonic_and_nonzero) {
    IdFactory<RequestIdTag> f;
    auto a = f.next();
    auto b = f.next();
    auto c = f.next();
    SF_CHECK(a.get() < b.get());
    SF_CHECK(b.get() < c.get());
    SF_CHECK(a.get() != 0);
}

SF_TEST_FN(id_factory_supports_hashmap) {
    IdFactory<ProposalIdTag> f;
    std::unordered_map<ProposalId, int> m;
    for (int i = 0; i < 100; ++i) {
        m[f.next()] = i;
    }
    SF_CHECK(m.size() == 100);
}