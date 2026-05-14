#include "common.h"
#include "DatabaseManager.h"
#include "SQLParser.h"
#include "SecurityManager.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

void handleClient(SOCKET clientSocket) {
    std::cout << "[系统] 客户端已连接！线程ID: " << std::this_thread::get_id() << "\n";

    char buffer[4096];
    int bytesReceived;

    while ((bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        std::string sql(buffer);
        std::cout << "[收到指令] " << sql << "\n";

        std::stringstream responseStream;
        std::streambuf* oldCoutBuffer = std::cout.rdbuf(responseStream.rdbuf());

        SQLParser::getInstance().execute(sql);

        std::cout.rdbuf(oldCoutBuffer);

        std::string response = responseStream.str();
        if (response.empty()) response = "OK (无输出)\n";
        send(clientSocket, response.c_str(), response.length(), 0);
    }

    std::cout << "[系统] 客户端已断开连接。\n";
    closesocket(clientSocket);
}

int main() {
    std::cout << "==============================\n";
    std::cout << "  简易DBMS (多线程服务端版)\n";
    std::cout << "  正在监听端口 8080...\n";
    std::cout << "==============================\n\n";

    MKDIR("data");
    DatabaseManager::getInstance().ensureDefaultDB();
    SecurityManager::getInstance().initialize();
    SQLParser& parser = SQLParser::getInstance();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);

    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed. Error: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed. Error: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    std::vector<std::thread> clientThreads;

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed. Error: " << WSAGetLastError() << "\n";
            continue;
        }

        clientThreads.emplace_back(handleClient, clientSocket);
        clientThreads.back().detach();
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
