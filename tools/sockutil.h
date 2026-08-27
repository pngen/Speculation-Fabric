// Winsock2 framing helpers for the distributed control plane binaries.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#ifndef _WIN32
#error "distributed control plane targets Windows WSA"
#endif
#include <winsock2.h>
#include "speculation_fabric/core/wire.hpp"
#include <ws2tcpip.h>

namespace sfsock {

inline bool init() {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
}
inline void cleanup() { WSACleanup(); }

inline int listen_port(std::uint16_t port, std::string& err) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { err = "socket"; return -1; }
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { err = "bind"; closesocket(s); return -1; }
    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) { err = "listen"; closesocket(s); return -1; }
    return (int)s;
}

inline int connect_port(const std::string& host, std::uint16_t port, std::string& err) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { err = "socket"; return -1; }
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) { err = "bad host"; closesocket(s); return -1; }
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        err = "connect " + std::to_string(WSAGetLastError()); closesocket(s); return -1;
    }
    return (int)s;
}

inline int accept_conn(int listener) {
    SOCKET c = ::accept((SOCKET)listener, nullptr, nullptr);
    return (int)c;
}

inline bool send_all(int fd, const std::uint8_t* data, std::size_t n) {
    std::size_t sent = 0;
    while (sent < n) {
        int k = ::send((SOCKET)fd, reinterpret_cast<const char*>(data + sent), (int)(n - sent), 0);
        if (k <= 0) return false;
        sent += (std::size_t)k;
    }
    return true;
}

inline bool recv_exact(int fd, std::uint8_t* data, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        int k = ::recv((SOCKET)fd, reinterpret_cast<char*>(data + got), (int)(n - got), 0);
        if (k <= 0) return false;
        got += (std::size_t)k;
    }
    return true;
}

// framed helpers using speculation_fabric::wire
inline bool send_frame(int fd, const std::vector<std::uint8_t>& payload) {
    auto framed = speculation_fabric::wire::add_frame_prefix(payload);
    if (framed.is_error()) return false;
    return send_all(fd, framed.value().data(), framed.value().size());
}

inline bool recv_frame(int fd, std::vector<std::uint8_t>& payload) {
    std::uint8_t lenb[4];
    if (!recv_exact(fd, lenb, 4)) return false;
    std::uint32_t len = (std::uint32_t)lenb[0] | ((std::uint32_t)lenb[1] << 8) |
                        ((std::uint32_t)lenb[2] << 16) | ((std::uint32_t)lenb[3] << 24);
    if (len == 0 || len > speculation_fabric::wire::kMaxFrameSize) return false;
    payload.assign(len, 0);
    return recv_exact(fd, payload.data(), len);
}

}  // namespace sfsock