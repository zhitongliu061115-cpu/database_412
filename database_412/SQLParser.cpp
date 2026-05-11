#include "SQLParser.h"
#include "DatabaseManager.h"
#include "TableManager.h"
#include "FieldManager.h"
#include "RecordManager.h"
#include <set>
#include <cctype>
#include <stdexcept>
#include <memory>

// ==================== 词法分析 ====================
enum TokenType {
    TOK_KEYWORD, TOK_IDENT, TOK_STRING, TOK_NUMBER,
    TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_EQUAL,
    TOK_STAR, TOK_SEMICOLON, TOK_LT, TOK_GT, TOK_LE, TOK_GE, TOK_NE, TOK_END
};

struct Token { TokenType type; std::string text; };

class Tokenizer {
    std::string sql; size_t pos;
public:
    Tokenizer(const std::string& s) : sql(s), pos(0) { trim(sql); }
    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (pos < sql.size()) {
            skipSpaces();
            if (pos >= sql.size()) break;
            char ch = sql[pos];
            if (ch == '(') { tokens.push_back({ TOK_LPAREN,"(" }); pos++; }
            else if (ch == ')') { tokens.push_back({ TOK_RPAREN,")" }); pos++; }
            else if (ch == ',') { tokens.push_back({ TOK_COMMA,"," }); pos++; }
            else if (ch == '=') { tokens.push_back({ TOK_EQUAL,"=" }); pos++; }
            else if (ch == '*') { tokens.push_back({ TOK_STAR,"*" }); pos++; }
            else if (ch == ';') { tokens.push_back({ TOK_SEMICOLON,";" }); pos++; }
            else if (ch == '<') {
                if (pos + 1 < sql.size() && sql[pos + 1] == '=') { tokens.push_back({ TOK_LE,"<=" }); pos += 2; }
                else if (pos + 1 < sql.size() && sql[pos + 1] == '>') { tokens.push_back({ TOK_NE,"<>" }); pos += 2; }
                else { tokens.push_back({ TOK_LT,"<" }); pos++; }
            }
            else if (ch == '>') {
                if (pos + 1 < sql.size() && sql[pos + 1] == '=') { tokens.push_back({ TOK_GE,">=" }); pos += 2; }
                else { tokens.push_back({ TOK_GT,">" }); pos++; }
            }
            else if (ch == '!') {
                if (pos + 1 < sql.size() && sql[pos + 1] == '=') { tokens.push_back({ TOK_NE,"!=" }); pos += 2; }
                else { tokens.push_back({ TOK_IDENT,"!" }); pos++; }
            }
            else if (ch == '\'' || ch == '"') { tokens.push_back({ TOK_STRING, readQuoted(ch) }); }
            else if (isdigit(ch) || (ch == '-' && pos + 1 < sql.size() && isdigit(sql[pos + 1]))) {
                tokens.push_back({ TOK_NUMBER, readNumber() });
            }
            else {
                std::string word = readIdentifier();
                std::string upper = toUpper(word);
                if (isKeyword(upper)) tokens.push_back({ TOK_KEYWORD, upper });
                else tokens.push_back({ TOK_IDENT, word });
            }
        }
        tokens.push_back({ TOK_END,"" });
        return tokens;
    }
private:
    void skipSpaces() { while (pos < sql.size() && isspace(sql[pos])) pos++; }
    std::string readQuoted(char quote) {
        size_t start = ++pos;
        while (pos < sql.size() && sql[pos] != quote) pos++;
        std::string val = sql.substr(start, pos - start);
        if (pos < sql.size()) pos++; return val;
    }
    std::string readNumber() {
        size_t start = pos;
        if (sql[pos] == '-') pos++;
        while (pos < sql.size() && (isdigit(sql[pos]) || sql[pos] == '.')) pos++;
        return sql.substr(start, pos - start);
    }
    std::string readIdentifier() {
        size_t start = pos;
        while (pos < sql.size() && !isspace(sql[pos]) &&
            sql[pos] != '(' && sql[pos] != ')' && sql[pos] != ',' &&
            sql[pos] != '=' && sql[pos] != ';' && sql[pos] != '*' &&
            sql[pos] != '\'' && sql[pos] != '"' && sql[pos] != '<' && sql[pos] != '>' && sql[pos] != '!')
            pos++;
        return sql.substr(start, pos - start);
    }
    bool isKeyword(const std::string& s) {
        static const std::set<std::string> kw = {
            "CREATE","DATABASE","DROP","USE","TABLE","ALTER","ADD","COLUMN","MODIFY",
            "INSERT","INTO","VALUES","SELECT","FROM","WHERE","UPDATE","SET","DELETE",
            "AND","OR","NOT","ORDER","BY","GROUP","ASC","DESC","COUNT","SUM","AVG",
            "MIN","MAX","HAVING","EXIT","QUIT","HELP","NULL","PRIMARY","KEY"
        };
        return kw.count(s) > 0;
    }
};

