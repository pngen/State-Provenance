# Contributing to State Provenance

Thank you for your interest in contributing to State Provenance. State
Provenance is an open-source project maintained by Summon Software Labs and
distributed under the Apache License, Version 2.0.

## Contributor License

There is **no Contributor License Agreement (CLA)**. By submitting a
contribution to this repository you agree that your contribution is provided
under the **Apache License, Version 2.0** (the same license as the project),
as described in Section 5 of the license ("Submission of Contributions").
You retain the copyright to your contributions.

## How to contribute

1. **Fork and branch.** Work on a clearly named branch off `main`.
2. **Follow the engineering standards.** Keep the public API stable, the
   build clean at /W4 /WX with zero compiler warnings on MSVC, and the
   deterministic, concurrency, persistence, and property tests passing.
3. **Add tests.** Every new behavior must be covered by deterministic tests
   that run to completion and do not depend on wall-clock limits.
4. **Run the full closure** (Release and Debug builds, the test suites, the
   multiprocess authority proof, and the CUDA proof where hardware is
   available) before opening a pull request.
5. **Do not fabricate results.** If a capability cannot be demonstrated on
   the available toolchain or hardware, narrow the claim in the documentation
   rather than claiming success.

## Documentation

Public README content must describe only the actual runtime and public
engineering evidence. Do not document internal workflow directives, harness
requirements, or agent instructions. The README ends with the License section.

## Code of conduct

Be respectful, constructive, and evidence-driven in all interactions.

## Licensing

By contributing you agree to the attribution terms in the project NOTICE file.
State Provenance transmits no telemetry and makes no network calls on its own.