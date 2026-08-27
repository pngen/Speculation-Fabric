# Speculation Fabric - Architecture

## Governing model

The runtime distinguishes two kinds of sequence state:

- Authoritative state. The source of truth. It holds the committed token prefix,
  the authoritative generation, and the authoritative state/KV reference. It
  can only advance through a valid commit.
- Speculative state. Candidate paths that explore ahead. They are proposed,
  verified, and either folded into authoritative state or released. Speculative
  state alone never advances authoritative state.

## Components

- SpeculationFabric. The runtime. Holds per-request authoritative state,
  attempts, branches, proposals, reservations, and instrumentation. It drives
  each speculative cycle and is the only thing that can commit authoritative
  progress.
- SpeculationScheduler. Decides whether a sequence may speculate, what depth and
  how many branches to request, and which proposer/verifier to use. Decisions are
  explicit and inspectable.
- ProposalExecutor / VerificationExecutor. Backends. The CPU and CUDA executors
  are the only places device/framework specifics live. The runtime defines the
  contract; a backend satisfies it.
- Clock. Deadline and latency semantics.
- Persistence. Durable authoritative state.
- Wire protocol. The framed TCP control plane for distributed operation.

## Invariants

The runtime enforces these invariants at every cycle:

1. Authoritative token count never decreases.
2. Authoritative token count advances only through a valid commit of an accepted
   prefix.
3. Speculative token count alone never advances authoritative state.
4. A rejected suffix is never authoritative.
5. A duplicate or stale verification commits nothing.
6. A verification result never applies to the wrong proposal generation.
7. A completion from an earlier attempt never mutates the current attempt.
8. Losing branches cannot later commit.
9. The committed branch becomes uniquely authoritative.
10. Proposal and verification generations are monotonic.
11. Reservations never underflow and are never double-released.
12. Memory accounting never goes negative.
13. Incompatible model pairs never execute.
14. Cancellation prevents future authoritative speculative commits.

## Thread-safety

A single SpeculationFabric instance may be used from many threads. The engine
never invokes an executor, persistence layer, or callback while holding an
internal lock, and never recursively acquires a lock it already holds. Callers
must not invoke methods that re-enter the engine while holding a reference
obtained from a snapshot.
