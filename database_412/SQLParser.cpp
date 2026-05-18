#include "SQLParser.h"

#include "DatabaseManager.h"
#include "BackupManager.h"
#include "FieldManager.h"
#include "FileManager.h"
#include "IndexManager.h"
#include "RecordManager.h"
#include "SecurityManager.h"
#include "TableManager.h"
#include "Transaction.h"
#include "LockManager.h"     // 新增：锁管理器头文件

#include <cctype>
#include <memory>
#include <set>
#include <stdexcept>

enum TokenType {
    TOK_KEYWORD, TOK_IDENT, TOK_STRING, TOK_NUMBER,
    TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_EQUAL,
    TOK_STAR, TOK_SEMICOLON, TOK_LT, TOK_GT, TOK_LE, TOK_GE, TOK_NE, TOK_END
};

struct Token {
    TokenType type;
    std::string text;
};

class Tokenizer {
public:
    explicit Tokenizer(const std::string& s) : sql(s), pos(0) {
        trim(sql);
    }

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (pos < sql.size()) {
            skipSpaces();
            if (pos >= sql.size()) break;

            const char ch = sql[pos];
            if (ch == '(') {
                tokens.push_back({ TOK_LPAREN, "(" });
                ++pos;
            }
            else if (ch == ')') {
                tokens.push_back({ TOK_RPAREN, ")" });
                ++pos;
            }
            else if (ch == ',') {
                tokens.push_back({ TOK_COMMA, "," });
                ++pos;
            }
            else if (ch == '=') {
                tokens.push_back({ TOK_EQUAL, "=" });
                ++pos;
            }
            else if (ch == '*') {
                tokens.push_back({ TOK_STAR, "*" });
                ++pos;
            }
            else if (ch == ';') {
                tokens.push_back({ TOK_SEMICOLON, ";" });
                ++pos;
            }
            else if (ch == '<') {
                if (pos + 1 < sql.size() && sql[pos + 1] == '=') {
                    tokens.push_back({ TOK_LE, "<=" });
                    pos += 2;
                }
                else if (pos + 1 < sql.size() && sql[pos + 1] == '>') {
                    tokens.push_back({ TOK_NE, "<>" });
                    pos += 2;
                }
                else {
                    tokens.push_back({ TOK_LT, "<" });
                    ++pos;
                }
            }
            else if (ch == '>') {
                if (pos + 1 < sql.size() && sql[pos + 1] == '=') {
                    tokens.push_back({ TOK_GE, ">=" });
                    pos += 2;
                }
                else {
                    tokens.push_back({ TOK_GT, ">" });
                    ++pos;
                }
            }
            else if (ch == '!') {
                if (pos + 1 < sql.size() && sql[pos + 1] == '=') {
                    tokens.push_back({ TOK_NE, "!=" });
                    pos += 2;
                }
                else {
                    tokens.push_back({ TOK_IDENT, "!" });
                    ++pos;
                }
            }
            else if (ch == '\'' || ch == '"') {
                tokens.push_back({ TOK_STRING, readQuoted(ch) });
            }
            else if (isdigit(static_cast<unsigned char>(ch)) ||
                (ch == '-' && pos + 1 < sql.size() && isdigit(static_cast<unsigned char>(sql[pos + 1])))) {
                tokens.push_back({ TOK_NUMBER, readNumber() });
            }
            else {
                const std::string word = readIdentifier();
                const std::string upper = toUpper(word);
                if (isKeyword(upper)) tokens.push_back({ TOK_KEYWORD, upper });
                else tokens.push_back({ TOK_IDENT, word });
            }
        }
        tokens.push_back({ TOK_END, "" });
        return tokens;
    }

