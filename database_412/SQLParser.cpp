#include "SQLParser.h"
#include "DatabaseManager.h"
#include "TableManager.h"
#include "FieldManager.h"
#include "RecordManager.h"
#include <set>
#include <cctype>
#include <stdexcept>

// ==================== 词法分析 ====================
enum TokenType {
    TOK_KEYWORD, TOK_IDENT, TOK_STRING, TOK_NUMBER,
    TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_EQUAL,
    TOK_STAR, TOK_SEMICOLON, TOK_END
};

struct Token {
    TokenType type;
    std::string text;
};

class Tokenizer {
    std::string sql;
    size_t pos;
public:
    Tokenizer(const std::string& s) : sql(s), pos(0) {
        trim(sql);
    }

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (pos < sql.size()) {
            skipSpaces();
            if (pos >= sql.size()) break;

            char ch = sql[pos];
            if (ch == '(') { tokens.push_back({ TOK_LPAREN, "(" }); pos++; }
            else if (ch == ')') { tokens.push_back({ TOK_RPAREN, ")" }); pos++; }
            else if (ch == ',') { tokens.push_back({ TOK_COMMA, "," }); pos++; }
            else if (ch == '=') { tokens.push_back({ TOK_EQUAL, "=" }); pos++; }
            else if (ch == '*') { tokens.push_back({ TOK_STAR, "*" }); pos++; }
            else if (ch == ';') { tokens.push_back({ TOK_SEMICOLON, ";" }); pos++; }
            else if (ch == '\'' || ch == '"') {
                tokens.push_back({ TOK_STRING, readQuoted(ch) });
            }
            else if (isdigit(ch) || ch == '-') {
                tokens.push_back({ TOK_NUMBER, readNumber() });
            }
            else {
                std::string word = readIdentifier();
                std::string upper = toUpper(word);
                if (isKeyword(upper))
                    tokens.push_back({ TOK_KEYWORD, upper });
                else
                    tokens.push_back({ TOK_IDENT, word });
            }
        }
        tokens.push_back({ TOK_END, "" });
        return tokens;
    }

private:
    void skipSpaces() {
        while (pos < sql.size() && isspace(sql[pos])) pos++;
    }

    std::string readQuoted(char quote) {
        size_t start = ++pos;
        while (pos < sql.size() && sql[pos] != quote) pos++;
        std::string val = sql.substr(start, pos - start);
        if (pos < sql.size()) pos++;
        return val;
    }

    std::string readNumber() {
        size_t start = pos;
        if (sql[pos] == '-') pos++;
        while (pos < sql.size() && (isdigit(sql[pos]) || sql[pos] == '.')) pos++;
        return sql.substr(start, pos - start);
    }

    std::string readIdentifier() {
        size_t start = pos;
        while (pos < sql.size() &&
            !isspace(sql[pos]) &&
            sql[pos] != '(' && sql[pos] != ')' &&
            sql[pos] != ',' && sql[pos] != '=' &&
            sql[pos] != ';' && sql[pos] != '*' &&
            sql[pos] != '\'' && sql[pos] != '"')
        {
            pos++;
        }
        return sql.substr(start, pos - start);
    }

    bool isKeyword(const std::string& s) {
        static const std::set<std::string> kw = {
            "CREATE","DATABASE","DROP","USE","TABLE",
            "ALTER","ADD","COLUMN","MODIFY",
            "INSERT","INTO","VALUES","SELECT","FROM",
            "WHERE","UPDATE","SET","DELETE","EXIT","QUIT","HELP"
        };
        return kw.count(s) > 0;
    }
};

// ==================== 语法分析辅助 ====================
class Parser {
    std::vector<Token> tokens;
    size_t idx;
public:
    Parser(const std::vector<Token>& toks) : tokens(toks), idx(0) {}

    Token peek() {
        if (idx < tokens.size()) return tokens[idx];
        return { TOK_END, "" };
    }

    Token consume(TokenType expected) {
        Token t = peek();
        if (t.type != expected) {
            throw std::runtime_error("语法错误: 期望 " + std::to_string(expected));
        }
        idx++;
        return t;
    }

    bool match(TokenType type) {
        if (peek().type == type) {
            idx++;
            return true;
        }
        return false;
    }

    bool matchKeyword(const std::string& kw) {
        if (peek().type == TOK_KEYWORD && peek().text == kw) {
            idx++;
            return true;
        }
        return false;
    }

