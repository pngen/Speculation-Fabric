// Unit tests for model pair compatibility.
#include "sf_test.hpp"
#include "speculation_fabric/core/compat.hpp"
#include "speculation_fabric/core/error.hpp"
#include <unordered_map>

using namespace speculation_fabric;

static ModelIdentity make_model(TokenizerId tok) {
    ModelIdentity m;
    m.model = ModelId{101};
    m.revision = Revision{1};
    m.tokenizer.id = tok;
    m.tokenizer.name = "tok" + std::to_string(tok.get());
    m.tokenizer.vocab_size = 1000;
    m.executor.id = ExecutorId{1};
    m.executor.name = "cpu";
    m.executor.kind = ExecutorKind::CPU;
    m.name = "m";
    return m;
}

SF_TEST_FN(compat_key_canonical_distinguishes_models) {
    ModelPairCompatibilityKey k1{make_model(TokenizerId{1}), make_model(TokenizerId{2}), 1};
    ModelPairCompatibilityKey k2{make_model(TokenizerId{1}), make_model(TokenizerId{2}), 1};
    ModelPairCompatibilityKey k3{make_model(TokenizerId{1}), make_model(TokenizerId{3}), 1};
    SF_CHECK(k1 == k2);
    SF_CHECK(k1.canonical() == k2.canonical());
    SF_CHECK(k1.canonical() != k3.canonical());
}