// ==================== 语法分析辅助 ====================
class Parser {
    std::vector<Token> tokens; size_t idx;
public:
    Parser(const std::vector<Token>& t) : tokens(t), idx(0) {}
    Token peek() { return idx < tokens.size() ? tokens[idx] : Token{ TOK_END,"" }; }
    Token consume(TokenType exp) {
        Token t = peek();
        if (t.type != exp) throw std::runtime_error("语法错误"); idx++; return t;
    }
    bool match(TokenType t) { if (peek().type == t) { idx++; return true; } return false; }
    bool matchKeyword(const std::string& k) {
        if (peek().type == TOK_KEYWORD && peek().text == k) { idx++; return true; } return false;
    }
    Token expectIdent() {
        Token t = peek();
        if (t.type == TOK_IDENT || t.type == TOK_KEYWORD) { idx++; return t; }
        throw std::runtime_error("期望标识符");
    }
    Token expectStringOrIdent() {
        Token t = peek();
        if (t.type == TOK_STRING || t.type == TOK_NUMBER || t.type == TOK_IDENT) { idx++; return t; }
        throw std::runtime_error("期望值");
    }
};

// 前向声明
static std::unique_ptr<ExprNode> parseExpr(Parser& p, const std::vector<FieldInfo>& fields);
static std::unique_ptr<ExprNode> parseAndExpr(Parser& p, const std::vector<FieldInfo>& fields);
static std::unique_ptr<ExprNode> parseComparison(Parser& p, const std::vector<FieldInfo>& fields);
static std::unique_ptr<ExprNode> parsePrimary(Parser& p, const std::vector<FieldInfo>& fields);

// 获取字段类型辅助
static DataType getFieldType(const std::string& colName, const std::vector<FieldInfo>& fields) {
    for (const auto& f : fields) if (f.name == colName) return f.type;
    return DataType::VARCHAR;
}

