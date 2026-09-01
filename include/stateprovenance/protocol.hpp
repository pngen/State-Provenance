#pragma once
// ---------------------------------------------------------------------------
// State Provenance - framed TCP transport for the multiprocess authority/
// distributed proof and for coordinating workers.
//
// A frame is:  [u32 length][u8 opcode][payload bytes], length = 1 + payload.
// Little-endian throughout.  The transport carries authoritative publish
// claims and results so that stale authority can be replayed and rejected on
// the real wire.  Uses Winsock2 on Windows.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

#include "stateprovenance/ids.hpp"
#include "stateprovenance/enums.hpp"
#include "stateprovenance/record.hpp"
#include "stateprovenance/authority.hpp"
#include "stateprovenance/persist.hpp"

namespace stateprovenance::spnet {

// ---------------------------------------------------------------------------
// Opcodes
// ---------------------------------------------------------------------------
enum class Op : std::uint8_t {
    Hello       = 1,   // worker -> coord: register (worker_id, boot_id)
    Publish     = 2,   // worker -> coord: publish a record under an authority claim
    Respond     = 3,   // coord -> workers/clients: result payload
    RollEpoch   = 4,   // control -> coord: roll coordinator epoch
    SetAuthority= 5,   // control -> coord: set state/provenance/... generations
    Save        = 6,   // control -> coord: save snapshot to path
    Load        = 7,   // control -> coord: load snapshot from path (coordinator startup)
    Query       = 8,   // client -> coord: query a subject id
    Shutdown    = 9,   // control -> coord: stop serving
    PublishStale= 10,  // worker -> coord: like Publish but expected to be rejected (for the proof)
    Stats       = 11,  // control -> coord: return accepted/rejected accounting plus authority
};

constexpr std::uint32_t kMaxFrame = 32u * 1024u * 1024u;  // 32 MiB cap

// ---------------------------------------------------------------------------
// Socket primitives
// ---------------------------------------------------------------------------
inline bool init() {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}
inline void cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}
inline void close_socket(int s) {
#ifdef _WIN32
    if (s >= 0) { shutdown(s, SD_BOTH); closesocket(s); }
#else
    if (s >= 0) { shutdown(s, SHUT_RDWR); ::close(s); }
#endif
}

// Bind and listen. port==0 => ephemeral. Returns listening socket or -1.
inline int listen_socket(std::uint16_t port) {
#ifdef _WIN32
    SOCKET ls = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return -1;
    BOOL yes = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(ls); return -1;
    }
    if (::listen(ls, SOMAXCONN) == SOCKET_ERROR) { closesocket(ls); return -1; }
    return static_cast<int>(ls);
#else
    return -1;
#endif
}

inline std::uint16_t local_port(int listen_sock) {
#ifdef _WIN32
    sockaddr_in addr{};
    int len = sizeof(addr);
    if (getsockname(listen_sock, reinterpret_cast<sockaddr*>(&addr), &len) == SOCKET_ERROR)
        return 0;
    return ntohs(addr.sin_port);
#else
    return 0;
#endif
}

inline int accept_conn(int listen_sock) {
#ifdef _WIN32
    sockaddr_in addr{};
    int len = sizeof(addr);
    SOCKET c = ::accept(listen_sock, reinterpret_cast<sockaddr*>(&addr), &len);
    return c == INVALID_SOCKET ? -1 : static_cast<int>(c);
#else
    return -1;
#endif
}

inline int connect_socket(const std::string& host, std::uint16_t port, int timeout_ms = 5000) {
#ifdef _WIN32
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;
    // non-blocking connect with timeout
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        hostent* he = gethostbyname(host.c_str());
        if (!he) { closesocket(s); return -1; }
        std::memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }
    int r = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) { closesocket(s); return -1; }
        fd_set w; FD_ZERO(&w); FD_SET(s, &w);
        timeval tv; tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
        r = select(0, nullptr, &w, nullptr, &tv);
        if (r <= 0) { closesocket(s); return -1; }
        int soerr = 0; int slen = sizeof(soerr);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &slen);
        if (soerr != 0) { closesocket(s); return -1; }
    }
    u_long blk = 0;
    ioctlsocket(s, FIONBIO, &blk);
    return static_cast<int>(s);
#else
    (void)host; (void)port; (void)timeout_ms; return -1;
#endif
}

inline bool send_all(int s, const void* data, std::size_t n) {
#ifdef _WIN32
    const char* p = static_cast<const char*>(data);
    while (n > 0) {
        int sent = ::send(s, p, static_cast<int>(n), 0);
        if (sent <= 0) return false;
        p += sent; n -= static_cast<std::size_t>(sent);
    }
    return true;
#else
    (void)s; (void)data; (void)n; return false;
#endif
}