    Token expectIdent() {
        Token t = peek();
        if (t.type != TOK_IDENT && t.type != TOK_KEYWORD)
            throw std::runtime_error("期望标识符");
        idx++;
        return t;
    }

    Token expectStringOrIdent() {
        Token t = peek();
        if (t.type == TOK_STRING || t.type == TOK_NUMBER || t.type == TOK_IDENT || t.type == TOK_KEYWORD) {
            idx++;
            return t;
        }
        throw std::runtime_error("期望字符串/数字/标识符");
    }

    void skipSemicolon() {
        match(TOK_SEMICOLON);
    }
};

// ==================== 各命令解析 ====================

static void parseCreate(Parser& p) {
    if (p.matchKeyword("DATABASE")) {
        Token name = p.expectIdent();
        DatabaseManager::getInstance().createDB(name.text);
    }
    else if (p.matchKeyword("TABLE")) {
        Token name = p.expectIdent();
        TableManager::getInstance().createTable(name.text);
    }
    else {
        std::cout << "Err: 未知的 CREATE 类型\n";
    }
}

static void parseDrop(Parser& p) {
    if (p.matchKeyword("DATABASE")) {
        Token name = p.expectIdent();
        DatabaseManager::getInstance().dropDB(name.text);
    }
    else if (p.matchKeyword("TABLE")) {
        Token name = p.expectIdent();
        TableManager::getInstance().dropTable(name.text);
    }
    else {
        std::cout << "Err: 未知的 DROP 类型\n";
    }
}

static void parseAlter(Parser& p) {
    if (!p.matchKeyword("TABLE"))
        throw std::runtime_error("期望 TABLE 关键字");
    Token tname = p.expectIdent();
    std::string action = p.expectIdent().text;

    if (action == "ADD") {
        p.matchKeyword("COLUMN");  // 可选
        Token col = p.expectIdent();
        std::string type = p.expectIdent().text;
        // 处理括号中的参数，如 VARCHAR(20)
        if (p.match(TOK_LPAREN)) {
            std::string param = p.expectStringOrIdent().text;
            p.consume(TOK_RPAREN);
            type += "(" + param + ")";
        }
        FieldManager::getInstance().addField(tname.text, col.text, type);
    }
    else if (action == "DROP") {
        p.matchKeyword("COLUMN");  // 可选
        Token col = p.expectIdent();
        FieldManager::getInstance().dropField(tname.text, col.text);
    }
    else if (action == "MODIFY") {
        p.matchKeyword("COLUMN");
        Token oldName = p.expectIdent();
        Token newName = p.expectIdent();
        FieldManager::getInstance().modifyField(tname.text, oldName.text, newName.text);
    }
    else {
        std::cout << "Err: 不支持 ALTER 操作\n";
    }
}

static void parseInsert(Parser& p) {
    if (!p.matchKeyword("INTO"))
        throw std::runtime_error("期望 INTO");
    Token tname = p.expectIdent();

    // 可选列列表 (col1, col2, ...)
    std::vector<std::string> cols;
    if (p.match(TOK_LPAREN)) {
        while (true) {
            Token col = p.expectIdent();
            cols.push_back(col.text);
            if (!p.match(TOK_COMMA)) break;
        }
        p.consume(TOK_RPAREN);
    }

    if (!p.matchKeyword("VALUES"))
        throw std::runtime_error("期望 VALUES");

    // 值列表
    p.match(TOK_LPAREN);  // 可选括号
    std::vector<std::string> vals;
    while (true) {
        Token val = p.expectStringOrIdent();
        vals.push_back(val.text);
        if (!p.match(TOK_COMMA)) break;
    }
    p.match(TOK_RPAREN);

    // 直接按顺序插入（当前不支持列映射）
    RecordManager::getInstance().insertRecord(tname.text, vals);
}

static void parseSelect(Parser& p) {
    if (!p.match(TOK_STAR))
        throw std::runtime_error("仅支持 SELECT * ...");
    if (!p.matchKeyword("FROM"))
        throw std::runtime_error("期望 FROM");
    Token tname = p.expectIdent();

    if (p.matchKeyword("WHERE")) {
        Token left = p.expectIdent();   // 字段名 或 row
        if (p.peek().type == TOK_EQUAL) {
            p.consume(TOK_EQUAL);
            Token right = p.expectStringOrIdent();
            if (left.text == "row") {
                // 不推荐，仅提示，仍显示全表
                std::cout << "提示: SELECT * WHERE row = n 暂未实现，显示全表\n";
                RecordManager::getInstance().selectRecords(tname.text);
            }
            else {
                RecordManager::getInstance().selectRecords(tname.text, left.text, right.text);
            }
        }
        else {
            throw std::runtime_error("WHERE 格式错误");
        }
    }
    else {
        RecordManager::getInstance().selectRecords(tname.text);
    }
}

