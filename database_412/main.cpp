#include "common.h"
#include "DatabaseManager.h"
#include "SQLParser.h"
#include "SecurityManager.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>
#include <thread>
#include <vector>
#include <mutex>
#include <filesystem> // 【关键引入】C++17 标准文件系统库

#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;

// 全局互斥锁：确保单例状态切换、std::cout 重定向在多线程环境下的绝对安全
std::mutex g_dbMutex;

void handleClient(SOCKET clientSocket) {
    std::cout << "[系统] 客户端已连接！线程ID: " << std::this_thread::get_id() << "\n";

    char buffer[4096];
    int bytesReceived;

    while ((bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        std::string payload(buffer);
        std::cout << "[收到网络报文] 线程 " << std::this_thread::get_id() << " : " << payload << "\n";

        // =================================================================
        // 路由 1：动态同步目录树数据 (FETCH_SCHEMA)
        // 核心逻辑：扫描 data/Schema/ 目录下的文件夹和 .fld 文件，将其序列化为网络报文
        // =================================================================
        if (payload.find("INTERNAL_CMD:FETCH_SCHEMA") == 0) {
            std::string syncData = "SCHEMA_SYNC";
            {
                // 扫描文件目录时加锁，防止扫描期间其他线程正在创建用户或表导致冲突
                std::lock_guard<std::mutex> lock(g_dbMutex);

                if (fs::exists("data/Schema")) {
                    // 1. 遍历 data/Schema 下的所有子目录（每个目录代表一个用户Schema）
                    for (const auto& userEntry : fs::directory_iterator("data/Schema")) {
                        if (userEntry.is_directory()) {
                            std::string username = userEntry.path().filename().string();
                            syncData += "|USER:" + username;

                            // 2. 遍历该用户目录下的所有文件，筛选出 .fld 结尾的文件（每个代表一张数据表）
                            for (const auto& fileEntry : fs::directory_iterator(userEntry.path())) {
                                if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".fld") {
                                    std::string tablename = fileEntry.path().stem().string();
                                    syncData += "|TABLE:" + username + ":" + tablename;
                                }
                            }
                        }
                    }
                }
            }
            // 将拼接好的目录流报文发送回客户端
            send(clientSocket, syncData.c_str(), syncData.length(), 0);
            continue; // 本次报文处理完毕，直接跳过后面的普通 SQL 解析流，等待下一次接收
        }

        std::string response;

        // =================================================================
        // 常规指令与 SQL 执行区域（涉及数据库单例状态更改，必须严格加锁临界区）
        // =================================================================
        {
            std::lock_guard<std::mutex> lock(g_dbMutex);

            std::stringstream responseStream;
            std::streambuf* oldCoutBuffer = std::cout.rdbuf(responseStream.rdbuf());

            // 路由 2：注册新用户 (CREATE_USER)
            if (payload.find("INTERNAL_CMD:CREATE_USER:") == 0) {
                std::string username = payload.substr(25);
                std::cout << "[系统] 正在为新用户初始化 Schema 空间: " << username << "\n";
                // 物理路径映射：data/Schema/用户名
                DatabaseManager::getInstance().createDB("Schema/" + username);
            }
            // 路由 3：基于特定用户控制台执行会话 SQL
            else if (payload.find("USER:") == 0) {
                size_t splitPos = payload.find("|SQL:");
                if (splitPos != std::string::npos) {
                    std::string username = payload.substr(5, splitPos - 5);
                    std::string sqlText = payload.substr(splitPos + 5);

                    // 核心拦截：将当前工作环境切换到当前用户的隔离子文件夹中
                    DatabaseManager::getInstance().useDB("Schema/" + username);

                    // 让底层的解析器执行真实的 SQL
                    SQLParser::getInstance().execute(sqlText);
                }
                else {
                    std::cout << "Err: 无法解析的会话 SQL 报文格式。\n";
                }
            }
            // 路由 4：兼容未带用户前缀的直接通用 SQL 语句
            else {
                SQLParser::getInstance().execute(payload);
            }

            std::cout.rdbuf(oldCoutBuffer);
            response = responseStream.str();
        }

        if (response.empty()) response = "OK (无输出)\n";
        send(clientSocket, response.c_str(), response.length(), 0);
    }

    std::cout << "[系统] 客户端已断开连接。线程ID: " << std::this_thread::get_id() << "\n";
    closesocket(clientSocket);
}

int main() {
    std::cout << "=========================================\n";
    std::cout << "  简易DBMS (多线程 + Schema 隔离服务端版)\n";
    std::cout << "  正在监听端口 8080...\n";
    std::cout << "=========================================\n\n";

    // 1. 初始化物理基础层级目录结构
    MKDIR("data");
    MKDIR("data/Schema"); // 建立独立的系统隔离专属 Schema 区域

    DatabaseManager::getInstance().ensureDefaultDB();
    SecurityManager::getInstance().initialize();

    // 2. 初始化 Windows Sockets 环境
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

    // 3. 服务器主并发轮询监听循环
    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed. Error: " << WSAGetLastError() << "\n";
            continue;
        }

        // 每一个新客户端进来，为其开辟一条独立的业务处理线程，并进行脱离管理
        clientThreads.emplace_back(handleClient, clientSocket);
        clientThreads.back().detach();
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
