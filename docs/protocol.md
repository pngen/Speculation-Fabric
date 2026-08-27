# Speculation Fabric - Framed wire protocol

The distributed control plane uses real framed TCP.

## Frame

Every frame is:

- 4-byte little-endian length (the payload byte count),
- the payload.

A hard maximum frame size bounds every frame. A zero-length frame is rejected.

## Message header

A payload is a binary message:

- protocol version: u32
- message type: u32
- u64 field count: u32
- u32 field count: u32
- body length: u32
- u64 fields (8 bytes each, little-endian)
- u32 fields (4 bytes each, little-endian)
- body bytes

The protocol version is currently 1. Message types include Hello,
SubmitRequest, ProposalDispatch, ProposalResult, VerificationDispatch,
VerificationResult, Commit, Reject, Cancel, Info, and Shutdown.

## Lossless identities

All 64-bit identities are encoded as fixed-width little-endian integers. They
are never serialized through floating-point JSON numbers.

## Authority

Every proposal and verification message carries enough authority to reject
stale work: coordinator epoch, worker id, worker boot id, request id, attempt
id, sequence id, proposal id, branch id, proposal generation, verification
generation, authoritative base generation, and dispatch id.

The decoder rejects unknown protocol versions, unknown message types,
oversized frames, truncated frames, zero-length messages, malformed identities,
and impossible candidate depths. Stale epoch, stale boot, obsolete attempt, and
obsolete generation are rejected with structured stale-authority errors.