inline bool recv_all(int s, void* data, std::size_t n) {
#ifdef _WIN32
    char* p = static_cast<char*>(data);
    while (n > 0) {
        int got = ::recv(s, p, static_cast<int>(n), 0);
        if (got <= 0) return false;
        p += got; n -= static_cast<std::size_t>(got);
    }
    return true;
#else
    (void)s; (void)data; (void)n; return false;
#endif
}

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------
inline bool send_frame(int s, std::uint8_t op, const std::string& payload) {
    // frame = [u32 len][u8 op][payload]; len = payload.size() + 1
    std::uint32_t len = static_cast<std::uint32_t>(payload.size()) + 1u;
    if (len > kMaxFrame) return false;
    std::vector<std::uint8_t> head(4);
    head[0] = static_cast<std::uint8_t>(len & 0xFFu);
    head[1] = static_cast<std::uint8_t>((len >> 8) & 0xFFu);
    head[2] = static_cast<std::uint8_t>((len >> 16) & 0xFFu);
    head[3] = static_cast<std::uint8_t>((len >> 24) & 0xFFu);
    if (!send_all(s, head.data(), 4)) return false;
    if (!send_all(s, &op, 1)) return false;
    if (!payload.empty() && !send_all(s, payload.data(), payload.size())) return false;
    return true;
}

// Reads one frame.  Returns false on EOF/error.
inline bool recv_frame(int s, std::uint8_t& op, std::string& payload) {
    std::uint8_t head[4];
    if (!recv_all(s, head, 4)) return false;
    std::uint32_t len = static_cast<std::uint32_t>(head[0]) |
                        (static_cast<std::uint32_t>(head[1]) << 8) |
                        (static_cast<std::uint32_t>(head[2]) << 16) |
                        (static_cast<std::uint32_t>(head[3]) << 24);
    if (len == 0 || len > kMaxFrame) return false;
    if (!recv_all(s, &op, 1)) return false;
    std::uint32_t plen = len - 1u;
    if (plen > 0) {
        payload.resize(plen);
        if (!recv_all(s, payload.data(), plen)) return false;
    } else {
        payload.clear();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Claim<->wire encoding (used by Publish)
// ---------------------------------------------------------------------------
inline std::string encode_claim(const AuthorityClaim& c) {
    BinaryWriter w;
    w.u64(c.coordinator_epoch);
    w.u64(c.worker_id.get());
    w.u64(c.worker_boot.get());
    w.u64(c.attempt_id.get());
    w.u64(c.attempt_generation.get());
    w.u64(c.state_generation.get());
    w.u64(c.provenance_generation.get());
    // domain-generation optionals: 0 => not constrained
    w.u64(c.artifact_generation ? c.artifact_generation->get() : 0);
    w.u64(c.model_generation ? c.model_generation->get() : 0);
    w.u64(c.adapter_generation ? c.adapter_generation->get() : 0);
    w.u64(c.composition_generation ? c.composition_generation->get() : 0);
    w.u64(c.policy_generation ? c.policy_generation->get() : 0);
    w.u64(c.dependency_generation ? c.dependency_generation->get() : 0);
    return std::string(reinterpret_cast<const char*>(w.data().data()), w.size());
}

inline void decode_claim(const std::string& payload, std::size_t& off, AuthorityClaim& c) {
    BinaryReader r(payload.data() + off, payload.size() - off);
    std::uint64_t u;
    r.u64(u); c.coordinator_epoch = u;
    r.u64(u); c.worker_id = WorkerId(u);
    r.u64(u); c.worker_boot = WorkerBootId(u);
    r.u64(u); c.attempt_id = AttemptId(u);
    r.u64(u); c.attempt_generation = AttemptGeneration(u);
    r.u64(u); c.state_generation = StateGeneration(u);
    r.u64(u); c.provenance_generation = ProvenanceGeneration(u);
    r.u64(u); if (u) c.artifact_generation = ArtifactGeneration(u);
    r.u64(u); if (u) c.model_generation = ModelGeneration(u);
    r.u64(u); if (u) c.adapter_generation = AdapterGeneration(u);
    r.u64(u); if (u) c.composition_generation = CompositionGeneration(u);
    r.u64(u); if (u) c.policy_generation = PolicyGeneration(u);
    r.u64(u); if (u) c.dependency_generation = DependencyGeneration(u);
    off += payload.size() - r.remaining();
}

// Full publish frame payload: encode_claim(claim) + write_record(d)
inline std::string encode_publish_payload(const RecordData& d, const AuthorityClaim& c) {
    std::string claim = encode_claim(c);
    BinaryWriter w;
    write_record(w, d);
    std::string rec(reinterpret_cast<const char*>(w.data().data()), w.size());
    std::string out;
    out.reserve(claim.size() + rec.size());
    out += claim;
    out += rec;
    return out;
}

inline bool decode_publish_payload(const std::string& payload, RecordData& d, AuthorityClaim& c) {
    // claim is fixed-size: 13 * 8 bytes = 104 bytes
    if (payload.size() < 104) return false;
    std::size_t off = 0;
    decode_claim(payload, off, c);
    BinaryReader r(payload.data() + off, payload.size() - off);
    auto err = read_record(r, d);
    return err.has_value() == false;
}

} // namespace stateprovenance::spnet