private:
    void skipSpaces() {
        while (pos < sql.size() && isspace(static_cast<unsigned char>(sql[pos]))) ++pos;
    }

    std::string readQuoted(char quote) {
        ++pos;
        std::string value;
        while (pos < sql.size()) {
            const char ch = sql[pos];
            if (ch == '\\' && pos + 1 < sql.size()) {
                const char next = sql[pos + 1];
                if (next == quote || next == '\\') {
                    value.push_back(next);
                    pos += 2;
                    continue;
                }
                value.push_back(ch);
                value.push_back(next);
                pos += 2;
                continue;
            }
            if (ch == quote) {
                ++pos;
                return value;
            }
            value.push_back(ch);
            ++pos;
        }
        throw std::runtime_error("unterminated string");
    }

    std::string readNumber() {
        const size_t start = pos;
        if (sql[pos] == '-') ++pos;
        while (pos < sql.size() &&
            (isdigit(static_cast<unsigned char>(sql[pos])) || sql[pos] == '.')) {
            ++pos;
        }
        return sql.substr(start, pos - start);
    }

    std::string readIdentifier() {
        const size_t start = pos;
        while (pos < sql.size() && !isspace(static_cast<unsigned char>(sql[pos])) &&
            sql[pos] != '(' && sql[pos] != ')' && sql[pos] != ',' &&
            sql[pos] != '=' && sql[pos] != ';' && sql[pos] != '*' &&
            sql[pos] != '\'' && sql[pos] != '"' && sql[pos] != '<' &&
            sql[pos] != '>' && sql[pos] != '!') {
            ++pos;
        }
        return sql.substr(start, pos - start);
    }

    bool isKeyword(const std::string& s) {
        static const std::set<std::string> keywords = {
            "CREATE", "DATABASE", "DROP", "USE", "TABLE", "ALTER", "ADD", "COLUMN", "MODIFY",
            "INSERT", "INTO", "VALUES", "SELECT", "FROM", "WHERE", "UPDATE", "SET", "DELETE",
            "AND", "OR", "NOT", "ORDER", "BY", "GROUP", "ASC", "DESC", "COUNT", "SUM", "AVG",
            "MIN", "MAX", "HAVING", "EXIT", "QUIT", "HELP", "NULL", "PRIMARY", "KEY",
            "INDEX", "UNIQUE",
            "BEGIN", "COMMIT", "ROLLBACK", "USER", "IDENTIFIED", "LOGIN", "LOGOUT",
            "GRANT", "REVOKE", "ON", "TO", "FROM", "SHOW", "USERS", "GRANTS", "FOR", "ALL",
            "BACKUP", "RESTORE", "LOCKS"     // 新增 LOCKS 关键字
        };
        return keywords.count(s) > 0;
    }

    std::string sql;
    size_t pos;
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& t) : tokens(t), idx(0) {}

    Token peek() const {
        return idx < tokens.size() ? tokens[idx] : Token{ TOK_END, "" };
    }

    Token consume(TokenType expected) {
        const Token t = peek();
        if (t.type != expected) throw std::runtime_error("syntax error");
        ++idx;
        return t;
    }

    bool match(TokenType t) {
        if (peek().type == t) {
            ++idx;
            return true;
        }
        return false;
    }

    bool matchKeyword(const std::string& keyword) {
        if (peek().type == TOK_KEYWORD && peek().text == keyword) {
            ++idx;
            return true;
        }
        return false;
    }

    Token expectIdent() {
        const Token t = peek();
        if (t.type == TOK_IDENT || t.type == TOK_KEYWORD) {
            ++idx;
            return t;
        }
        throw std::runtime_error("expect identifier");
    }

    Token expectStringOrIdent() {
        const Token t = peek();
        if (t.type == TOK_STRING || t.type == TOK_NUMBER || t.type == TOK_IDENT) {
            ++idx;
            return t;
        }
        throw std::runtime_error("expect value");
    }

private:
    std::vector<Token> tokens;
    size_t idx;
};

static std::unique_ptr<ExprNode> parseExpr(Parser& p, const std::vector<FieldInfo>& fields);
static std::unique_ptr<ExprNode> parseAndExpr(Parser& p, const std::vector<FieldInfo>& fields);
static std::unique_ptr<ExprNode> parseComparison(Parser& p, const std::vector<FieldInfo>& fields);
static std::unique_ptr<ExprNode> parsePrimary(Parser& p, const std::vector<FieldInfo>& fields);

static DataType getFieldType(const std::string& colName, const std::vector<FieldInfo>& fields) {
    for (const auto& field : fields) {
        if (std::string(field.name) == colName) return field.type;
    }
    return DataType::VARCHAR;
}

static std::unique_ptr<ExprNode> parsePrimary(Parser& p, const std::vector<FieldInfo>& fields) {
    if (p.match(TOK_LPAREN)) {
        auto node = parseExpr(p, fields);
        p.consume(TOK_RPAREN);
        return node;
    }

    const Token t = p.peek();
    if (t.type == TOK_IDENT || t.type == TOK_KEYWORD) {
        p.consume(t.type);
        return ExprNode::makeLeafCol(t.text, getFieldType(t.text, fields));
    }
    if (t.type == TOK_STRING || t.type == TOK_NUMBER) {
        DataType type = DataType::VARCHAR;
        if (t.type == TOK_NUMBER) {
            type = (t.text.find('.') != std::string::npos) ? DataType::DOUBLE : DataType::INT;
        }
        p.consume(t.type);
        return ExprNode::makeLeafConst(t.text, type);
    }

    throw std::runtime_error("bad expression");
}

static std::unique_ptr<ExprNode> parseComparison(Parser& p, const std::vector<FieldInfo>& fields) {
    auto left = parsePrimary(p, fields);
    CompOp op = CompOp::EQ;

    if (p.match(TOK_EQUAL)) op = CompOp::EQ;
    else if (p.match(TOK_LT)) op = CompOp::LT;
    else if (p.match(TOK_GT)) op = CompOp::GT;
    else if (p.match(TOK_LE)) op = CompOp::LE;
    else if (p.match(TOK_GE)) op = CompOp::GE;
    else if (p.match(TOK_NE)) op = CompOp::NE;
    else throw std::runtime_error("WHERE needs comparison");

    auto right = parsePrimary(p, fields);
    return ExprNode::makeComp(op, std::move(left), std::move(right));
}

static std::unique_ptr<ExprNode> parseAndExpr(Parser& p, const std::vector<FieldInfo>& fields) {
    auto left = parseComparison(p, fields);
    while (p.matchKeyword("AND")) {
        auto right = parseComparison(p, fields);
        left = ExprNode::makeLogic(LogicOp::AND, std::move(left), std::move(right));
    }
    return left;
}

static std::unique_ptr<ExprNode> parseExpr(Parser& p, const std::vector<FieldInfo>& fields) {
    auto left = parseAndExpr(p, fields);
    while (p.matchKeyword("OR")) {
        auto right = parseAndExpr(p, fields);
        left = ExprNode::makeLogic(LogicOp::OR, std::move(left), std::move(right));
    }
    return left;
}

