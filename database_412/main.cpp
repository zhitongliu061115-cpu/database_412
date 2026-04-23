#include "common.h"
#include "SQLParser.h"
#include "SocketServer.h"

int main(int argc, char* argv[]) {
    std::cout << "==============================\n";
    std::cout << "  简易DBMS (模块化版)\n";
    std::cout << "  输入 HELP 查看命令\n";
    std::cout << "==============================\n\n";

    MKDIR("data");

    // 给你保留了旧版命令行模式，方便本地调试。
    // 运行参数带 --cli 就走原来的交互式 REPL。
    if (argc > 1 && std::string(argv[1]) == "--cli") {
        SQLParser& parser = SQLParser::getInstance();
        std::string line;
        while (true) {
            std::cout << (g_current_db.empty() ? "DBMS> " : "DBMS [" + g_current_db + "]> ");
            std::getline(std::cin, line);
            parser.execute(line);
        }
    }

    constexpr uint16_t kPort = 9527;
    SocketServer server;
    if (!server.start(kPort)) {
        return 1;
    }

    server.run();
    return 0;
}
