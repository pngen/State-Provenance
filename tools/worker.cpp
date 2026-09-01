// ---------------------------------------------------------------------------
// State Provenance - worker/source process for the multiprocess proof.
// argv: <coord_port> <worker_id> <boot_id> <mode> <state_id> [input_id]
//   mode = produce_root | produce_child | register
// After the action the worker stays alive (blocking on recv) so the proof
// harness can kill it as a real OS process.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "stateprovenance/store.hpp"
#include "stateprovenance/protocol.hpp"

using namespace stateprovenance;
using namespace stateprovenance::spnet;

static std::map<std::string, std::string> parse(const std::string& s) {
    std::map<std::string, std::string> m;
    std::size_t pos = 0;
    while (pos < s.size()) {
        std::size_t eq = s.find('=', pos);
        std::size_t sem = s.find(';', pos);
        if (eq == std::string::npos) break;
        std::string k = s.substr(pos, eq - pos);
        std::string v = s.substr(eq + 1, (sem == std::string::npos ? s.size() : sem) - eq - 1);
        m[k] = v;
        if (sem == std::string::npos) break;
        pos = sem + 1;
    }
    return m;
}
static std::uint64_t hx(const std::string& v) { return std::strtoull(v.empty() ? "0" : v.c_str(), nullptr, 16); }

int main(int argc, char** argv) {
    if (argc < 6) { std::fprintf(stderr, "usage: sp_worker <coord_port> <worker_id> <boot_id> <mode> <state_id> [input_id]\n"); return 2; }
    if (!init()) return 1;
    int cport = std::atoi(argv[1]);
    WorkerId wid(hx(argv[2]));
    WorkerBootId boot(hx(argv[3]));
    std::string mode = argv[4];
    StateId sid(hx(argv[5]));
    StateId input = (argc >= 7) ? StateId(hx(argv[6])) : StateId{};

    int conn = connect_socket("127.0.0.1", static_cast<std::uint16_t>(cport), 10000);
    if (conn < 0) { std::fprintf(stderr, "worker connect failed\n"); return 3; }

    // Hello: worker_id u64, boot_id u64
    std::string hello;
    hello.resize(16);
    {
        BinaryWriter w;
        w.u64(wid.get()); w.u64(boot.get());
        hello.assign(reinterpret_cast<const char*>(w.data().data()), w.size());
    }
    if (!send_frame(conn, static_cast<std::uint8_t>(Op::Hello), hello)) return 4;
    std::uint8_t op; std::string resp;
    if (!recv_frame(conn, op, resp)) return 4;
    auto auth = parse(resp);
    std::uint64_t epoch = hx(auth["epoch"]);
    std::uint64_t attempt = hx(auth["attempt"]);
    std::uint64_t attempt_gen = hx(auth["attempt_gen"]);
    std::uint64_t state_gen = hx(auth["state_gen"]);
    std::uint64_t prov_gen = hx(auth["prov_gen"]);
    std::uint64_t model_gen = hx(auth["model_gen"]);

    if (mode == "produce_root" || mode == "produce_child") {
        RecordData d;
        d.provenance_id = ProvenanceId(sid.get());
        d.provenance_generation = ProvenanceGeneration(prov_gen ? prov_gen : 1);
        d.subject_id = sid;
        d.subject_kind = (mode == "produce_child") ? SubjectKind::TensorState : SubjectKind::KVState;
        d.state_generation = StateGeneration(state_gen ? state_gen : 1);
        d.producer_id = ProducerId(0x7000000000000000ull | wid.get());
        d.producer_generation = ProducerGeneration(1);
        d.execution_id = ExecutionId(0x8000000000000000ull | sid.get());
        d.attempt_id = AttemptId(attempt ? attempt : 1);
        d.attempt_generation = AttemptGeneration(attempt_gen ? attempt_gen : 1);
        d.model_id = ModelId(0x9000000000000000ull);
        d.model_generation = ModelGeneration(model_gen ? model_gen : 1);
        d.architecture = "sm_120"; d.compute_capability = "12.0";
        d.evidence = EvidenceClass::MEASURED;
        d.validity = ValidityState::VALID;
        d.coordinator_epoch = epoch;
        d.worker_id = wid; d.worker_boot = boot;
        d.input_states.clear();
        if (mode == "produce_child" && input.valid()) d.input_states.push_back(input);
        else if (mode == "produce_root") { d.input_states.clear(); }
        d.content_digest = "sha256:mock:" + sid.to_string();

        AuthorityClaim c;
        c.worker_id = wid; c.worker_boot = boot;
        c.attempt_id = AttemptId(attempt ? attempt : 1);
        c.attempt_generation = AttemptGeneration(attempt_gen ? attempt_gen : 1);
        c.state_generation = StateGeneration(state_gen ? state_gen : 1);
        c.provenance_generation = ProvenanceGeneration(prov_gen ? prov_gen : 1);
        c.coordinator_epoch = epoch;
        c.dependency_generation = DependencyGeneration(hx(auth["dep_gen"]) ? hx(auth["dep_gen"]) : 0); // absent if 0

        auto payload = encode_publish_payload(d, c);
        if (!send_frame(conn, static_cast<std::uint8_t>(Op::Publish), payload)) return 5;
        if (!recv_frame(conn, op, resp)) return 5;
        std::printf("WORKER_PUBLISH %s %s\n", sid.to_string().c_str(), resp.c_str());
        std::fflush(stdout);
    } else {
        std::printf("WORKER_REGISTER %s\n", boot.to_string().c_str());
        std::fflush(stdout);
    }

    // Stay alive until the harness kills this process or closes the connection.
    std::uint8_t rop; std::string rsp;
    while (recv_frame(conn, rop, rsp)) { /* drain */ }
    close_socket(conn);
    cleanup();
    return 0;
}