static void parseCreate(Parser& p) {
    if (p.matchKeyword("DATABASE")) {
        DatabaseManager::getInstance().createDB(p.expectIdent().text);
    }
    else if (p.matchKeyword("TABLE")) {
        TableManager::getInstance().createTable(p.expectIdent().text);
    }
    else if (p.matchKeyword("UNIQUE")) {
        if (!p.matchKeyword("INDEX")) throw std::runtime_error("expect INDEX");
        const Token idxName = p.expectIdent();
        if (!p.matchKeyword("ON")) throw std::runtime_error("expect ON");
        const Token tname = p.expectIdent();
        p.consume(TOK_LPAREN);
        const Token col = p.expectIdent();
        p.consume(TOK_RPAREN);
        IndexManager::getInstance().createIndex(tname.text, col.text, idxName.text, true);
    }
    else if (p.matchKeyword("INDEX")) {
        const Token idxName = p.expectIdent();
        if (!p.matchKeyword("ON")) throw std::runtime_error("expect ON");
        const Token tname = p.expectIdent();
        p.consume(TOK_LPAREN);
        const Token col = p.expectIdent();
        p.consume(TOK_RPAREN);
        IndexManager::getInstance().createIndex(tname.text, col.text, idxName.text, false);
    }
    else {
        std::cout << "Err: unknown CREATE type\n";
    }
}

static void parseDrop(Parser& p) {
    if (p.matchKeyword("DATABASE")) {
        DatabaseManager::getInstance().dropDB(p.expectIdent().text);
    }
    else if (p.matchKeyword("TABLE")) {
        TableManager::getInstance().dropTable(p.expectIdent().text);
    }
    else if (p.matchKeyword("INDEX")) {
        const Token idxName = p.expectIdent();
        std::string tname;
        if (p.matchKeyword("ON")) {
            tname = p.expectIdent().text;
        }
        IndexManager::getInstance().dropIndexByName(idxName.text, tname);
    }
    else {
        std::cout << "Err: unknown DROP type\n";
    }
}

static void parseAlter(Parser& p) {
    if (!p.matchKeyword("TABLE")) throw std::runtime_error("expect TABLE");
    const Token tname = p.expectIdent();
    const std::string action = p.expectIdent().text;

    if (action == "ADD") {
        p.matchKeyword("COLUMN");
        const Token col = p.expectIdent();
        std::string type = p.expectIdent().text;
        if (p.match(TOK_LPAREN)) {
            const std::string param = p.expectStringOrIdent().text;
            p.consume(TOK_RPAREN);
            type += "(" + param + ")";
        }

        bool notNull = false;
        bool primaryKey = false;
        while (true) {
            if (p.matchKeyword("NOT") && p.matchKeyword("NULL")) notNull = true;
            else if (p.matchKeyword("PRIMARY") && p.matchKeyword("KEY")) primaryKey = true;
            else break;
        }
        FieldManager::getInstance().addField(tname.text, col.text, type, notNull, primaryKey);
    }
    else if (action == "DROP") {
        p.matchKeyword("COLUMN");
        FieldManager::getInstance().dropField(tname.text, p.expectIdent().text);
    }
    else if (action == "MODIFY") {
        p.matchKeyword("COLUMN");
        const Token oldName = p.expectIdent();
        const Token newName = p.expectIdent();
        FieldManager::getInstance().modifyField(tname.text, oldName.text, newName.text);
    }
    else {
        std::cout << "Err: unsupported ALTER action\n";
    }
}

static void parseInsert(Parser& p) {
    if (!p.matchKeyword("INTO")) throw std::runtime_error("expect INTO");
    const Token tname = p.expectIdent();

    std::vector<std::string> cols;
    std::vector<std::string> vals;
    if (p.match(TOK_LPAREN)) {
        while (true) {
            cols.push_back(p.expectIdent().text);
            if (!p.match(TOK_COMMA)) break;
        }
        p.consume(TOK_RPAREN);
    }

    if (!p.matchKeyword("VALUES")) throw std::runtime_error("expect VALUES");
    p.match(TOK_LPAREN);
    while (true) {
        vals.push_back(p.expectStringOrIdent().text);
        if (!p.match(TOK_COMMA)) break;
    }
    p.match(TOK_RPAREN);

    if (TransactionManager::getInstance().hasActiveTransaction()) {
        if (cols.empty()) RecordManager::getInstance().insertRecordTx(tname.text, vals);
        else RecordManager::getInstance().insertRecordTx(tname.text, cols, vals);
    }
    else {
        if (cols.empty()) RecordManager::getInstance().insertRecord(tname.text, vals);
        else RecordManager::getInstance().insertRecord(tname.text, cols, vals);
    }
}

