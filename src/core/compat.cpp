// Speculation Fabric — model-pair compatibility evaluation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "speculation_fabric/core/compat.hpp"

namespace speculation_fabric {

namespace {
void add_reason(std::vector<CompatReason>& out, std::string code, std::string detail,
                bool fatal = true) {
    out.push_back(CompatReason{std::move(code), std::move(detail), fatal});
}
}  // namespace

ModelPairCompatibilityDecision evaluate_compatibility(const ModelPairCompatibilityKey& key) {
    ModelPairCompatibilityDecision d;
    d.canonical_key = key.canonical();
    d.cache_hit = false;

    const auto& p = key.proposer;
    const auto& v = key.verifier;
    bool ok = true;

    // Tokenizer / vocabulary identity is a correctness requirement.
    if (!(p.tokenizer == v.tokenizer)) {
        add_reason(d.reasons, "tokenizer_mismatch",
                   "proposer tokenizer '" + p.tokenizer.name +
                       "' (id " + p.tokenizer.id.str() +
                       ") differs from verifier tokenizer '" + v.tokenizer.name +
                       "' (id " + v.tokenizer.id.str() + ")");
        ok = false;
    } else if (p.tokenizer.id.is_null()) {
        add_reason(d.reasons, "missing_tokenizer_identity",
                   "proposer/vocabulary identity is unset");
        ok = false;
    }
    if (p.tokenizer.vocab_size != v.tokenizer.vocab_size) {
        add_reason(d.reasons, "vocabulary_size_mismatch",
                   std::to_string(p.tokenizer.vocab_size) + " != " +
                       std::to_string(v.tokenizer.vocab_size) + " vocab entries");
        ok = false;
    }

    // Adapter stacks must match: differing adapters change token semantics.
    if (p.adapter.has_value() != v.adapter.has_value()) {
        add_reason(d.reasons, "adapter_stack_mismatch",
                   "one model has an adapter stack and the other does not");
        ok = false;
    } else if (p.adapter && v.adapter && !(*p.adapter == *v.adapter)) {
        add_reason(d.reasons, "adapter_mismatch",
                   "adapter '" + p.adapter->name + "' differs from '" + v.adapter->name + "'");
        ok = false;
    }

    // Executor / device numerical compatibility.
    if (!same_executor_kind(p.executor.kind, v.executor.kind)) {
        add_reason(d.reasons, "executor_kind_mismatch",
                   "executors run on different device kinds");
        ok = false;
    }
    if (p.executor.protocol_version != 0 && v.executor.protocol_version != 0 &&
        p.executor.protocol_version != v.executor.protocol_version) {
        add_reason(d.reasons, "candidate_protocol_mismatch",
                   "proposer protocol " + std::to_string(p.executor.protocol_version) +
                       " != verifier protocol " + std::to_string(v.executor.protocol_version));
        ok = false;
    }
    // A pair cannot reference an unset executor/protocol version.
    if (key.protocol_version == 0) {
        add_reason(d.reasons, "missing_protocol_version",
                   "key protocol version must be >= 1");
        ok = false;
    }

    // Model revisions participate, but a revision mismatch alone is not fatal
    // when the tokenizer identity matches.
    if (p.model == v.model && p.revision != v.revision) {
        add_reason(d.reasons, "revision_mismatch",
                   "same model id at different revisions (" + p.revision.str() +
                       " vs " + v.revision.str() + "): tokens remain interpretable only if "
                       "the vocabulary is unchanged (it is)",
                   /*fatal=*/false);
    }

    d.compatible = ok;
    return d;
}

}  // namespace speculation_fabric
