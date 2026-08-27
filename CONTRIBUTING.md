# Contributing to Speculation Fabric

Speculation Fabric accepts contributions from individuals and organizations on
the terms of the Apache License 2.0 without requiring a Contributor License
Agreement (CLA).

## Process

1. Open an issue describing the change and its motivation.
2. Fork and make changes on a feature branch.
3. Add or update tests that exercise real runtime behavior.
4. Build with the strict warning settings (\/W4 /WX on MSVC) and ensure zero
   warnings in Release and Debug.
5. Run the full test suite.
6. Submit a pull request.

## Style

- C++20, vendor-neutral.
- No inference framework types in the runtime semantics.
- Use Result<T> for ordinary control flow; never throw for an expected
  rejection.
- Keep every 64-bit identity lossless (never through floating-point JSON).
- Keep the speculative state machine explicit and guarded.
- Describe any new semantic in the documentation set.

## Testing

New behavior must be backed by real, observable tests. Do not stub, mock, or
simulate a capability that is required to be real. The validation matrix in
docs/validation.md describes the required layers.
