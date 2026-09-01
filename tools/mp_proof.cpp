// ---------------------------------------------------------------------------
// State Provenance - multiprocess distributed authority proof harness.
// Spawns a real coordinator OS process plus worker OS processes over framed
// TCP, performs the full atomic authority/fencing scenario, and closes with
// exact accounting.  See the narrative comments in each step.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "stateprovenance/store.hpp"
#include "stateprovenance/persist.hpp"
#include "stateprovenance/protocol.hpp"

using namespace stateprovenance;
using namespace stateprovenance::spnet;

static int g_failures = 0;
static std::string g_winlog;
#define REQUIRE(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "PROOF FAIL: %s\n", msg); ++g_failures; } else { std::printf("  ok: %s\n", msg); std::fflush(stdout); } } while(0)

struct Proc { HANDLE h = NULL; HANDLE read = NULL; DWORD pid = 0; };

static Proc spawn(const std::string& exe, const std::string& args) {
    Proc p;
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return p;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    std::string cmdline = "\"" + exe + "\" " + args;
    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmd(cmdline.begin(), cmdline.end()); cmd.push_back('\0');
    if (!CreateProcessA(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) { CloseHandle(rd); CloseHandle(wr); return p; }
    CloseHandle(wr);
    p.h = pi.hProcess; p.pid = pi.dwProcessId; p.read = rd; CloseHandle(pi.hThread);
    return p;
}

static std::string read_until(Proc& p, const std::string& marker) {
    std::string acc; char buf[256]; DWORD got = 0;
    while (acc.find(marker) == std::string::npos) {
        if (!p.read || !ReadFile(p.read, buf, sizeof(buf), &got, nullptr) || got == 0) break;
        acc.append(buf, got);
    }
    return acc;
}

static void kill(Proc& p) {
    if (p.h) { TerminateProcess(p.h, 0); WaitForSingleObject(p.h, 3000); CloseHandle(p.h); p.h = nullptr; }
    if (p.read) { CloseHandle(p.read); p.read = nullptr; }
}

class Control {
public:
    void connect_to(int coord_port) { sock_ = connect_socket("127.0.0.1", static_cast<std::uint16_t>(coord_port), 10000); }
    std::string op(std::uint8_t opcode, const std::string& payload = "") {
        if (sock_ < 0) return "err:no-socket";
        send_frame(sock_, opcode, payload);
        std::uint8_t op; std::string resp;
        if (!recv_frame(sock_, op, resp)) return "err:recv";
        return resp;
    }
    void close() { if (sock_ >= 0) { close_socket(sock_); sock_ = -1; } }
private:
    int sock_ = -1;
};

static std::string get_field(const std::string& s, const std::string& key) {
    std::size_t q = s.find(key + "=");
    if (q == std::string::npos) return "";
    q += key.size() + 1; std::size_t e = s.find(';', q);
    return s.substr(q, e == std::string::npos ? std::string::npos : e - q);
}

static void query(Control& c, StateId id, std::string& full, bool& present) {
    BinaryWriter w; w.u64(id.get());
    std::string pl(reinterpret_cast<const char*>(w.data().data()), w.size());
    full = c.op(static_cast<std::uint8_t>(Op::Query), pl);
    present = full.rfind("present", 0) == 0;
}

// Build a VALID candidate record for a stale/fresh publish.
static RecordData make_candidate(StateId sid, SubjectKind kind, StateId input,
                                 const AuthorityClaim& cl, int worker_hi) {
    RecordData d;
    d.provenance_id = ProvenanceId(sid.get());
    d.provenance_generation = cl.provenance_generation;
    d.subject_id = sid;
    d.subject_kind = kind;
    d.state_generation = cl.state_generation;
    d.producer_id = ProducerId(0x7000000000000000ull | static_cast<std::uint64_t>(worker_hi) | (sid.get() & 0xFF));
    d.producer_generation = ProducerGeneration(1);
    d.execution_id = ExecutionId(0x8000000000000000ull | sid.get());
    d.attempt_id = cl.attempt_id;
    d.attempt_generation = cl.attempt_generation;
    d.model_id = ModelId(0x9000000000000000ull);
    d.model_generation = cl.model_generation ? cl.model_generation.value() : ModelGeneration(1);
    d.architecture = "sm_120"; d.compute_capability = "12.0"; d.abi = "aarch64";
    d.dtype = "float16"; d.layout = "rowmajor"; d.shape = "1024x4096";
    d.evidence = EvidenceClass::MEASURED; d.validity = ValidityState::VALID;
    d.coordinator_epoch = cl.coordinator_epoch;
    d.worker_id = cl.worker_id; d.worker_boot = cl.worker_boot;
    d.content_digest = "sha256:mock:" + sid.to_string();
    if (input.valid()) d.input_states.push_back(input);
    return d;
}

static bool expect_reject(Control& c, const RecordData& cand, const AuthorityClaim& stale,
                          const std::string& dim) {
    std::string payload = encode_publish_payload(cand, stale);
    std::string r = c.op(static_cast<std::uint8_t>(Op::PublishStale), payload);
    bool ok = (r.rfind("err:", 0) == 0);
    if (ok && !dim.empty()) ok = r.find(dim) != std::string::npos;
    std::printf("    stale %-22s -> %s\n", (dim.empty() ? "?" : dim).c_str(), r.c_str());
    std::fflush(stdout);
    (void)g_winlog;
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: sp_mp_proof <coordinator_exe> <worker_exe> <snapshot_path>\n"); return 2; }
    if (!init()) { std::fprintf(stderr, "WSAStartup failed\n"); return 1; }
    std::string coord_exe = argv[1], worker_exe = argv[2], snapshot = argv[3];

    std::printf("== State Provenance multiprocess authority proof ==\n"); std::fflush(stdout);

    // ----- 1. Coordinator as a real OS process -----
    Proc coord = spawn(coord_exe, "0");
    if (!coord.h) { std::fprintf(stderr, "coordinator spawn failed\n"); return 3; }
    std::string l = read_until(coord, "LISTENING");
    int port = std::atoi(l.substr(l.find("LISTENING") + 9).c_str());
    std::printf("[1] coordinator listening on %d\n", port); std::fflush(stdout);
    REQUIRE(port > 0, "coordinator bound a real TCP port");

    Control ctl; ctl.connect_to(port);
    std::string init = "state_gen=1;prov_gen=1;dep_gen=1;model_gen=1;attempt=1;attempt_gen=1;policy_gen=1";
    REQUIRE(ctl.op(static_cast<std::uint8_t>(Op::SetAuthority), init) == "ok",
            "coordinator accepted initial authority setup");

    // ----- 2. Worker A produces state S1 with valid provenance -----
    StateId S1(0x1001);
    WorkerId WA(0xA001);
    WorkerBootId BA1(0xB0001);
    Proc workerA = spawn(worker_exe, std::to_string(port) + " " + WA.to_string() + " " +
                       BA1.to_string() + " produce_root " + S1.to_string());
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    std::string q1; bool p1;
    query(ctl, S1, q1, p1);
    std::printf("[2] worker A produced %s -> %s\n", S1.to_string().c_str(), q1.c_str()); std::fflush(stdout);
    REQUIRE(p1, "worker A produced state S1 with valid provenance");
    std::string s1_digest_captured = get_field(q1, "derivation_digest");

    // ----- 3. Kill worker A as a real OS process -----
    kill(workerA);
    std::printf("[3] killed worker A OS process (pid=%lu)\n", (unsigned long)workerA.pid); std::fflush(stdout);
    REQUIRE(workerA.h == nullptr, "worker A terminated as a real OS process");

    // ----- 4. Coordinator epoch rolls -----
    std::string er = ctl.op(static_cast<std::uint8_t>(Op::RollEpoch));
    REQUIRE(er.rfind("epoch=2", 0) == 0, "coordinator epoch rolled to 2");
    // Also roll the dependency generation to simulate dependency invalidation.
    std::string dep2 = "state_gen=1;prov_gen=1;dep_gen=2;model_gen=1;attempt=1;attempt_gen=1;policy_gen=1";
    REQUIRE(ctl.op(static_cast<std::uint8_t>(Op::SetAuthority), dep2) == "ok", "dependency generation rolled to 2");

    // ----- 5. Worker A restarts with a fresh WorkerBootId -----
    WorkerBootId BA2(0xB0002);
    Proc workerA2 = spawn(worker_exe, std::to_string(port) + " " + WA.to_string() + " " +
                        BA2.to_string() + " register 0");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::printf("[5] restarted worker A with fresh WorkerBootId %s\n", BA2.to_string().c_str()); std::fflush(stdout);

    // ----- 6. Replay stale mutations over the REAL transport -----
    std::printf("[6] replaying stale authority over real TCP transport\n"); std::fflush(stdout);
    int rejected = 0;
    AuthorityClaim cur;
    cur.worker_id = WA; cur.worker_boot = BA2;
    cur.attempt_id = AttemptId(1); cur.attempt_generation = AttemptGeneration(1);
    cur.state_generation = StateGeneration(1); cur.provenance_generation = ProvenanceGeneration(1);
    cur.coordinator_epoch = 2;
    cur.dependency_generation = DependencyGeneration(2);
    // (a) stale epoch
    { AuthorityClaim st = cur; st.coordinator_epoch = 1;
      RecordData cd = make_candidate(StateId(0x2001), SubjectKind::KVState, StateId{}, st, 1);
      if (expect_reject(ctl, cd, st, "CoordinatorEpoch")) ++rejected; }
    // (b) stale WorkerBootId
    { AuthorityClaim st = cur; st.worker_boot = BA1;
      RecordData cd = make_candidate(StateId(0x2002), SubjectKind::KVState, StateId{}, st, 2);
      if (expect_reject(ctl, cd, st, "WorkerBootId")) ++rejected; }
    // (c) stale AttemptId
    { AuthorityClaim st = cur; st.attempt_id = AttemptId(999);
      RecordData cd = make_candidate(StateId(0x2003), SubjectKind::KVState, StateId{}, st, 3);
      if (expect_reject(ctl, cd, st, "AttemptId")) ++rejected; }
    // (d) stale AttemptGeneration
    { AuthorityClaim st = cur; st.attempt_generation = AttemptGeneration(999);
      RecordData cd = make_candidate(StateId(0x2004), SubjectKind::KVState, StateId{}, st, 4);
      if (expect_reject(ctl, cd, st, "AttemptGeneration")) ++rejected; }
    // (e) stale StateGeneration
    { AuthorityClaim st = cur; st.state_generation = StateGeneration(999);
      RecordData cd = make_candidate(StateId(0x2005), SubjectKind::KVState, StateId{}, st, 5);
      if (expect_reject(ctl, cd, st, "StateGeneration")) ++rejected; }
    // (f) stale ProvenanceGeneration
    { AuthorityClaim st = cur; st.provenance_generation = ProvenanceGeneration(999);
      RecordData cd = make_candidate(StateId(0x2006), SubjectKind::KVState, StateId{}, st, 6);
      if (expect_reject(ctl, cd, st, "ProvenanceGeneration")) ++rejected; }
    // (g) stale DependencyGeneration
    { AuthorityClaim st = cur; st.dependency_generation = DependencyGeneration(1);
      RecordData cd = make_candidate(StateId(0x2007), SubjectKind::KVState, StateId{}, st, 7);
      if (expect_reject(ctl, cd, st, "DependencyGeneration")) ++rejected; }
    std::printf("[7] rejected %d stale mutations\n", rejected); std::fflush(stdout);
    REQUIRE(rejected == 7, "every stale mutation rejected (7/7)");

    // ----- 8. Rejected stale work did NOT mutate authoritative provenance -----
    std::string q1b; bool p1b;
    query(ctl, S1, q1b, p1b);
    std::printf("[8] authoritative provenance after replay: %s\n", q1b.c_str()); std::fflush(stdout);
    REQUIRE(p1b && get_field(q1b, "size") == "1", "rejected stale work did not add records (size still 1)");
    REQUIRE(get_field(q1b, "derivation_digest") == s1_digest_captured,
            "S1 derivation digest unchanged after replay");

    // ----- 9/10. Fresh authoritative work under a new provenance generation -----
    std::string prov2 = "state_gen=1;prov_gen=2;dep_gen=2;model_gen=1;attempt=1;attempt_gen=1;policy_gen=1";
    REQUIRE(ctl.op(static_cast<std::uint8_t>(Op::SetAuthority), prov2) == "ok",
            "provenance generation rolled to 2 (new generation)");

    StateId S2(0x1002);
    WorkerId WB(0xB001);
    WorkerBootId BB(0xC0001);
    Proc workerB = spawn(worker_exe, std::to_string(port) + " " + WB.to_string() + " " +
                       BB.to_string() + " produce_child " + S2.to_string() + " " + S1.to_string());
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    std::string q2; bool p2;
    query(ctl, S2, q2, p2);
    std::printf("[9/10] worker B fresh derivation %s -> %s\n", S2.to_string().c_str(), q2.c_str()); std::fflush(stdout);
    REQUIRE(p2, "fresh authoritative derivation S2 accepted under new provenance generation");
    REQUIRE(get_field(q2, "prov_gen") == "0x0000000000000002", "S2 carries the new provenance generation");

    // ----- 11. Save the provenance graph (coordinator writes durable snapshot) -----
    std::string save1 = ctl.op(static_cast<std::uint8_t>(Op::Save), snapshot);
    std::printf("[11] save -> %s\n", save1.c_str()); std::fflush(stdout);
    REQUIRE(save1.rfind("saved", 0) == 0, "coordinator saved the provenance graph durably");
    std::string D1 = get_field(save1, "digest");

    // ----- 14. Exact accounting closure (before the coordinator restarts) -----
    std::string stats = ctl.op(static_cast<std::uint8_t>(Op::Stats));
    std::printf("[14] stats -> %s\n", stats.c_str()); std::fflush(stdout);
    long long acc = std::atoll(get_field(stats, "accepted").c_str());
    long long rej = std::atoll(get_field(stats, "rejected").c_str());
    std::string sz = get_field(stats, "size");
    REQUIRE(acc == 2, "accepted mutations == 2 (S1 and S2)");
    REQUIRE(rej == 7, "rejected mutations == 7 (all stale)");
    REQUIRE(sz == "2", "authoritative provenance contains exactly 2 subjects");

    // ----- 12. Restart/recover the coordinator from disk -----
    std::string stop = ctl.op(static_cast<std::uint8_t>(Op::Shutdown));
    (void)stop;
    ctl.close();
    WaitForSingleObject(coord.h, 3000); CloseHandle(coord.h); coord.h = nullptr;
    if (coord.read) { CloseHandle(coord.read); coord.read = nullptr; }
    std::string load_args = "0 " + snapshot + " load";
    Proc coord2 = spawn(coord_exe, load_args);
    std::string l2 = read_until(coord2, "LISTENING");
    std::size_t lp = l2.find("LISTENING");
    int port2 = 0;
    if (lp != std::string::npos) port2 = std::atoi(l2.substr(lp + 9).c_str());
    std::printf("[12] coordinator recovered, listening on %d\n", port2); std::fflush(stdout);
    if (port2 <= 0) std::fprintf(stderr, "  coordinator2 output: %s\n", l2.c_str());
    REQUIRE(port2 > 0, "coordinator restarted and recovered from disk");

    // ----- 13. Replay and verify the same stable provenance/evidence digest -----
    Control ctl2; ctl2.connect_to(port2);
    std::string q1c, q2c; bool p1c, p2c;
    query(ctl2, S1, q1c, p1c);
    query(ctl2, S2, q2c, p2c);
    std::printf("[13] recovered S1=%s  S2=%s\n", p1c ? "present" : "MISSING", p2c ? "present" : "MISSING"); std::fflush(stdout);
    REQUIRE(p1c && p2c, "recovered provenance contains S1 and S2");
    std::string verify_path = snapshot + ".verify";
    std::string save2 = ctl2.op(static_cast<std::uint8_t>(Op::Save), verify_path);
    std::string D2 = get_field(save2, "digest");
    std::printf("[13] digest after recovery = %s\n", D2.c_str()); std::fflush(stdout);
    REQUIRE(!D2.empty() && D2 == D1, "stable provenance/evidence digest reproduced after recovery");

    std::printf("\nPROOF_RESULT %s (accepted=%lld rejected=%lld failures=%d)\n",
                g_failures ? "FAIL" : "PASS", acc, rej, g_failures);
    std::fflush(stdout);

    kill(workerA2); kill(workerB);
    ctl2.close(); kill(coord2);
    cleanup();
    return g_failures ? 1 : 0;
}
