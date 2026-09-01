// ---------------------------------------------------------------------------
// State Provenance - distributed coordinator process.
// Serves framed-TCP requests and owns the authoritative ProvenanceStore.
// argv: <listen_port> [snapshot_path] [load]
// ---------------------------------------------------------------------------
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "stateprovenance/store.hpp"
#include "stateprovenance/persist.hpp"
#include "stateprovenance/protocol.hpp"

using namespace stateprovenance;
using namespace stateprovenance::spnet;

namespace {
std::atomic<bool> g_stopping{false};
std::atomic<long long> g_accepted{0};
std::atomic<long long> g_rejected{0};
std::mutex g_count_mu;
ProvenanceStore g_store;
}

static std::string authority_summary() {
    auto a = g_store.authority();
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "epoch=%llu;attempt=%s;attempt_gen=%s;state_gen=%s;prov_gen=%s;dep_gen=%s;model_gen=%s;adapter_gen=%s;policy_gen=%s",
        static_cast<unsigned long long>(a.epoch),
        a.attempt_id.to_string().c_str(),
        a.attempt_generation.to_string().c_str(),
        a.state_generation.to_string().c_str(),
        a.provenance_generation.to_string().c_str(),
        a.dependency_generation.to_string().c_str(),
        a.model_generation.to_string().c_str(),
        a.adapter_generation.to_string().c_str(),
        a.policy_generation.to_string().c_str());
    return std::string(buf);
}

