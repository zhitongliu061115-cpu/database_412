#ifndef SOCKET_SERVER_H
#define SOCKET_SERVER_H

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
using SocketHandle = SOCKET;
#else
using SocketHandle = int;
#endif

class SocketServer {
public:
    SocketServer();
    ~SocketServer();

    SocketServer(const SocketServer&) = delete;
    SocketServer& operator=(const SocketServer&) = delete;

    bool start(uint16_t port);
    void run();
    void stop();

private:
    bool initNetwork();
    void cleanupNetwork();
    void handleClient(SocketHandle clientSocket, const std::string& clientTag);
    bool recvLine(SocketHandle sock, std::string& cache, std::string& line);
    bool sendPacket(SocketHandle sock, const std::string& payload);

    SocketHandle listenSocket_;
    bool networkReady_;
    bool running_;
};

#endif // SOCKET_SERVER_H