static void parseUpdate(Parser& p) {
    Token tname = p.expectIdent();
    if (!p.matchKeyword("SET"))
        throw std::runtime_error("期望 SET");
    Token setCol = p.expectIdent();
    p.consume(TOK_EQUAL);
    Token setVal = p.expectStringOrIdent();

    if (p.matchKeyword("WHERE")) {
        Token left = p.expectIdent();
        if (p.peek().type == TOK_EQUAL) {
            p.consume(TOK_EQUAL);
            Token right = p.expectStringOrIdent();
            if (left.text == "row") {
                int rn = std::stoi(right.text);
                RecordManager::getInstance().updateRecord(tname.text, setCol.text, setVal.text, rn);
            }
            else {
                // 列条件更新（更新所有满足条件的行）
                RecordManager::getInstance().updateRecords(tname.text, setCol.text, setVal.text,
                    left.text, right.text);
            }
        }
        else {
            throw std::runtime_error("WHERE 格式错误");
        }
    }
    else {
        std::cout << "Err: UPDATE 必须包含 WHERE 条件 (row = n 或 col = val)\n";
    }
}

static void parseDelete(Parser& p) {
    if (!p.matchKeyword("FROM"))
        throw std::runtime_error("期望 FROM");
    Token tname = p.expectIdent();

    if (p.matchKeyword("WHERE")) {
        Token left = p.expectIdent();
        if (p.peek().type == TOK_EQUAL) {
            p.consume(TOK_EQUAL);
            Token right = p.expectStringOrIdent();
            if (left.text == "row") {
                int rn = std::stoi(right.text);
                RecordManager::getInstance().deleteRecord(tname.text, rn);
            }
            else {
                // 按列值删除
                RecordManager::getInstance().deleteRecords(tname.text, left.text, right.text);
            }
        }
        else {
            throw std::runtime_error("WHERE 格式错误");
        }
    }
    else {
        // 无 WHERE 则删除全部
        RecordManager::getInstance().deleteRecord(tname.text, -1);
    }
}

// ==================== SQLParser 接口 ====================
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
        << "  ALTER TABLE <t> ADD [COLUMN] <col> <type>\n"
        << "  ALTER TABLE <t> DROP [COLUMN] <col>\n"
        << "  ALTER TABLE <t> MODIFY [COLUMN] <old> <new>\n"
        << "  INSERT INTO <t> [(cols)] VALUES (vals)\n"
        << "  SELECT * FROM <t> [WHERE col = val]\n"
        << "  UPDATE <t> SET <col> = <val> WHERE <col> = <val> [or row = n]\n"
        << "  DELETE FROM <t> [WHERE col = val] [or row = n]\n"
        << "  EXIT / QUIT\n\n";
}

void SQLParser::execute(const std::string& sql) {
    std::string s = sql;
    trim(s);
    if (s.empty()) return;

    try {
        Tokenizer tokenizer(s);
        std::vector<Token> tokens = tokenizer.tokenize();
        Parser p(tokens);

        Token first = p.peek();
        if (first.type == TOK_END) return;

        std::string cmd = first.text;

        if (cmd == "CREATE") {
            p.consume(TOK_KEYWORD);
            parseCreate(p);
        }
        else if (cmd == "DROP") {
            p.consume(TOK_KEYWORD);
            parseDrop(p);
        }
        else if (cmd == "USE") {
            p.consume(TOK_KEYWORD);
            Token db = p.expectIdent();
            DatabaseManager::getInstance().useDB(db.text);
        }
        else if (cmd == "ALTER") {
            p.consume(TOK_KEYWORD);
            parseAlter(p);
        }
        else if (cmd == "INSERT") {
            p.consume(TOK_KEYWORD);
            parseInsert(p);
        }
        else if (cmd == "SELECT") {
            p.consume(TOK_KEYWORD);
            parseSelect(p);
        }
        else if (cmd == "UPDATE") {
            p.consume(TOK_KEYWORD);
            parseUpdate(p);
        }
        else if (cmd == "DELETE") {
            p.consume(TOK_KEYWORD);
            parseDelete(p);
        }
        else if (cmd == "EXIT" || cmd == "QUIT") {
            std::cout << "再见！\n";
            exit(0);
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
}
