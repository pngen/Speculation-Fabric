# Speculation Fabric - Model-pair compatibility

A speculative pair is never considered valid merely because two model names are
supplied. Compatibility is a typed, deterministic decision.

## Key

A ModelPairCompatibilityKey pairs the proposer model identity, the verifier
model identity, and the candidate protocol version. The same key always yields
the same decision.

## Checks

- Tokenizer/vocabulary identity must match exactly. A mismatch is a correctness
  rejection and is never silently coerced.
- Vocabulary sizes must agree.
- Adapter stacks must match (either both present and identical, or both absent).
- Executor kind must be compatible (same kind, or either is Unknown).
- Candidate protocol versions must agree when set.
- The key protocol version must be non-zero.

## Decision

The decision carries a compatibility flag and a list of reasons. An
incompatibility is explainable: each reason has a stable code and a
human-readable detail. The decision is cached by key.