static void parseSelect(Parser& p) {
    std::vector<std::string> outCols;
    if (!p.match(TOK_STAR)) {
        while (true) {
            outCols.push_back(p.expectIdent().text);
            if (!p.match(TOK_COMMA)) break;
        }
    }
    if (!p.matchKeyword("FROM")) throw std::runtime_error("expect FROM");
    const Token tname = p.expectIdent();

    std::unique_ptr<ExprNode> whereCond;
    if (p.matchKeyword("WHERE")) {
        const auto fields = FieldManager::getInstance().getFields(tname.text);
        whereCond = parseExpr(p, fields);
    }

    std::string groupByCol;
    AggFuncType aggFunc = AggFuncType::NONE;
    std::string aggCol;
    if (p.matchKeyword("GROUP") && p.matchKeyword("BY")) {
        groupByCol = p.expectIdent().text;
    }

    for (auto& col : outCols) {
        const std::string upper = toUpper(col);
        if (upper.find("COUNT(") == 0) {
            aggFunc = AggFuncType::COUNT;
            aggCol = col.substr(6, col.size() - 7);
        }
        else if (upper.find("SUM(") == 0) {
            aggFunc = AggFuncType::SUM;
            aggCol = col.substr(4, col.size() - 5);
        }
        else if (upper.find("AVG(") == 0) {
            aggFunc = AggFuncType::AVG;
            aggCol = col.substr(4, col.size() - 5);
        }
        else if (upper.find("MIN(") == 0) {
            aggFunc = AggFuncType::MIN;
            aggCol = col.substr(4, col.size() - 5);
        }
        else if (upper.find("MAX(") == 0) {
            aggFunc = AggFuncType::MAX;
            aggCol = col.substr(4, col.size() - 5);
        }
    }
    if (aggFunc != AggFuncType::NONE) outCols.clear();

    std::string orderByCol;
    bool orderAsc = true;
    if (p.matchKeyword("ORDER") && p.matchKeyword("BY")) {
        orderByCol = p.expectIdent().text;
        if (p.matchKeyword("DESC")) orderAsc = false;
        else p.matchKeyword("ASC");
    }

    RecordManager::getInstance().selectRecords(
        tname.text, outCols, whereCond.get(), orderByCol, orderAsc, groupByCol, aggFunc, aggCol);
}

static void parseUpdate(Parser& p) {
    const Token tname = p.expectIdent();
    if (!p.matchKeyword("SET")) throw std::runtime_error("expect SET");
    const Token setCol = p.expectIdent();
    p.consume(TOK_EQUAL);
    const Token setVal = p.expectStringOrIdent();

    std::unique_ptr<ExprNode> whereCond;
    if (p.matchKeyword("WHERE")) {
        const auto fields = FieldManager::getInstance().getFields(tname.text);
        whereCond = parseExpr(p, fields);
    }
    else {
        std::cout << "Err: UPDATE must have WHERE\n";
        return;
    }

    if (whereCond &&
        whereCond->type == ExprNode::UNARY &&
        whereCond->left && whereCond->right &&
        whereCond->left->type == ExprNode::LEAF &&
        whereCond->right->type == ExprNode::LEAF &&
        whereCond->left->colName == "row") {
        const int row = std::stoi(whereCond->right->value);
        RecordManager::getInstance().updateRecord(tname.text, setCol.text, setVal.text, row);
        return;
    }

    if (TransactionManager::getInstance().hasActiveTransaction()) {
        RecordManager::getInstance().updateRecordsTx(tname.text, setCol.text, setVal.text, whereCond.get());
    }
    else {
        RecordManager::getInstance().updateRecords(tname.text, setCol.text, setVal.text, whereCond.get());
    }
}

static void parseDelete(Parser& p) {
    if (!p.matchKeyword("FROM")) throw std::runtime_error("expect FROM");
    const Token tname = p.expectIdent();

    std::unique_ptr<ExprNode> whereCond;
    if (p.matchKeyword("WHERE")) {
        const auto fields = FieldManager::getInstance().getFields(tname.text);
        whereCond = parseExpr(p, fields);
    }
    else {
        RecordManager::getInstance().deleteRecord(tname.text, -1);
        return;
    }

    if (whereCond &&
        whereCond->type == ExprNode::UNARY &&
        whereCond->left && whereCond->right &&
        whereCond->left->type == ExprNode::LEAF &&
        whereCond->right->type == ExprNode::LEAF &&
        whereCond->left->colName == "row") {
        const int row = std::stoi(whereCond->right->value);
        RecordManager::getInstance().deleteRecord(tname.text, row);
        return;
    }

    if (TransactionManager::getInstance().hasActiveTransaction()) {
        RecordManager::getInstance().deleteRecordsTx(tname.text, whereCond.get());
    }
    else {
        RecordManager::getInstance().deleteRecords(tname.text, whereCond.get());
    }
}

static void parseBegin(Parser&) {
    TransactionManager::getInstance().beginTransaction();
}

static void parseCommit(Parser&) {
    if (!TransactionManager::getInstance().hasActiveTransaction()) {
        std::cout << "Err: no active transaction\n";
        return;
    }
    TransactionManager::getInstance().commitTransaction(TransactionManager::getInstance().getCurrentTxId());
}

static void parseRollback(Parser&) {
    if (!TransactionManager::getInstance().hasActiveTransaction()) {
        std::cout << "Err: no active transaction\n";
        return;
    }
    TransactionManager::getInstance().rollbackTransaction(TransactionManager::getInstance().getCurrentTxId());
}

static uint32_t parsePrivilegeList(Parser& p) {
    uint32_t mask = 0;
    while (true) {
        const Token privilege = p.expectIdent();
        uint32_t current = 0;
        if (!SecurityManager::privilegeFromName(privilege.text, current)) {
            throw std::runtime_error("unknown privilege");
        }
        mask |= current;
        if (!p.match(TOK_COMMA)) break;
    }
    return mask;
}

