// ---------------------------------------------------------------------------
// State Provenance - spc CLI.
// Commands: register, inspect, explain, ancestry, descendants, deps,
//           check-reuse, invalidate, save, recover, replay, benchmark.
// Supports --json for machine-readable output.
// ---------------------------------------------------------------------------
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "stateprovenance/store.hpp"
#include "stateprovenance/persist.hpp"
#include "stateprovenance/explain.hpp"

using namespace stateprovenance;

static std::uint64_t hx(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }

static std::map<std::string,std::string> parse_args(int argc, char** argv, int from) {
    std::map<std::string,std::string> m;
    for (int i = from; i < argc; ++i) {
        std::string a = argv[i];
        if (a.size() >= 2 && a[0] == '-' && a[1] == '-') {
            std::string k = a.substr(2);
            if (i + 1 < argc && std::string(argv[i+1]) != "--json" && argv[i+1][0] != '-') {
                m[k] = argv[i+1]; ++i;
            } else { m[k] = "1"; }
        }
    }
    return m;
}

static int usage() {
    std::printf("spc: State Provenance CLI\n"
        "  register <file> --subject <hex> [--kind KVState] [--model <hex>] [--parents <hex,..>] [--producer <hex>] [--execution <hex>] [--evidence MEASURED] [--arch sm_120] --provgen <hex> --stategen <hex>\n"
        "  inspect   <file> <subject_hex> [--json]\n"
        "  explain   <file> <subject_hex>\n"
        "  ancestry  <file> <subject_hex>\n"
        "  descend   <file> <subject_hex>\n"
        "  deps      <file> <subject_hex>\n"
        "  check-reuse <file> <subject_hex> [--model <hex>] [--arch <s>] [--dtype <s>]\n"
        "  invalidate <file> <subject_hex> [--reason ModelRevisionChange]\n"
        "  save      <file> <out>\n"
        "  recover   <in> <out>\n"
        "  replay    <file>\n"
        "  benchmark <file> [--n <count>]\n");
    return 1;
}