static void handle_connection(int conn) {
    std::uint8_t op;
    std::string payload;
    while (!g_stopping.load() && recv_frame(conn, op, payload)) {
        switch (static_cast<Op>(op)) {
            case Op::Hello: {
                // payload = worker_id u64, boot_id u64
                if (payload.size() >= 16) {
                    BinaryReader r(payload.data(), payload.size());
                    std::uint64_t wid, bid;
                    r.u64(wid); r.u64(bid);
                    g_store.register_boot(WorkerId(wid), WorkerBootId(bid));
                }
                send_frame(conn, static_cast<std::uint8_t>(Op::Respond), authority_summary());
                break;
            }
            case Op::Publish:
            case Op::PublishStale: {
                RecordData d; AuthorityClaim c;
                if (!decode_publish_payload(payload, d, c)) {
                    send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "err:malformed publish");
                    g_rejected.fetch_add(1);
                    break;
                }
                auto r = g_store.publish(d, c);
                if (r.ok) { g_accepted.fetch_add(1); send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "ok");
                    std::printf("PUBLISHED %s prov_gen=%s\n", d.subject_id.to_string().c_str(), d.provenance_generation.to_string().c_str());
                    std::fflush(stdout);
                } else {
                    g_rejected.fetch_add(1);
                    send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "err:" + r.error);
                }
                break;
            }
            case Op::RollEpoch: {
                std::uint64_t e = g_store.roll_epoch();
                send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "epoch=" + std::to_string(e));
                break;
            }
            case Op::SetAuthority: {
                // payload: "prov_gen=<hex>;state_gen=<hex>;dep_gen=<hex>;model_gen=<hex>;attempt=<hex>;attempt_gen=<hex>"
                std::string s(payload);
                auto get = [&](const std::string& key)->std::string { // key=val separated by ';'
                    std::size_t pos = s.find(key + "=");
                    if (pos == std::string::npos) return "";
                    pos += key.size() + 1;
                    std::size_t end = s.find(';', pos);
                    return s.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                };
                auto hx = [](const std::string& v)->std::uint64_t { return std::strtoull(v.empty()?"0":v.c_str(), nullptr, 16); };
                std::uint64_t sg = hx(get("state_gen"));
                std::uint64_t pg = hx(get("prov_gen"));
                std::uint64_t dg = hx(get("dep_gen"));
                std::uint64_t mg = hx(get("model_gen"));
                std::uint64_t ag = hx(get("attempt"));
                std::uint64_t ag2 = hx(get("attempt_gen"));
                std::uint64_t pgany = hx(get("policy_gen"));
                if (sg) g_store.roll_state_generation(StateGeneration(sg));
                if (pg) g_store.roll_provenance_generation(ProvenanceGeneration(pg));
                if (dg) g_store.set_dependency_generation(DependencyGeneration(dg));
                if (mg) g_store.set_model_generation(ModelGeneration(mg));
                if (ag && ag2) g_store.begin_attempt(AttemptId(ag), AttemptGeneration(ag2));
                if (pgany) g_store.set_policy_generation(PolicyGeneration(pgany));
                send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "ok");
                break;
            }
            case Op::Save: {
                // payload = path
                auto snap = g_store.snapshot();
                auto dig = g_store.store_digest(snap);
                auto err = save_file(snap, payload);
                if (err) send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "err:" + *err);
                else send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "saved;digest=" + dig);
                break;
            }
            case Op::Query: {
                if (payload.size() >= 8) {
                    BinaryReader r(payload.data(), payload.size());
                    std::uint64_t id; r.u64(id);
                    StateId sid(id);
                    auto rec = g_store.find(sid);
                    if (!rec) { send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "absent"); break; }
                    auto st = rec->data();
                    auto v = g_store.validity_of(sid);
                    char b[256];
                    std::snprintf(b, sizeof(b),
                        "present;kind=%s;validity=%s;derivation_digest=%s;prov_gen=%s;state_gen=%s;model_gen=%s;evidence=%s;size=%llu",
                        name_of(st.subject_kind),
                        name_of(v),
                        rec->derivation_digest().c_str(),
                        st.provenance_generation.to_string().c_str(),
                        st.state_generation.to_string().c_str(),
                        st.model_generation.to_string().c_str(),
                        name_of(st.evidence),
                        static_cast<unsigned long long>(g_store.size()));
                    send_frame(conn, static_cast<std::uint8_t>(Op::Respond), std::string(b));
                } else send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "err:bad query");
                break;
            }
            case Op::Stats: {
                char b[128];
                std::snprintf(b, sizeof(b), "accepted=%lld;rejected=%lld;epoch=%llu;prov_gen=%s;size=%llu",
                    g_accepted.load(), g_rejected.load(),
                    static_cast<unsigned long long>(g_store.epoch()),
                    g_store.authority().provenance_generation.to_string().c_str(),
                    static_cast<unsigned long long>(g_store.size()));
                send_frame(conn, static_cast<std::uint8_t>(Op::Respond), std::string(b));
                break;
            }
            case Op::Shutdown: {
                send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "bye");
                g_stopping.store(true);
                return;
            }
            default:
                send_frame(conn, static_cast<std::uint8_t>(Op::Respond), "err:unknown op");
                break;
        }
    }
    close_socket(conn);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: sp_coordinator <listen_port> [snapshot] [load]\n"); return 2; }
    if (!init()) { std::fprintf(stderr, "WSAStartup failed\n"); return 1; }

    int port = std::atoi(argv[1]);
    // optional load at startup
    if (argc >= 4 && std::string(argv[3]) == "load") {
        auto lr = load_file(argv[2]);
        if (lr.snapshot) {
            g_store.restore(*lr.snapshot);
            std::printf("LOADED digest=%s\n", g_store.store_digest(*lr.snapshot).c_str());
            std::fflush(stdout);
        } else {
            std::fprintf(stderr, "load failed: %s\n", lr.error.c_str());
            return 3;
        }
    }

    int ls = listen_socket(static_cast<std::uint16_t>(port));
    if (ls < 0) { std::fprintf(stderr, "cannot listen on port %d\n", port); return 1; }
    std::uint16_t actual = local_port(ls);
    std::printf("LISTENING %u\n", actual);
    std::fflush(stdout);

    // accept loop
    while (!g_stopping.load()) {
        int conn = accept_conn(ls);
        if (conn < 0) {
            if (g_stopping.load()) break;
            continue;
        }
        std::thread t(handle_connection, conn);
        t.detach();
    }
    close_socket(ls);
    cleanup();
    std::printf("COORD_EXIT accepted=%lld rejected=%lld\n", g_accepted.load(), g_rejected.load());
    std::fflush(stdout);
    return 0;
}
