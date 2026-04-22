#include "common.h"
#include "SQLParser.h"

int main() {
    std::cout << "==============================\n";
    std::cout << "  简易DBMS (模块化版)\n";
    std::cout << "  输入 HELP 查看命令\n";
    std::cout << "==============================\n\n";

    MKDIR("data");
    SQLParser& parser = SQLParser::getInstance();

    std::string line;
    while (true) {
        std::cout << (g_current_db.empty() ? "DBMS> " : "DBMS [" + g_current_db + "]> ");
        std::getline(std::cin, line);
        parser.execute(line);
    }
    return 0;
}