static std::optional<std::string> publish_from_args(ProvenanceStore& s, const std::map<std::string,std::string>& a) {
    RecordData d;
    if (!a.count("subject")) return "register requires --subject";
    d.provenance_id = ProvenanceId(hx(a.count("pid") ? a.at("pid") : a.at("subject")) * 2);
    d.provenance_generation = ProvenanceGeneration(a.count("provgen") ? hx(a.at("provgen")) : 1);
    d.subject_id = StateId(hx(a.at("subject")));
    d.subject_kind = a.count("kind") ? parse_SubjectKind(a.at("kind")).value_or(SubjectKind::KVState) : SubjectKind::KVState;
    d.state_generation = StateGeneration(a.count("stategen") ? hx(a.at("stategen")) : 1);
    d.producer_id = ProducerId(a.count("producer") ? hx(a.at("producer")) : 0xA);
    d.producer_generation = ProducerGeneration(1);
    d.execution_id = ExecutionId(a.count("execution") ? hx(a.at("execution")) : 0xE1);
    d.attempt_id = AttemptId(0x1); d.attempt_generation = AttemptGeneration(1);
    if (a.count("model")) { d.model_id = ModelId(hx(a.at("model"))); d.model_generation = ModelGeneration(1); }
    if (a.count("arch")) d.architecture = a.at("arch");
    if (a.count("dtype")) d.dtype = a.at("dtype");
    if (a.count("shape")) d.shape = a.at("shape");
    d.evidence = a.count("evidence") ? parse_EvidenceClass(a.at("evidence")).value_or(EvidenceClass::MEASURED) : EvidenceClass::MEASURED;
    d.validity = ValidityState::VALID;
    if (a.count("parents")) {
        std::string p = a.at("parents");
        std::size_t pos = 0;
        while (pos < p.size()) {
            std::size_t comma = p.find(',', pos);
            d.input_states.push_back(StateId(hx(p.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos))));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    auto out = s.publish(d);
    if (!out.ok) return out.error;
    return std::nullopt;
}

static void load_store(const std::string& path, ProvenanceStore& s) {
    auto lr = load_file(path);
    if (lr.snapshot) { s.restore(*lr.snapshot); return; }
    // no persisted snapshot -> start a fresh store with default generations
    s.roll_provenance_generation(ProvenanceGeneration(1));
    s.roll_state_generation(StateGeneration(1));
}

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    if (cmd == "register") {
        if (argc < 3) return usage();
        ProvenanceStore s; load_store(argv[2], s);
        auto a = parse_args(argc, argv, 3);
        auto err = publish_from_args(s, a);
        if (err) { std::fprintf(stderr, "register failed: %s\n", err->c_str()); return 2; }
        auto e = save_file(s.snapshot(), argv[2]);
        if (e) { std::fprintf(stderr, "save failed: %s\n", e->c_str()); return 2; }
        auto id = StateId(hx(a.at("subject")));
        std::printf("registered subject %s (size=%zu)\n", id.to_string().c_str(), s.size());
        return 0;
    }
    if (cmd == "inspect") {
        if (argc < 4) return usage();
        bool json = false; for (int i=3;i<argc;++i) if (std::string(argv[i])=="--json") json=true;
        ProvenanceStore s; load_store(argv[2], s);
        auto rec = s.find(StateId(hx(argv[3])));
        if (!rec) { std::fprintf(stderr, "subject not found\n"); return 2; }
        std::printf("%s\n", json ? explain::record_json(rec->d).c_str() : explain::record_text(rec->d).c_str());
        return 0;
    }
    if (cmd == "explain") { if (argc<4) return usage(); ProvenanceStore s; load_store(argv[2], s); std::printf("%s\n", explain::chain_text(s, StateId(hx(argv[3]))).c_str()); return 0; }
    if (cmd == "ancestry") { if (argc<4) return usage(); ProvenanceStore s; load_store(argv[2], s); auto v=s.ancestors(StateId(hx(argv[3]))); for (auto id:v) std::printf("%s\n", id.to_string().c_str()); return 0; }
    if (cmd == "descend") { if (argc<4) return usage(); ProvenanceStore s; load_store(argv[2], s); auto v=s.descendants(StateId(hx(argv[3]))); for (auto id:v) std::printf("%s\n", id.to_string().c_str()); return 0; }
    if (cmd == "deps") { if (argc<4) return usage(); ProvenanceStore s; load_store(argv[2], s); auto rec=s.find(StateId(hx(argv[3]))); if(!rec){std::fprintf(stderr,"not found\n");return 2;} for (auto& dep: rec->d.dependencies) std::printf("%s %s\n", dep.id.to_string().c_str(), dep.kind.c_str()); return 0; }
    if (cmd == "check-reuse") {
        if (argc < 4) return usage();
        ProvenanceStore s; load_store(argv[2], s);
        auto a = parse_args(argc, argv, 3);
        ReuseRequest req;
        if (a.count("model")) { req.model_id = ModelId(hx(a.at("model"))); req.model_generation = ModelGeneration(1); }
        if (a.count("arch")) req.architecture = a.at("arch");
        if (a.count("dtype")) req.dtype = a.at("dtype");
        auto d = s.check_reuse(StateId(hx(argv[3])), req);
        bool json = a.count("json");
        if (json) {
            std::printf("{\"eligibility\":\"%s\",\"failed_dimensions\":[", name_of(d.eligibility));
            for (std::size_t i=0;i<d.failed_dimensions.size();++i){ if(i) std::printf(","); std::printf("\"%s\"", name_of(d.failed_dimensions[i])); }
            std::printf("],\"reasons\":[");
            for (std::size_t i=0;i<d.reasons.size();++i){ if(i) std::printf(","); std::printf("\"%s\"", d.reasons[i].c_str()); }
            std::printf("]}\n");
        } else std::printf("%s\n", d.summary().c_str());
        return 0;
    }
    if (cmd == "invalidate") {
        if (argc < 4) return usage();
        ProvenanceStore s; load_store(argv[2], s);
        auto a = parse_args(argc, argv, 4);
        auto reason = a.count("reason") ? parse_InvalidationReason(a.at("reason")).value_or(InvalidationReason::OperatorInvalidation) : InvalidationReason::OperatorInvalidation;
        auto out = s.invalidate(InvalidatingSubject{StateId(hx(argv[3])), reason, "cli"});
        if (!out.ok) { std::fprintf(stderr, "invalidate failed: %s\n", out.error.c_str()); return 2; }
        save_file(s.snapshot(), argv[2]);
        std::printf("invalidated %zu subject(s)\n", out.result.affected.size());
        return 0;
    }
    if (cmd == "save") { if (argc < 4) return usage(); ProvenanceStore s; load_store(argv[2], s); auto e=save_file(s.snapshot(), argv[3]); if(e){std::fprintf(stderr,"%s\n",e->c_str());return 2;} std::printf("saved digest=%s\n", s.store_digest(s.snapshot()).c_str()); return 0; }
    if (cmd == "recover") { if (argc < 4) return usage(); auto lr=load_file(argv[2]); if(!lr.snapshot){std::fprintf(stderr,"recover failed: %s\n",lr.error.c_str());return 2;} ProvenanceStore s; s.restore(*lr.snapshot); auto e=save_file(s.snapshot(), argv[3]); if(e){std::fprintf(stderr,"%s\n",e->c_str());return 2;} std::printf("recovered digest=%s\n", s.store_digest(s.snapshot()).c_str()); return 0; }
    if (cmd == "replay") { if (argc < 3) return usage(); ProvenanceStore s; load_store(argv[2], s); auto sn=s.snapshot(); auto b=encode_snapshot(sn); auto dec=decode_snapshot(b.data(), b.size()); if(!dec.snapshot){std::fprintf(stderr,"replay failed\n");return 2;} ProvenanceStore r; r.restore(*dec.snapshot); std::printf("replay digest=%s size=%zu\n", r.store_digest(r.snapshot()).c_str(), r.size()); return 0; }
    if (cmd == "benchmark") {
        if (argc < 3) return usage();
        ProvenanceStore s; load_store(argv[2], s);
        auto a = parse_args(argc, argv, 3);
        int n = a.count("n") ? std::atoi(a.at("n").c_str()) : 1000;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i) {
            RecordData d; d.provenance_id = ProvenanceId(0x100000000 + i); d.provenance_generation = ProvenanceGeneration(1);
            d.subject_id = StateId(0x200000000 + i); d.subject_kind = SubjectKind::KVState; d.state_generation = StateGeneration(1);
            d.producer_id = ProducerId(0xA); d.producer_generation = ProducerGeneration(1); d.execution_id = ExecutionId(0xE1);
            d.model_id = ModelId(0x7); d.model_generation = ModelGeneration(1); d.evidence = EvidenceClass::MEASURED; d.validity = ValidityState::VALID;
            if (i > 0) d.input_states = { StateId(0x200000000 + i - 1) };
            s.publish(d);
        }
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::printf("published %d records in %.2f ms (%.2f us/record); size=%zu\n", n, ms, ms*1000.0/n, s.size());
        return 0;
    }
    return usage();
}
