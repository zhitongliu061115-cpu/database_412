#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#include "common.h"

class SQLParser {
public:
    static SQLParser& getInstance();

    SQLParser(const SQLParser&) = delete;
    SQLParser& operator=(const SQLParser&) = delete;

    void execute(const std::string& sql);

private:
    SQLParser() = default;
    ~SQLParser() = default;
    void showHelp();
};

#endif // SQL_PARSER_H#pragma once
