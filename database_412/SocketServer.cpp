#include "SocketServer.h"
#include "SQLParser.h"
#include "common.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
constexpr int kSocketError = SOCKET_ERROR;
void closeSocket(SocketHandle sock) {
    closesocket(sock);
}
#else
constexpr SocketHandle kInvalidSocket = -1;
constexpr int kSocketError = -1;
void closeSocket(SocketHandle sock) {
    close(sock);
}
#endif

bool sendAll(SocketHandle sock, const char* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        int n = send(sock, data + sent, static_cast<int>(size - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}
}

SocketServer::SocketServer()
    : listenSocket_(kInvalidSocket), networkReady_(false), running_(false) {
}

SocketServer::~SocketServer() {
    stop();
}

bool SocketServer::initNetwork() {
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "Err: WSAStartup failed\n";
        return false;
    }
#endif
    networkReady_ = true;
    return true;
}

void SocketServer::cleanupNetwork() {
    if (!networkReady_) {
        return;
    }
#ifdef _WIN32
    WSACleanup();
#endif
    networkReady_ = false;
}

bool SocketServer::start(uint16_t port) {
    if (!initNetwork()) {
        return false;
    }

    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == kInvalidSocket) {
        std::cout << "Err: create listen socket failed\n";
        cleanupNetwork();
        return false;
    }

    int reuse = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == kSocketError) {
        std::cout << "Err: bind port " << port << " failed\n";
        closeSocket(listenSocket_);
        listenSocket_ = kInvalidSocket;
        cleanupNetwork();
        return false;
    }

    if (listen(listenSocket_, 8) == kSocketError) {
        std::cout << "Err: listen failed\n";
        closeSocket(listenSocket_);
        listenSocket_ = kInvalidSocket;
        cleanupNetwork();
        return false;
    }

    running_ = true;
    std::cout << "[Socket] server started on port " << port << "\n";
    return true;
}

void SocketServer::stop() {
    running_ = false;
    if (listenSocket_ != kInvalidSocket) {
        closeSocket(listenSocket_);
        listenSocket_ = kInvalidSocket;
    }
    cleanupNetwork();
}

void SocketServer::run() {
    while (running_) {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int addrLen = sizeof(clientAddr);
#else
        socklen_t addrLen = sizeof(clientAddr);
#endif

        SocketHandle clientSock = accept(listenSocket_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientSock == kInvalidSocket) {
            if (running_) {
                std::cout << "Warn: accept failed, waiting for next client\n";
            }
            continue;
        }

        char ipBuf[64] = { 0 };
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
        std::string clientTag = std::string(ipBuf) + ":" + std::to_string(ntohs(clientAddr.sin_port));

        std::cout << "[Socket] client connected: " << clientTag << "\n";
        handleClient(clientSock, clientTag);
        closeSocket(clientSock);
        std::cout << "[Socket] client disconnected: " << clientTag << "\n";
    }
}

void SocketServer::handleClient(SocketHandle clientSocket, const std::string& clientTag) {
    std::string recvCache;

    while (running_) {
        std::string line;
        if (!recvLine(clientSocket, recvCache, line)) {
            return;
        }

        trim(line);
        if (line.empty()) {
            continue;
        }

        // Keep one single parser path so REPL mode and socket mode never drift apart.
        bool shouldCloseClient = false;
        std::string response = SQLParser::getInstance().executeWithOutput(line, &shouldCloseClient);
        if (response.empty()) {
            response = "OK: command handled, no output.\n";
        }

        if (!sendPacket(clientSocket, response)) {
            std::cout << "Warn: send response failed: " << clientTag << "\n";
            return;
        }

        if (shouldCloseClient) {
            // EXIT should close this client session only, not the whole server process.
            return;
        }
    }
}

bool SocketServer::recvLine(SocketHandle sock, std::string& cache, std::string& line) {
    while (true) {
        size_t pos = cache.find('\n');
        if (pos != std::string::npos) {
            line = cache.substr(0, pos);
            cache.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }

        char buf[1024];
        int n = recv(sock, buf, static_cast<int>(sizeof(buf)), 0);
        if (n <= 0) {
            return false;
        }

        cache.append(buf, static_cast<size_t>(n));
    }
}

bool SocketServer::sendPacket(SocketHandle sock, const std::string& payload) {
    // Protocol: 4-byte big-endian length + UTF-8 payload.
    uint32_t len = static_cast<uint32_t>(payload.size());
    uint32_t netLen = htonl(len);

    if (!sendAll(sock, reinterpret_cast<const char*>(&netLen), sizeof(netLen))) {
        return false;
    }

    if (len == 0) {
        return true;
    }

    return sendAll(sock, payload.data(), payload.size());
}
