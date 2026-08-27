# Speculation Fabric - Benchmark methodology

Benchmarks measure real completed runtime work, never empty loops.

## Measurement

Benchmarks report measured values for throughput and latency. Derived values
such as acceptance ratio are labeled as derived. Estimated economics are labeled
as estimated and are never presented as measured facts.

## Workloads

The benchmark suite exercises completed speculative cycles across proposal
depths (1, 2, 4, 8), branch counts (1, 2, 4), and acceptance profiles (full,
partial, reject). Workload configuration is recorded exactly.

## What is measured

- speculative cycles per second,
- proposals and verifications per second,
- accepted and rejected tokens per second,
- plan / proposal / verification / commit / rollback overhead,
- CPU proposer and verifier throughput,
- CUDA proposer and verifier throughput,
- end-to-end accepted-token throughput.

CUDA benchmarks represent verified, completed work on the device.
