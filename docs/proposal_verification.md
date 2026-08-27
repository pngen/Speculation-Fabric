# Speculation Fabric - Proposal and verification semantics

## Proposal

A Proposal is a final, immutable candidate. Before finalization it is a plan; a
plan becomes a dispatch, which a proposer executor runs to produce a candidate.
After a proposal is produced and becomes eligible for verification it is never
mutated. A proposal carries:

- proposal id and generation,
- request, sequence, attempt, branch, cycle,
- coordinator epoch and dispatch authority,
- proposer worker identity and boot identity,
- proposer model identity and revision,
- authoritative base generation and the state/KV generation it consumed,
- the candidate token sequence and its depth,
- the tokenizer/vocabulary identity used to interpret token ids,
- the speculative state it produced,
- compute and memory spent (measured).

## Verification

Verification is authoritative. A VerificationExecutor receives the proposal
identity and generation, the authoritative starting state and generation, the
candidate tokens, the proposer and verifier identities, the compatibility
identity, the dispatch authority, and the state/KV reference.

The verifier returns a per-position acceptance result sufficient to determine:

- the accepted prefix length,
- the first rejection point,
- fully accepted / fully rejected,
- verifier failure and retryability,
- a terminal condition where applicable,
- the authoritative next state information,
- measured execution metadata.

## Acceptance

Given proposal A B C D E and a verification accepting A B C and rejecting D:

- commit exactly A B C,
- reject D and E,
- advance authoritative state exactly three steps,
- invalidate speculative descendants based on D and E,
- release unused speculative resources,
- preserve correct next-generation authority.

A full rejection commits nothing. A duplicate verification, a verification for
the wrong proposal generation, or a verification against the wrong
authoritative base generation commits nothing.
