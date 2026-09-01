# State Provenance

State Provenance is a production-grade C++20 systems runtime that answers one
question precisely, and nothing else: **where did this reusable machine-produced
state come from, what exact computation and inputs produced it, what depends on
it, what authority made it current, and is it still valid to reuse now?**

It is not a generic audit log, a database demo, a metadata wrapper, a caching
layer, or a CRUD scaffold. It is the authoritative owner of provenance,
derivation, lineage, dependency, compatibility, generation, authority, and
reuse-eligibility semantics for machine-produced AI state.

State Provenance is header-only, deterministic, thread-safe, and ships as an
installable CMake package.

---

## The Systems Question

For any piece of reusable machine-produced state, State Provenance provides the
authoritative answer to:

- what produced this object
- which exact inputs participated
- which parent objects it derived from
- which model/revision generated it
- which adapter/composition state participated
- which runtime/backend/compiler/toolchain participated
- which architecture/device capability applied
- which policy/configuration influenced production
- which execution attempt produced it
- which generation made it current
- what descendants depend on it
- whether its derivation chain remains valid
- whether the object is safe to reuse under a requested environment
- why reuse is allowed or denied
- whether provenance is complete, partial, reconstructed, reported, derived,
  estimated, or unavailable

---

## Architecture

The runtime is a small set of collaborating, header-only components in the
`stateprovenance` namespace:

| Component | Responsibility |
|---|---|
| `ids.hpp` | Strong typed identities and independently rolled generations. |
| `enums.hpp` | Typed domain vocabulary (subject kinds, evidence classes, validity, reuse, invalidation, compatibility dimensions). |
| `digest.hpp` | Deterministic CRC-32 and 64-bit stable hashing. |
| `record.hpp` | Immutable, finalized provenance records plus a validating builder. |
| `graph.hpp` | Explicit directed derivation DAG with deterministic traversal. |
| `compat.hpp` | Typed compatibility and reuse-eligibility evaluation. |
| `authority.hpp` | Distributed mutation authority and fencing (epoch, boot, attempt, generations). |
| `invalidate.hpp` | Deterministic invalidation propagation with lazy causal chains. |
| `store.hpp` | Thread-safe provenance registry with indexes, status overlay, snapshot/restore. |
| `persist.hpp` | Versioned binary persistence with CRC-32 integrity and corruption/truncation rejection. |
| `protocol.hpp` | Framed TCP transport for the multiprocess authority proof. |
| `explain.hpp` | Deterministic text, JSON, and path explanations plus stable digests. |

The store holds immutable records plus a mutable *status overlay* (current
validity/invalidation) so historical facts are never rewritten. Locks are
reader/writer; no lock is held across file, socket, or hash work.

---

## Provenance Model

A finalized provenance record is immutable. Every semantic change is expressed
as a new record and, where the authority rolls, a new generation. Records carry
subject identity/kind/state generation, provenance identity/generation, producer
and its generation, execution and attempt, creation timestamp, model
identity/revision/generation, adapter and composition identity, runtime/backend,
toolchain, device architecture and compute capability, input state references,
parent provenance references, direct dependencies, compatibility requirements,
policy fingerprint, content digest, authority metadata, evidence classification,
validity state, invalidation reason, and reuse-eligibility state.

Evidence is explicit and never silently promoted: `MEASURED`, `REPORTED`,
`DERIVED`, `RECONSTRUCTED`, `ESTIMATED`, `UNKNOWN`. Missing values remain
missing and later observations never rewrite historical facts.

---

## Identity & Generation Model

Every identity is a distinct strong type (`StateId`, `ProvenanceId`,
`ProducerId`, `ExecutionId`, `AttemptId`, `WorkerId`, `WorkerBootId`,
`DependencyId`, `ArtifactId`, `ModelId`, `AdapterId`, `CompositionId`,
`PolicyId`, `RuntimeId`, `DeviceId`, `BackendId`, `ToolchainId`). Generations
roll independently and are separately typed (`StateGeneration`,
`ProvenanceGeneration`, `ProducerGeneration`, `DependencyGeneration`,
`PolicyGeneration`, `RuntimeGeneration`, `ArtifactGeneration`,`ModelGeneration`,
`AdapterGeneration`, `CompositionGeneration`, `AttemptGeneration`). Ids and
generations are never raw integers or loose strings.

