# Speculation Fabric - Validation matrix

The repository validates the runtime at several layers. Every test suite builds
with /W4 /WX and zero warnings in Release and Debug.

## Test suites

- test_error: Result/Error model.
- test_ids: strong identities and id factories.
- test_compat: model-pair compatibility key determinism.
- test_state: state/sequence descriptors and reservation semantics.
- test_lifecycle: the guarded speculative state machine (legal cycles, terminal
  absorption, no duplicate commit, guard blocking).
- test_cpu_executors: deterministic CPU proposer/verifier (full/partial/reject
  acceptance, determinism, prior-step dependence, branch distinctness, verifier
  failure paths).
- test_fabric: the runtime engine (full accept, partial accept + rollback,
  reject-all, authoritative monotonicity, multi-branch single winner,
  cancellation, retry, incompatible-pair rejection).
- test_persistence: persistence round-trip, checksum corruption rejection,
  truncation rejection, unknown-version rejection, file store round-trip.
- test_wire: framed protocol round-trips and strict framing/validation rejection.
- test_cuda (when CUDA is enabled): CUDA proposer/verifier cross-validated
  against the CPU reference on the device, including full/partial/reject
  acceptance and device memory recovery.

## Invariants asserted

The suites continuously assert that authoritative token count never decreases,
advances only through valid commit, that speculative tokens alone never advance
authoritative state, that rejected suffixes never remain authoritative, that
duplicate/stale verifications commit nothing, that losing branches cannot commit,
that generations are monotonic, that reservations never underflow, and that
incompatible model pairs never execute.

## Repeated runs

The full suite is run repeatedly in clean Release and Debug configurations.

## Atomic distributed requirement

The distributed stale-authority closure proof is an atomic requirement at the
validation layer. See docs/protocol.md and the distributed scenario in the
repository for the exact multiprocess scenario.

## Atomic distributed closure scenario

The repository ships a real framed-TCP distributed control plane
(`sf_coordinator`, `sf_worker`, `sf_driver`) and an atomic multiprocess
scenario (`tools/run_distributed_scenario.ps1`) that:

- launches a coordinator, a proposer worker, and a verifier worker as real OS
  processes and establishes proposer/verifier capability over framed TCP,
- submits heterogeneous multi-tenant requests across multiple depths and a
  multi-branch cycle,
- demonstrates real full acceptance, partial-prefix acceptance, rejected-suffix
  rollback, and fresh authoritative commit,
- kills a proposer worker as an actual OS process and restarts it as a new OS
  process with a NEW WorkerBootId (the coordinator detects the reconnection),
- rolls the coordinator epoch,
- replays preserved old-epoch / old-boot / obsolete-attempt / obsolete-
  generation authority and proves each is deterministically rejected, and
- proves that no stale message commits a token or advances authoritative
  generation, that fresh work commits under the current authority, that losing
  branches cannot later commit, and that the coordinator closes with correct
  committed counts for every request.
