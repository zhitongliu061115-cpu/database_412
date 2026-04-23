#include "SQLParser.h"
#include "DatabaseManager.h"
#include "TableManager.h"
#include "FieldManager.h"
#include "RecordManager.h"
#include <cstdlib>

namespace {
class CoutRedirectGuard {
public:
    explicit CoutRedirectGuard(std::ostream& target) : oldBuf_(std::cout.rdbuf(target.rdbuf())) {}
    ~CoutRedirectGuard() {
        std::cout.rdbuf(oldBuf_);
    }

private:
    std::streambuf* oldBuf_;
};
}

SQLParser& SQLParser::getInstance() {
    static SQLParser instance;
    return instance;
}

void SQLParser::showHelp() {
    std::cout << "\n支持的命令:\n"
        << "  CREATE DATABASE <name>\n"
        << "  DROP DATABASE <name>\n"
        << "  USE <db_name>\n"
        << "  CREATE TABLE <name>\n"
        << "  DROP TABLE <name>\n"
        << "  ALTER TABLE <t> ADD <col> <type>\n"
        << "  ALTER TABLE <t> DROP <col>\n"
        << "  ALTER TABLE <t> MODIFY <old_col> <new_col>\n"
        << "  INSERT INTO <t> VALUES v1, v2, ...\n"
        << "  SELECT * FROM <t>\n"
        << "  UPDATE <t> SET <c> = v WHERE row = 0\n"
        << "  DELETE FROM <t> [WHERE row = 0]\n"
        << "  EXIT\n\n";
}

void SQLParser::execute(const std::string& sql) {
    bool shouldExit = false;
    std::string output = executeWithOutput(sql, &shouldExit);
    if (!output.empty()) {
        std::cout << output;
    }
    if (shouldExit) {
        std::exit(0);
    }
}

std::string SQLParser::executeWithOutput(const std::string& sql, bool* shouldExit) {
    bool exitFlag = false;
    std::ostringstream capture;
    CoutRedirectGuard guard(capture);

    std::string s = sql;
    trim(s);
    if (s.empty()) {
        if (shouldExit != nullptr) {
            *shouldExit = false;
        }
        return capture.str();
    }

    auto tokens = split(s, ' ');
    if (tokens.empty()) {
        if (shouldExit != nullptr) {
            *shouldExit = false;
        }
        return capture.str();
    }

    std::string cmd = toUpper(tokens[0]);

    try {
        if (cmd == "CREATE" && tokens.size() > 2) {
            std::string type = toUpper(tokens[1]);
            if (type == "DATABASE") DatabaseManager::getInstance().createDB(tokens[2]);
            else if (type == "TABLE") TableManager::getInstance().createTable(tokens[2]);
            else std::cout << "Err: 未知的 CREATE 类型\n";
        }
        else if (cmd == "DROP" && tokens.size() > 2) {
            std::string type = toUpper(tokens[1]);
            if (type == "DATABASE") DatabaseManager::getInstance().dropDB(tokens[2]);
            else if (type == "TABLE") TableManager::getInstance().dropTable(tokens[2]);
            else std::cout << "Err: 未知的 DROP 类型\n";
        }
        else if (cmd == "USE" && tokens.size() > 1) {
            DatabaseManager::getInstance().useDB(tokens[1]);
        }
        else if (cmd == "ALTER" && tokens.size() > 4) {
            // ALTER TABLE <tname> ADD/DROP/MODIFY ...
            if (toUpper(tokens[1]) == "TABLE" && tokens.size() > 2) {
                std::string tname = tokens[2];
                std::string action = toUpper(tokens[3]);
                if (action == "ADD" && tokens.size() > 5) {
                    FieldManager::getInstance().addField(tname, tokens[4], tokens[5]);
                }
                else if (action == "DROP" && tokens.size() > 4) {
                    FieldManager::getInstance().dropField(tname, tokens[4]);
                }
                else if (action == "MODIFY" && tokens.size() > 5) {
                    FieldManager::getInstance().modifyField(tname, tokens[4], tokens[5]);
                }
                else {
                    std::cout << "Err: ALTER 操作不支持\n";
                }
            }
            else {
                std::cout << "Err: 语法错误，正确格式: ALTER TABLE <tname> ...\n";
            }
        }
        else if (cmd == "INSERT" && tokens.size() > 3) {
            // INSERT INTO <tname> VALUES ...
            if (toUpper(tokens[1]) == "INTO" && tokens.size() > 2) {
                std::string tname = tokens[2];
                size_t val_pos = toUpper(s).find("VALUES");
                if (val_pos != std::string::npos) {
                    std::string val_part = s.substr(val_pos + 6);
                    auto vals = split(val_part, ',');
                    RecordManager::getInstance().insertRecord(tname, vals);
                }
                else {
                    std::cout << "Err: 缺少 VALUES\n";
                }
            }
            else {
                std::cout << "Err: 语法错误，正确格式: INSERT INTO <tname> VALUES ...\n";
            }
        }
        else if (cmd == "SELECT" && tokens.size() > 3) {
            // SELECT * FROM <tname>
            if (tokens[1] == "*" && toUpper(tokens[2]) == "FROM" && tokens.size() > 3) {
                RecordManager::getInstance().selectRecords(tokens[3]);
            }
            else {
                std::cout << "Err: 仅支持 SELECT * FROM <tname>\n";
            }
        }
        else if (cmd == "UPDATE" && tokens.size() > 5) {
            // UPDATE <tname> SET <col> = <val> [WHERE row = <n>]
            std::string tname = tokens[1];
            if (toUpper(tokens[2]) == "SET" && tokens.size() > 4) {
                std::string col = tokens[3];
                if (tokens[4] == "=" && tokens.size() > 5) {
                    std::string val = tokens[5];
                    int row = 0;
                    if (tokens.size() > 9 && toUpper(tokens[6]) == "WHERE" && tokens[7] == "row" && tokens[8] == "=") {
                        row = std::stoi(tokens[9]);
                    }
                    RecordManager::getInstance().updateRecord(tname, col, val, row);
                }
                else {
                    std::cout << "Err: 语法错误，正确格式: UPDATE <tname> SET <col> = <val>\n";
                }
            }
            else {
                std::cout << "Err: 语法错误，正确格式: UPDATE <tname> SET <col> = <val>\n";
            }
        }
        else if (cmd == "DELETE" && tokens.size() > 2) {
            // DELETE FROM <tname> [WHERE row = <n>]
            if (toUpper(tokens[1]) == "FROM" && tokens.size() > 2) {
                std::string tname = tokens[2];
                int row = -1;  // -1 表示删除所有
                if (tokens.size() > 6 && toUpper(tokens[3]) == "WHERE" && tokens[4] == "row" && tokens[5] == "=") {
                    row = std::stoi(tokens[6]);
                }
                RecordManager::getInstance().deleteRecord(tname, row);
            }
            else {
                std::cout << "Err: 语法错误，正确格式: DELETE FROM <tname>\n";
            }
        }
        else if (cmd == "EXIT" || cmd == "QUIT") {
            std::cout << "再见！\n";
            exitFlag = true;
        }
        else if (cmd == "HELP") {
            showHelp();
        }
        else {
            std::cout << "未知命令，输入 HELP 查看帮助\n";
        }
    }
    catch (const std::exception& e) {
        std::cout << "执行错误: " << e.what() << std::endl;
    }

    if (shouldExit != nullptr) {
        *shouldExit = exitFlag;
    }
    return capture.str();
}
