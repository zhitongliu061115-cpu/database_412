#include "common.h"
#include "SQLParser.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>

// 告诉编译器链接 Winsock 库
#pragma comment(lib, "ws2_32.lib")

int main() {
    std::cout << "==============================\n";
    std::cout << "  简易DBMS (Socket 服务端版)\n";
    std::cout << "  正在监听端口 8080...\n";
    std::cout << "==============================\n\n";

    MKDIR("data");
    SQLParser& parser = SQLParser::getInstance();

    // 1. 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    // 2. 创建并绑定 Socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080); // 监听 8080 端口

    bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, SOMAXCONN);

    // 3. 循环接收客户端连接
    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        std::cout << "[系统] 客户端已连接！\n";

        char buffer[4096];
        int bytesReceived;

        // 4. 循环接收当前客户端发来的 SQL
        while ((bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytesReceived] = '\0';
            std::string sql(buffer);
            std::cout << "[收到指令] " << sql << "\n";

			//重定向 std::cout 到一个字符串流，以捕获 SQLParser 的输出
            std::stringstream responseStream;
            std::streambuf* oldCoutBuffer = std::cout.rdbuf(responseStream.rdbuf());

            // 执行 SQL
            parser.execute(sql);

            // 恢复 std::cout
            std::cout.rdbuf(oldCoutBuffer);

            // 获取后端执行的输出结果，发回给客户端
            std::string response = responseStream.str();
            if (response.empty()) response = "OK (无输出)\n";
            send(clientSocket, response.c_str(), response.length(), 0);
        }

        std::cout << "[系统] 客户端已断开连接。\n";
        closesocket(clientSocket);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}