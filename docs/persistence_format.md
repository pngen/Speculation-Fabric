# Speculation Fabric - Persistence binary format

The persistence format is a versioned, checksummed, deterministic binary
sequence. All integers are little-endian, fixed width; 64-bit identities are
lossless.

## Layout

- Magic: 4 bytes 'S', 'F', 'A', 'R'
- Format version: u32 (currently 1)
- Coordinator epoch: u64
- Request count: u32
- For each request:
  - request id: u64
  - sequence id: u64
  - attempt id: u64
  - epoch: u64
  - authoritative generation: u64
  - authoritative state id: u64
  - authoritative state generation: u64
  - committed token count: u32
  - committed tokens: u32 each
- CRC-32 (IEEE) over every byte before the trailing 4-byte checksum

## Validation

The decoder rejects unknown versions, truncation (any read past the end),
corruption (checksum mismatch), counts that exceed strict bounds, and trailing
bytes.

## Recovery semantics

Recovery reconstructs an equivalent authoritative state. Formerly in-flight
proposal and verification work is never treated as automatically authoritative.
Recovery never double-commits accepted tokens, never resurrects rejected
branches, never restores stale worker authority as current, never double-counts
reservations, and never loses committed authoritative progress.