---

## Derivation Graph

An explicit directed DAG over state identities tracks parents, children,
transitive ancestry and descendants, derivation roots, shared ancestors, fan-in
and fan-out, branching derivation, and recomputation lineage. The graph rejects
illegal cycles, self-dependency, duplicate edges, malformed ancestry, and
dangling endpoints. All traversal and topological ordering are deterministic
(sorted by identity).

---

## Compatibility & Reuse Eligibility

Reuse is evaluated against a requested execution environment across typed
dimensions: model identity/revision, tokenizer, adapter identity/revision,
adapter composition, backend/runtime/compiler/toolchain, architecture, compute
capability, ABI, dtype, layout, geometry, shape/kernel specialization, graph
topology, and policy/artifact/residency/dependency generations.

The result is a typed outcome (`ELIGIBLE`, `INELIGIBLE`, `STALE`,
`INVALIDATED`, `INCOMPATIBLE`, `INCOMPLETE_PROVENANCE`, `UNKNOWN`) with the
exact failed dimension(s) and reasons. Every denial is explainable.

---

## Invalidation

Invalidation is deterministic. A subject can become stale/invalid because of
model or adapter revision change, composition change, artifact or dependency
invalidation, runtime/backend or architecture incompatibility, policy generation
change, explicit operator invalidation, corrupted state, or producer generation
rollover. Propagation reaches the transitive descendant closure with exact
causal chains rendered lazily (so impact analysis is linear in the affected set
and edges, not quadratic on depth-heavy graphs); unrelated branches are never
touched.

---

## Persistence

Versioned binary persistence uses deterministic little-endian encoding, an
explicit version, CRC-32 integrity, bounded lengths, and atomic durable
replacement. It rejects corruption, truncation, trailing garbage, duplicate
identities, invalid enums, invalid generations, malformed references, and
cycles. Recovery reconstructs records, lineage, dependencies, generations,
validity/authority state, and compatibility metadata, and reproduces stable
digests. The sequence of newlines, tabs, and control characters in `persist.hpp`
is fully deterministic.

---

## Distributed Authority

Mutation authority is fenced. Claims carry `CoordinatorEpoch`, `WorkerBootId`,
`AttemptId`/`AttemptGeneration`, `StateGeneration`, `ProvenanceGeneration`, and
the domain generations. Stale authority is rejected deterministically, so old
work never becomes current after an epoch roll, worker restart, attempt retry,
generation rollover, or revision change.

A **real multiprocess proof** (`sp_mp_proof`) drives a real coordinator OS
process plus worker OS processes over framed TCP: worker A produces valid
provenance, is killed as a real OS process, the coordinator epoch rolls, worker
A restarts with a fresh `WorkerBootId`, seven stale mutations (stale epoch, boot,
attempt id, attempt generation, state generation, provenance generation,
dependency generation) are replayed over the real transport and rejected (7/7),
authoritative provenance is unchanged, fresh work is accepted under a new
provenance generation, the graph is saved, the coordinator is restarted and
recovers from disk, and the **exact same stable provenance/evidence digest** is
reproduced (accepted=2, rejected=7, accounting exact).

---

## CUDA Validation

The CUDA proof (`sp_cuda_proof`) runs on the real **NVIDIA GeForce RTX 5090**
(compute capability **12.0 / sm_120**) using **CUDA 13.1**: host input to
`cudaMalloc` to H2D to a real CUDA kernel to derived device state to D2H
verification against a CPU reference to provenance publication to reuse
eligibility to dependency/revision mutation to reuse rejection to recomputation
under a new generation to successful verification to teardown to exact device
memory recovery. The device is confirmed at runtime to be compute capability
12.0 before any result is accepted.

---

## Explainability & CLI

The `spc` CLI supports `register`, `inspect`, `explain`, `ancestry`, `descend`,
`deps`, `check-reuse`, `invalidate`, `save`, `recover`, `replay`, and `benchmark`,
with text and JSON output. The C++ API exposes deterministic human-readable text,
JSON, path/graph explanations, and stable digests.