static void parseCreateUser(Parser& p) {
    if (!SecurityManager::getInstance().requireAdmin()) return;
    const Token username = p.expectIdent();
    if (!p.matchKeyword("IDENTIFIED") || !p.matchKeyword("BY")) {
        throw std::runtime_error("expect IDENTIFIED BY");
    }
    const Token password = p.expectStringOrIdent();
    SecurityManager::getInstance().createUser(username.text, password.text, false);
}

static void parseLogin(Parser& p) {
    const Token username = p.expectIdent();
    const Token password = p.expectStringOrIdent();
    SecurityManager::getInstance().login(username.text, password.text);
}

static void parseGrant(Parser& p) {
    if (!SecurityManager::getInstance().requireAdmin()) return;
    const uint32_t mask = parsePrivilegeList(p);
    if (!p.matchKeyword("ON")) throw std::runtime_error("expect ON");
    const Token table = p.expectIdent();
    if (!p.matchKeyword("TO")) throw std::runtime_error("expect TO");
    const Token username = p.expectIdent();
    if (g_current_db.empty()) {
        std::cout << "Err: no active database context\n";
        return;
    }
    SecurityManager::getInstance().grantPrivilege(username.text, g_current_db, table.text, mask);
}

static void parseRevoke(Parser& p) {
    if (!SecurityManager::getInstance().requireAdmin()) return;
    const uint32_t mask = parsePrivilegeList(p);
    if (!p.matchKeyword("ON")) throw std::runtime_error("expect ON");
    const Token table = p.expectIdent();
    if (!p.matchKeyword("FROM")) throw std::runtime_error("expect FROM");
    const Token username = p.expectIdent();
    if (g_current_db.empty()) {
        std::cout << "Err: no active database context\n";
        return;
    }
    SecurityManager::getInstance().revokePrivilege(username.text, g_current_db, table.text, mask);
}

static void parseShow(Parser& p) {
    if (p.matchKeyword("USERS")) {
        SecurityManager::getInstance().showUsers();
        return;
    }
    if (p.matchKeyword("GRANTS")) {
        if (!p.matchKeyword("FOR")) throw std::runtime_error("expect FOR");
        SecurityManager::getInstance().showGrants(p.expectIdent().text);
        return;
    }
    // 新增：SHOW LOCKS 命令
    if (p.matchKeyword("LOCKS")) {
        LockManager::getInstance().printLocks();
        return;
    }
    throw std::runtime_error("unknown SHOW command");
}

static bool parseBackup(Parser& p) {
    if (!SecurityManager::getInstance().requireAdmin()) return false;
    if (!p.matchKeyword("DATABASE")) throw std::runtime_error("expect DATABASE");
    const Token dbName = p.expectIdent();
    if (!p.matchKeyword("TO")) throw std::runtime_error("expect TO");
    const Token filePath = p.expectStringOrIdent();
    return BackupManager::getInstance().backupDatabase(dbName.text, filePath.text);
}

static bool parseRestore(Parser& p) {
    if (!SecurityManager::getInstance().requireAdmin()) return false;
    if (!p.matchKeyword("DATABASE")) throw std::runtime_error("expect DATABASE");
    const Token dbName = p.expectIdent();
    if (!p.matchKeyword("FROM")) throw std::runtime_error("expect FROM");
    const Token filePath = p.expectStringOrIdent();
    return BackupManager::getInstance().restoreDatabase(dbName.text, filePath.text);
}

static bool shouldAppendLog(const std::string& sql) {
    const std::string upper = toUpper(sql);
    return upper.rfind("CREATE TABLE", 0) == 0 ||
        upper.rfind("ALTER TABLE", 0) == 0 ||
        upper.rfind("DROP TABLE", 0) == 0 ||
        upper.rfind("INSERT INTO", 0) == 0 ||
        upper.rfind("UPDATE ", 0) == 0 ||
        upper.rfind("DELETE FROM", 0) == 0;
}

static bool shouldSkipLogForActiveTransaction(const std::string& sql) {
    if (!TransactionManager::getInstance().hasActiveTransaction()) return false;
    const std::string upper = toUpper(sql);
    return upper.rfind("INSERT INTO", 0) == 0 ||
        upper.rfind("UPDATE ", 0) == 0 ||
        upper.rfind("DELETE FROM", 0) == 0;
}

SQLParser& SQLParser::getInstance() {
    static SQLParser instance;
    return instance;
}

