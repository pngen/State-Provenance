#pragma once
// ---------------------------------------------------------------------------
// State Provenance - shared example helpers.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <string>

#include "stateprovenance/store.hpp"
#include "stateprovenance/persist.hpp"
#include "stateprovenance/explain.hpp"
#include "stateprovenance/compat.hpp"

using namespace stateprovenance;

inline RecordData ex_base(StateId sid, ProvenanceId pid, StateGeneration sg = StateGeneration(1),
                          ProvenanceGeneration pg = ProvenanceGeneration(1)) {
    RecordData d;
    d.provenance_id = pid; d.provenance_generation = pg;
    d.subject_id = sid; d.subject_kind = SubjectKind::KVState;
    d.state_generation = sg;
    d.producer_id = ProducerId(0xA); d.producer_generation = ProducerGeneration(1);
    d.execution_id = ExecutionId(0xE1); d.attempt_id = AttemptId(0x1); d.attempt_generation = AttemptGeneration(1);
    d.model_id = ModelId(0x7); d.model_generation = ModelGeneration(1);
    d.architecture = "sm_120"; d.abi = "sm_120"; d.dtype = "float16"; d.layout = "rowmajor"; d.shape = "1024x4096";
    d.evidence = EvidenceClass::MEASURED; d.validity = ValidityState::VALID;
    return d;
}
