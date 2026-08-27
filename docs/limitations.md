# Speculation Fabric - Limitations

This document records the actual, proven limitations of the implementation. It
does not describe the runtime's intended future behavior.

## Synthetic models

The CPU and CUDA proposer/verifier executors run a bounded, deterministic
synthetic model (a fixed-width integer recurrence seeded from the authority
envelope). It is not inference on a trained neural network. This is deliberate:
it makes proposal generation and verification reproducible, real numerical work
without a model dependency. It does not represent the accuracy or behavior of
any particular production model.

## Opaque state / KV-cache

The runtime models state references (identity and generation) and the memory
reservations around them. It does not implement an external KV-cache runtime.
State contents are passed through typed interfaces and are treated as opaque.

## CUDA scope

- The CUDA proposer and verifier select device 0 (the RTX 5090 on the reference
  system). They do not enumerate or balance across a multi-GPU topology.
- The proposer and verifier kernels each launch one block of 16 threads over the
  synthetic state words. They are bounded and correct but not maximally
  parallel, and are not a substitute for a production inference kernel.
- CUDA is compiled only when a CUDA toolkit is found; the CPU path is always
  available.

## In-process cycle execution

The engine exposes a synchronous run_cycle that drives one speculative cycle
through proposal and verification. Multi-request concurrency is exercised
through the test suite and the runtime's thread-safe registry, but a single
run_cycle call is sequential per request.

## Economics provenance

Speculative economics are tracked from the executors and the runtime. Values
are explicitly labeled as measured, derived, configured, or estimated. Estimated
latency savings and losses are not presented as measured facts.

## Attempt and cycle model

The runtime keeps per-request attempt counters and per-cycle generation
counters. Retries always use a new attempt identity. A speculatively-completed
cycle is reset to Ready so a request can continue decoding; the request itself
is terminal only when completed, cancelled, or failed.