// -------- 表达式解析 --------
static std::unique_ptr<ExprNode> parsePrimary(Parser& p, const std::vector<FieldInfo>& fields) {
    if (p.match(TOK_LPAREN)) {
        auto node = parseExpr(p, fields);
        p.consume(TOK_RPAREN);
        return node;
    }
    Token t = p.peek();
    if (t.type == TOK_IDENT || t.type == TOK_KEYWORD) {
        // 可能是列名或布尔字面量（如 NOT NULL 暂时不处理）
        // 判断是否是列名：检查是否在fields中
        std::string name = t.text;
        p.consume(t.type);
        // 如果后跟操作符，则是列引用
        if (p.peek().type == TOK_EQUAL || p.peek().type == TOK_LT || p.peek().type == TOK_GT ||
            p.peek().type == TOK_LE || p.peek().type == TOK_GE || p.peek().type == TOK_NE) {
            return ExprNode::makeLeafCol(name, getFieldType(name, fields));
        }
        else {
            // 可能是布尔常量？暂不支持，回退为列引用
            return ExprNode::makeLeafCol(name, getFieldType(name, fields));
        }
    }
    else if (t.type == TOK_STRING || t.type == TOK_NUMBER) {
        std::string val = t.text;
        DataType dt = DataType::VARCHAR;
        if (t.type == TOK_NUMBER) dt = (val.find('.') != std::string::npos) ? DataType::DOUBLE : DataType::INT;
        p.consume(t.type);
        return ExprNode::makeLeafConst(val, dt);
    }
    throw std::runtime_error("表达式解析错误");
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
    else {
        // 没有比较运算符，可能是个单独的列或常量（用于布尔？）暂不支持，返回左节点作为真值？我们直接要求where子句必须是比较
        // 这里简单处理：如果左节点是列，返回一个等于自身的比较（永远真），否则报错
        // 但为了支持`WHERE col`这种不标准的，我们拒绝。
        throw std::runtime_error("WHERE 需要比较表达式 (例: col = val)");
    }
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

// ---------- DDL / DML 解析 ----------

static void parseCreate(Parser& p) {
    if (p.matchKeyword("DATABASE")) {
        Token name = p.expectIdent();
        DatabaseManager::getInstance().createDB(name.text);
    }
    else if (p.matchKeyword("TABLE")) {
        Token name = p.expectIdent();
        TableManager::getInstance().createTable(name.text);
    }
    else std::cout << "Err: 未知的 CREATE 类型\n";
}

static void parseDrop(Parser& p) {
    if (p.matchKeyword("DATABASE")) { Token name = p.expectIdent(); DatabaseManager::getInstance().dropDB(name.text); }
    else if (p.matchKeyword("TABLE")) { Token name = p.expectIdent(); TableManager::getInstance().dropTable(name.text); }
    else std::cout << "Err: 未知的 DROP 类型\n";
}

static void parseAlter(Parser& p) {
    if (!p.matchKeyword("TABLE")) throw std::runtime_error("期望 TABLE");
    Token tname = p.expectIdent();
    std::string action = p.expectIdent().text;
    if (action == "ADD") {
        p.matchKeyword("COLUMN");
        Token col = p.expectIdent();
        std::string type = p.expectIdent().text;
        if (p.match(TOK_LPAREN)) { std::string param = p.expectStringOrIdent().text; p.consume(TOK_RPAREN); type += "(" + param + ")"; }
        bool notNull = false, pk = false;
        while (true) {
            if (p.matchKeyword("NOT") && p.matchKeyword("NULL")) notNull = true;
            else if (p.matchKeyword("PRIMARY") && p.matchKeyword("KEY")) pk = true;
            else break;
        }
        FieldManager::getInstance().addField(tname.text, col.text, type, notNull, pk);
    }
    else if (action == "DROP") {
        p.matchKeyword("COLUMN");
        Token col = p.expectIdent();
        FieldManager::getInstance().dropField(tname.text, col.text);
    }
    else if (action == "MODIFY") {
        p.matchKeyword("COLUMN");
        Token oldName = p.expectIdent();
        Token newName = p.expectIdent();
        FieldManager::getInstance().modifyField(tname.text, oldName.text, newName.text);
    }
    else std::cout << "Err: 不支持 ALTER 操作\n";
}

static void parseInsert(Parser& p) {
    if (!p.matchKeyword("INTO")) throw std::runtime_error("期望 INTO");
    Token tname = p.expectIdent();
    std::vector<std::string> cols, vals;
    if (p.match(TOK_LPAREN)) {
        while (true) { Token col = p.expectIdent(); cols.push_back(col.text); if (!p.match(TOK_COMMA)) break; }
        p.consume(TOK_RPAREN);
    }
    if (!p.matchKeyword("VALUES")) throw std::runtime_error("期望 VALUES");
    p.match(TOK_LPAREN);
    while (true) { Token v = p.expectStringOrIdent(); vals.push_back(v.text); if (!p.match(TOK_COMMA)) break; }
    p.match(TOK_RPAREN);
    if (cols.empty()) RecordManager::getInstance().insertRecord(tname.text, vals);
    else RecordManager::getInstance().insertRecord(tname.text, cols, vals);
}

static void parseSelect(Parser& p) {
    std::vector<std::string> outCols;
    if (!p.match(TOK_STAR)) {
        // 列列表
        while (true) { Token col = p.expectIdent(); outCols.push_back(col.text); if (!p.match(TOK_COMMA)) break; }
    }
    if (!p.matchKeyword("FROM")) throw std::runtime_error("期望 FROM");
    Token tname = p.expectIdent();

    // WHERE
    std::unique_ptr<ExprNode> whereCond;
    if (p.matchKeyword("WHERE")) {
        auto fields = FieldManager::getInstance().getFields(tname.text);
        whereCond = parseExpr(p, fields);
    }

    // GROUP BY
    std::string groupByCol;
    AggFuncType aggFunc = AggFuncType::NONE;
    std::string aggCol;
    if (p.matchKeyword("GROUP") && p.matchKeyword("BY")) {
        groupByCol = p.expectIdent().text;
    }

    // 聚合函数可能出现在 SELECT 列表中，这里简化处理：只支持单个聚合函数且放在列列表里
    // 我们检查outCols中是否有聚合函数调用，如 COUNT(col)
    for (auto& col : outCols) {
        std::string upper = toUpper(col);
        if (upper.find("COUNT(") == 0) { aggFunc = AggFuncType::COUNT; aggCol = col.substr(6, col.size() - 7); }
        else if (upper.find("SUM(") == 0) { aggFunc = AggFuncType::SUM; aggCol = col.substr(4, col.size() - 5); }
        else if (upper.find("AVG(") == 0) { aggFunc = AggFuncType::AVG; aggCol = col.substr(4, col.size() - 5); }
        else if (upper.find("MIN(") == 0) { aggFunc = AggFuncType::MIN; aggCol = col.substr(4, col.size() - 5); }
        else if (upper.find("MAX(") == 0) { aggFunc = AggFuncType::MAX; aggCol = col.substr(4, col.size() - 5); }
    }
    if (aggFunc != AggFuncType::NONE) {
        outCols.clear(); // 聚合查询不输出原始列
    }

    // ORDER BY
    std::string orderByCol; bool orderAsc = true;
    if (p.matchKeyword("ORDER") && p.matchKeyword("BY")) {
        orderByCol = p.expectIdent().text;
        if (p.matchKeyword("DESC")) orderAsc = false;
        else p.matchKeyword("ASC");
    }

    RecordManager::getInstance().selectRecords(tname.text, outCols, whereCond.get(),
        orderByCol, orderAsc, groupByCol, aggFunc, aggCol);
}

static void parseUpdate(Parser& p) {
    Token tname = p.expectIdent();
    if (!p.matchKeyword("SET")) throw std::runtime_error("期望 SET");
    Token setCol = p.expectIdent();
    p.consume(TOK_EQUAL);
    Token setVal = p.expectStringOrIdent();
    std::unique_ptr<ExprNode> whereCond;
    if (p.matchKeyword("WHERE")) {
        auto fields = FieldManager::getInstance().getFields(tname.text);
        whereCond = parseExpr(p, fields);
    }
    else {
        std::cout << "Err: UPDATE 必须含 WHERE\n"; return;
    }
    // 检查是否是 row = n 格式
    if (auto comp = dynamic_cast<ExprNode*>(whereCond.get())) {
        if (comp->type == ExprNode::UNARY && comp->left->colName == "row") {
            int rn = std::stoi(comp->right->value);
            RecordManager::getInstance().updateRecord(tname.text, setCol.text, setVal.text, rn);
            return;
        }
    }
    RecordManager::getInstance().updateRecords(tname.text, setCol.text, setVal.text, whereCond.get());
}

static void parseDelete(Parser& p) {
    if (!p.matchKeyword("FROM")) throw std::runtime_error("期望 FROM");
    Token tname = p.expectIdent();
    std::unique_ptr<ExprNode> whereCond;
    if (p.matchKeyword("WHERE")) {
        auto fields = FieldManager::getInstance().getFields(tname.text);
        whereCond = parseExpr(p, fields);
    }
    else {
        RecordManager::getInstance().deleteRecord(tname.text, -1);
        return;
    }
    if (auto comp = dynamic_cast<ExprNode*>(whereCond.get())) {
        if (comp->type == ExprNode::UNARY && comp->left->colName == "row") {
            int rn = std::stoi(comp->right->value);
            RecordManager::getInstance().deleteRecord(tname.text, rn);
            return;
        }
    }
    RecordManager::getInstance().deleteRecords(tname.text, whereCond.get());
}

// ---------- SQLParser 接口 ----------
SQLParser& SQLParser::getInstance() { static SQLParser ins; return ins; }

void SQLParser::showHelp() {
    std::cout << "\n支持的命令:\n"
        << "  CREATE DATABASE <name>\n  DROP DATABASE <name>\n  USE <db>\n"
        << "  CREATE TABLE <name>\n  DROP TABLE <name>\n"
        << "  ALTER TABLE <t> ADD [COLUMN] <col> <type> [NOT NULL]\n"
        << "  ALTER TABLE <t> DROP [COLUMN] <col>\n"
        << "  ALTER TABLE <t> MODIFY [COLUMN] <old> <new>\n"
        << "  INSERT INTO <t> [(cols)] VALUES (vals)\n"
        << "  SELECT [cols|*] FROM <t> [WHERE expr] [GROUP BY col] [ORDER BY col [ASC|DESC]]\n"
        << "  UPDATE <t> SET col=val WHERE expr\n"
        << "  DELETE FROM <t> [WHERE expr]\n"
        << "  expr: comparison (AND/OR comparison)*\n"
        << "  comparison: col <op> val | ( expr )\n"
        << "  op: =, <, >, <=, >=, !=, <>\n\n";
}

void SQLParser::execute(const std::string& sql) {
    std::string s = sql; trim(s);
    if (s.empty()) return;
    try {
        Tokenizer tokenizer(s);
        Parser p(tokenizer.tokenize());
        Token first = p.peek();
        if (first.type == TOK_END) return;
        std::string cmd = first.text;
        if (cmd == "CREATE") { p.consume(TOK_KEYWORD); parseCreate(p); }
        else if (cmd == "DROP") { p.consume(TOK_KEYWORD); parseDrop(p); }
        else if (cmd == "USE") { p.consume(TOK_KEYWORD); DatabaseManager::getInstance().useDB(p.expectIdent().text); }
        else if (cmd == "ALTER") { p.consume(TOK_KEYWORD); parseAlter(p); }
        else if (cmd == "INSERT") { p.consume(TOK_KEYWORD); parseInsert(p); }
        else if (cmd == "SELECT") { p.consume(TOK_KEYWORD); parseSelect(p); }
        else if (cmd == "UPDATE") { p.consume(TOK_KEYWORD); parseUpdate(p); }
        else if (cmd == "DELETE") { p.consume(TOK_KEYWORD); parseDelete(p); }
        else if (cmd == "EXIT" || cmd == "QUIT") { std::cout << "再见！\n"; exit(0); }
        else if (cmd == "HELP") showHelp();
        else std::cout << "未知命令\n";
    }
    catch (const std::exception& e) { std::cout << "执行错误: " << e.what() << std::endl; }
}