void SQLParser::showHelp() {
    std::cout << "\nSupported commands:\n"
        << "  LOGIN <user> <password>\n"
        << "  LOGOUT\n"
        << "  CREATE USER <user> IDENTIFIED BY <password>\n"
        << "  DROP USER <user>\n"
        << "  GRANT <privileges> ON </td> TO <user>\n"
        << "  REVOKE <privileges> ON <tr> FROM <user>\n"
        << "  SHOW USERS\n"
        << "  SHOW GRANTS FOR <user>\n"
        << "  SHOW LOCKS                      -- 显示当前锁状态\n"   // 新增
        << "  BACKUP DATABASE <db> TO <file>\n"
        << "  RESTORE DATABASE <db> FROM <file>\n"
        << "  CREATE DATABASE <name>\n"
        << "  DROP DATABASE <name>\n"
        << "  USE <db>\n"
        << "  CREATE TABLE <name>\n"
        << "  DROP TABLE <name>\n"
        << "  CREATE [UNIQUE] INDEX <idx> ON <table>(<col>)\n"
        << "  DROP INDEX <idx> [ON </table>]\n"
        << "  ALTER TABLE <t> ADD [COLUMN] <col> <type> [NOT NULL]\n"
        << "  ALTER TABLE <t> DROP [COLUMN] <col>\n"
        << "  ALTER TABLE <t> MODIFY [COLUMN] <old> <new>\n"
        << "  INSERT INTO <t> [(cols)] VALUES (vals)\n"
        << "  SELECT [cols|*] FROM <t> [WHERE expr] [GROUP BY col] [ORDER BY col [ASC|DESC]]\n"
        << "  UPDATE <t> SET col=val WHERE expr\n"
        << "  DELETE FROM <t> [WHERE expr]\n"
        << "  BEGIN\n"
        << "  COMMIT\n"
        << "  ROLLBACK\n"
        << "Privileges: SELECT, INSERT, UPDATE, DELETE, ALTER, DROP, ALL\n";
}