---

## Examples

Thirteen runnable host examples plus two proof executables:

1. Simple derivation
2. Multi-parent (diamond) derivation
3. Model-revision provenance
4. Adapter/composition provenance
5. Compiled-kernel / generated-state provenance
6. Reuse eligibility
7. Invalidation propagation
8. Generation rollover
9. Persistence / recovery
10. Deterministic replay
11. Concurrent ingestion
12. Authority fencing
13. CUDA-generated state provenance (host view; the real proof is `sp_cuda_proof`)

`sp_mp_proof` demonstrates the distributed authority fencing scenario.

---

## Build, Install, Use

Requirements: a C++20 compiler (MSVC on Windows), CMake 3.21+, and CUDA 13.1 plus
an sm_120 GPU only if you build the CUDA proof.

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    build\Release\stateprovenance_tests.exe
    build\Release\sp_mp_proof.exe build\Release\sp_coordinator.exe build\Release\sp_worker.exe <snapshot>
    build\Release\sp_cuda_proof.exe
    build\Release\spc.exe
    build\Release\sp_benchmark.exe

Install and use from a downstream package:

    cmake --install build --config Release --prefix <prefix>

    # downstream CMakeLists.txt
    find_package(StateProvenance CONFIG REQUIRED)
    target_link_libraries(myapp PRIVATE StateProvenance::stateprovenance)

The exported target carries include directories, the C++20 feature requirement,
and warning hygiene. An independent downstream consumer is validated after
installation.

---

## Test Suite & Validation Results

The deterministic test suite covers strong IDs, generations, immutable records,
graph construction/traversal, cycle/duplicate/dangling rejection, compatibility
and reuse eligibility (including incomplete provenance), authority fencing
across every stale dimension and generation rollover, invalidation propagation
and determinism, persistence round-trip, corruption/truncation/trailing-garbage/
duplicate-ID/invalid-enum/malformed rejection, threaded concurrency
(publication, lookup, traversal during mutation, invalidation, generation
rollover, reuse, persistence during reads), and adversarial fuzz/property tests
with a fixed printed seed.

Current closure on a real NVIDIA GeForce RTX 5090: **30 test cases, 441 checks,
0 failures**; multiprocess authority proof **PASS**; CUDA proof **PASS**;
downstream consumer **PASS**; both Release and Debug build cleanly at `/W4 /WX`
with zero compiler warnings.

---

## Benchmark Summary

All measurements are for completed, useful operations (deterministic seed).

| Operation | Workload | Time |
|---|---|---|
| Provenance record creation + publication | 5,000 records | 6.8 ms (1.4 us/record) |
| Indexed lookup (StateId) | 5,000 lookups | 0.5 ms |
| Reuse-eligibility evaluation | 200 sampled evals | 110 ms |
| Parent/child traversal | 2,000 pairs | 0.2 ms |
| Ancestry + descendant traversal | ~750k nodes / 100 traversals | 104 ms |
| Invalidation impact analysis | 50 invalidations of 5k | 132 ms |
| Deterministic serialization | 50 x 5,000 records | 164 ms |
| Persistence save (durable) | 5,000 records | 7.5 ms |
| Recovery (load + restore) | 5,000 records | 27.7 ms |
| Replay (encode + decode + restore) | 5,000 records | 11.7 ms |
| Concurrent ingestion | 4,000 records / 4 threads | 8.5 ms |

---

## Limitations

- The header-only runtime is validated on MSVC (Windows) / CUDA 13.1. Other
  toolchains are not claimed as validated.
- The CUDA proof is validated on a single real RTX 5090 (sm_120). Synthetic
  device-topology scenarios are labelled synthetic; no multi-GPU validation is
  claimed.
- Invalidated descendants are always in the transitive closure of an invalid
  subject; a branch that should be reused despite one invalid parent must be
  explicitly recomputed rather than automatically rescued.
- The derived-state content digest is a caller-supplied integrity hash; State
  Provenance does not recompute contents.

---

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.