bool SQLParser::execute(const std::string& sql) {
    std::string s = sql;
    trim(s);
    if (s.empty()) return true;

    try {
        Tokenizer tokenizer(s);
        Parser p(tokenizer.tokenize());
        const Token first = p.peek();
        if (first.type == TOK_END) return true;

        const std::string cmd = first.text;
        bool success = true;
        if (cmd == "BEGIN") {
            p.consume(TOK_KEYWORD);
            parseBegin(p);
        }
        else if (cmd == "COMMIT") {
            p.consume(TOK_KEYWORD);
            const bool hadActive = TransactionManager::getInstance().hasActiveTransaction();
            parseCommit(p);
            success = hadActive && !TransactionManager::getInstance().hasActiveTransaction();
        }
        else if (cmd == "ROLLBACK") {
            p.consume(TOK_KEYWORD);
            const bool hadActive = TransactionManager::getInstance().hasActiveTransaction();
            parseRollback(p);
            success = hadActive && !TransactionManager::getInstance().hasActiveTransaction();
        }
        else if (cmd == "CREATE") {
            p.consume(TOK_KEYWORD);
            if (p.matchKeyword("USER")) {
                const std::string userFile = joinPath(g_root, "users.db");
                const auto beforeSize = FileManager::getInstance().readAllStruct<UserInfo>(userFile).size();
                parseCreateUser(p);
                const auto afterSize = FileManager::getInstance().readAllStruct<UserInfo>(userFile).size();
                success = afterSize > beforeSize;
            }
            else {
                if (p.peek().type == TOK_KEYWORD && p.peek().text == "DATABASE") {
                    const auto beforeSize = DatabaseManager::getInstance().getAllDBs().size();
                    parseCreate(p);
                    const auto afterSize = DatabaseManager::getInstance().getAllDBs().size();
                    success = afterSize > beforeSize;
                }
                else if (p.peek().type == TOK_KEYWORD && p.peek().text == "TABLE") {
                    if (g_current_db.empty()) {
                        parseCreate(p);
                        success = false;
                    }
                    else {
                        const auto beforeSize = TableManager::getInstance().getAllTables().size();
                        parseCreate(p);
                        const auto afterSize = TableManager::getInstance().getAllTables().size();
                        success = afterSize > beforeSize;
                    }
                }
                else {
                    parseCreate(p);
                }
            }
        }
        else if (cmd == "DROP") {
            p.consume(TOK_KEYWORD);
            if (p.matchKeyword("USER")) {
                if (!SecurityManager::getInstance().requireAdmin()) return false;
                success = SecurityManager::getInstance().dropUser(p.expectIdent().text);
            }
            else {
                if (p.peek().type == TOK_KEYWORD && p.peek().text == "DATABASE") {
                    const auto beforeSize = DatabaseManager::getInstance().getAllDBs().size();
                    parseDrop(p);
                    const auto afterSize = DatabaseManager::getInstance().getAllDBs().size();
                    success = afterSize + 1 == beforeSize;
                }
                else if (p.peek().type == TOK_KEYWORD && p.peek().text == "TABLE") {
                    if (g_current_db.empty()) {
                        parseDrop(p);
                        success = false;
                    }
                    else {
                        const auto beforeSize = TableManager::getInstance().getAllTables().size();
                        parseDrop(p);
                        const auto afterSize = TableManager::getInstance().getAllTables().size();
                        success = afterSize + 1 == beforeSize;
                    }
                }
                else {
                    parseDrop(p);
                }
            }
        }
        else if (cmd == "LOGIN") {
            p.consume(TOK_KEYWORD);
            const Token username = p.expectIdent();
            const Token password = p.expectStringOrIdent();
            success = SecurityManager::getInstance().login(username.text, password.text);
        }
        else if (cmd == "LOGOUT") {
            p.consume(TOK_KEYWORD);
            const bool wasLoggedIn = SecurityManager::getInstance().isLoggedIn();
            SecurityManager::getInstance().logout();
            success = wasLoggedIn && !SecurityManager::getInstance().isLoggedIn();
        }
        else if (cmd == "GRANT") {
            p.consume(TOK_KEYWORD);
            if (!SecurityManager::getInstance().requireAdmin()) return false;
            const uint32_t mask = parsePrivilegeList(p);
            if (!p.matchKeyword("ON")) throw std::runtime_error("expect ON");
            const Token table = p.expectIdent();
            if (!p.matchKeyword("TO")) throw std::runtime_error("expect TO");
            const Token username = p.expectIdent();
            if (g_current_db.empty()) {
                std::cout << "Err: no active database context\n";
                return false;
            }
            success = SecurityManager::getInstance().grantPrivilege(username.text, g_current_db, table.text, mask);
        }
        else if (cmd == "REVOKE") {
            p.consume(TOK_KEYWORD);
            if (!SecurityManager::getInstance().requireAdmin()) return false;
            const uint32_t mask = parsePrivilegeList(p);
            if (!p.matchKeyword("ON")) throw std::runtime_error("expect ON");
            const Token table = p.expectIdent();
            if (!p.matchKeyword("FROM")) throw std::runtime_error("expect FROM");
            const Token username = p.expectIdent();
            if (g_current_db.empty()) {
                std::cout << "Err: no active database context\n";
                return false;
            }
            success = SecurityManager::getInstance().revokePrivilege(username.text, g_current_db, table.text, mask);
        }
        else if (cmd == "SHOW") {
            p.consume(TOK_KEYWORD);
            parseShow(p);
            success = true;
        }
        else if (cmd == "BACKUP") {
            p.consume(TOK_KEYWORD);
            success = parseBackup(p);
        }
        else if (cmd == "RESTORE") {
            p.consume(TOK_KEYWORD);
            success = parseRestore(p);
        }
        else if (cmd == "USE") {
            p.consume(TOK_KEYWORD);
            success = DatabaseManager::getInstance().useDB(p.expectIdent().text);
        }
        else if (cmd == "ALTER") {
            p.consume(TOK_KEYWORD);
            if (!p.matchKeyword("TABLE")) throw std::runtime_error("expect TABLE");
            const Token tname = p.expectIdent();
            const std::vector<FieldInfo> beforeFields = FieldManager::getInstance().getFields(tname.text);
            const std::string action = p.expectIdent().text;
            if (action == "ADD") {
                p.matchKeyword("COLUMN");
                const Token col = p.expectIdent();
                std::string type = p.expectIdent().text;
                if (p.match(TOK_LPAREN)) {
                    const std::string param = p.expectStringOrIdent().text;
                    p.consume(TOK_RPAREN);
                    type += "(" + param + ")";
                }

                bool notNull = false;
                bool primaryKey = false;
                while (true) {
                    if (p.matchKeyword("NOT") && p.matchKeyword("NULL")) notNull = true;
                    else if (p.matchKeyword("PRIMARY") && p.matchKeyword("KEY")) primaryKey = true;
                    else break;
                }
                success = FieldManager::getInstance().addField(tname.text, col.text, type, notNull, primaryKey);
            }
            else if (action == "DROP") {
                p.matchKeyword("COLUMN");
                success = FieldManager::getInstance().dropField(tname.text, p.expectIdent().text);
            }
            else if (action == "MODIFY") {
                p.matchKeyword("COLUMN");
                const Token oldName = p.expectIdent();
                const Token newName = p.expectIdent();
                success = FieldManager::getInstance().modifyField(tname.text, oldName.text, newName.text);
            }
            else {
                std::cout << "Err: unsupported ALTER action\n";
                success = false;
            }
        }
        else if (cmd == "INSERT") {
            p.consume(TOK_KEYWORD);
            if (!p.matchKeyword("INTO")) throw std::runtime_error("expect INTO");
            const Token tname = p.expectIdent();

            std::vector<std::string> cols;
            std::vector<std::string> vals;
            if (p.match(TOK_LPAREN)) {
                while (true) {
                    cols.push_back(p.expectIdent().text);
                    if (!p.match(TOK_COMMA)) break;
                }
                p.consume(TOK_RPAREN);
            }

            if (!p.matchKeyword("VALUES")) throw std::runtime_error("expect VALUES");
            p.match(TOK_LPAREN);
            while (true) {
                vals.push_back(p.expectStringOrIdent().text);
                if (!p.match(TOK_COMMA)) break;
            }
            p.match(TOK_RPAREN);

            if (TransactionManager::getInstance().hasActiveTransaction()) {
                success = cols.empty()
                    ? RecordManager::getInstance().insertRecordTx(tname.text, vals)
                    : RecordManager::getInstance().insertRecordTx(tname.text, cols, vals);
            }
            else {
                success = cols.empty()
                    ? RecordManager::getInstance().insertRecord(tname.text, vals)
                    : RecordManager::getInstance().insertRecord(tname.text, cols, vals);
            }
        }
        else if (cmd == "SELECT") {
            p.consume(TOK_KEYWORD);
            std::vector<std::string> outCols;
            if (!p.match(TOK_STAR)) {
                while (true) {
                    outCols.push_back(p.expectIdent().text);
                    if (!p.match(TOK_COMMA)) break;
                }
            }
            if (!p.matchKeyword("FROM")) throw std::runtime_error("expect FROM");
            const Token tname = p.expectIdent();

            std::unique_ptr<ExprNode> whereCond;
            if (p.matchKeyword("WHERE")) {
                const auto fields = FieldManager::getInstance().getFields(tname.text);
                whereCond = parseExpr(p, fields);
            }

            std::string groupByCol;
            AggFuncType aggFunc = AggFuncType::NONE;
            std::string aggCol;
            if (p.matchKeyword("GROUP") && p.matchKeyword("BY")) {
                groupByCol = p.expectIdent().text;
            }

            for (auto& col : outCols) {
                const std::string upper = toUpper(col);
                if (upper.find("COUNT(") == 0) {
                    aggFunc = AggFuncType::COUNT;
                    aggCol = col.substr(6, col.size() - 7);
                }
                else if (upper.find("SUM(") == 0) {
                    aggFunc = AggFuncType::SUM;
                    aggCol = col.substr(4, col.size() - 5);
                }
                else if (upper.find("AVG(") == 0) {
                    aggFunc = AggFuncType::AVG;
                    aggCol = col.substr(4, col.size() - 5);
                }
                else if (upper.find("MIN(") == 0) {
                    aggFunc = AggFuncType::MIN;
                    aggCol = col.substr(4, col.size() - 5);
                }
                else if (upper.find("MAX(") == 0) {
                    aggFunc = AggFuncType::MAX;
                    aggCol = col.substr(4, col.size() - 5);
                }
            }
            if (aggFunc != AggFuncType::NONE) outCols.clear();

            std::string orderByCol;
            bool orderAsc = true;
            if (p.matchKeyword("ORDER") && p.matchKeyword("BY")) {
                orderByCol = p.expectIdent().text;
                if (p.matchKeyword("DESC")) orderAsc = false;
                else p.matchKeyword("ASC");
            }

            success = RecordManager::getInstance().selectRecords(
                tname.text, outCols, whereCond.get(), orderByCol, orderAsc, groupByCol, aggFunc, aggCol);
        }
        else if (cmd == "UPDATE") {
            p.consume(TOK_KEYWORD);
            const Token tname = p.expectIdent();
            if (!p.matchKeyword("SET")) throw std::runtime_error("expect SET");
            const Token setCol = p.expectIdent();
            p.consume(TOK_EQUAL);
            const Token setVal = p.expectStringOrIdent();

            std::unique_ptr<ExprNode> whereCond;
            if (p.matchKeyword("WHERE")) {
                const auto fields = FieldManager::getInstance().getFields(tname.text);
                whereCond = parseExpr(p, fields);
            }
            else {
                std::cout << "Err: UPDATE must have WHERE\n";
                return false;
            }

            if (whereCond &&
                whereCond->type == ExprNode::UNARY &&
                whereCond->left && whereCond->right &&
                whereCond->left->type == ExprNode::LEAF &&
                whereCond->right->type == ExprNode::LEAF &&
                whereCond->left->colName == "row") {
                const int row = std::stoi(whereCond->right->value);
                success = RecordManager::getInstance().updateRecord(tname.text, setCol.text, setVal.text, row);
            }
            else if (TransactionManager::getInstance().hasActiveTransaction()) {
                success = RecordManager::getInstance().updateRecordsTx(tname.text, setCol.text, setVal.text, whereCond.get());
            }
            else {
                success = RecordManager::getInstance().updateRecords(tname.text, setCol.text, setVal.text, whereCond.get());
            }
        }
        else if (cmd == "DELETE") {
            p.consume(TOK_KEYWORD);
            if (!p.matchKeyword("FROM")) throw std::runtime_error("expect FROM");
            const Token tname = p.expectIdent();

            std::unique_ptr<ExprNode> whereCond;
            if (p.matchKeyword("WHERE")) {
                const auto fields = FieldManager::getInstance().getFields(tname.text);
                whereCond = parseExpr(p, fields);
            }
            else {
                success = RecordManager::getInstance().deleteRecord(tname.text, -1);
                if (success && shouldAppendLog(s) && !shouldSkipLogForActiveTransaction(s) && !g_current_db.empty()) {
                    BackupManager::getInstance().appendLog(g_current_db, s);
                }
                return success;
            }

            if (whereCond &&
                whereCond->type == ExprNode::UNARY &&
                whereCond->left && whereCond->right &&
                whereCond->left->type == ExprNode::LEAF &&
                whereCond->right->type == ExprNode::LEAF &&
                whereCond->left->colName == "row") {
                const int row = std::stoi(whereCond->right->value);
                success = RecordManager::getInstance().deleteRecord(tname.text, row);
            }
            else if (TransactionManager::getInstance().hasActiveTransaction()) {
                success = RecordManager::getInstance().deleteRecordsTx(tname.text, whereCond.get());
            }
            else {
                success = RecordManager::getInstance().deleteRecords(tname.text, whereCond.get());
            }
        }
        else if (cmd == "EXIT" || cmd == "QUIT") {
            std::cout << "bye\n";
            exit(0);
        }
        else if (cmd == "HELP") {
            showHelp();
        }
        else {
            std::cout << "unknown command\n";
            return false;
        }

        if (success && shouldAppendLog(s) && !shouldSkipLogForActiveTransaction(s) && !g_current_db.empty()) {
            BackupManager::getInstance().appendLog(g_current_db, s);
        }
        return success;
    }
    catch (const std::exception& e) {
        std::cout << "execute error: " << e.what() << std::endl;
        return false;
    }
}
