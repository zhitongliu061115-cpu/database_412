#ifndef COMMON_H
#define COMMON_H

#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <sys/stat.h>
#include <functional>
#include <memory>
#include <set>
#include <cmath>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

constexpr uint32_t MAX_NAME_LEN = 128;
constexpr uint32_t MAX_PATH_LEN = 256;
constexpr char SYS_DB_FILE[] = "system.db";

enum class DataType : uint32_t { INT = 0, VARCHAR = 1, DOUBLE = 2 };
enum class CompOp { EQ, NE, LT, LE, GT, GE };      // 比较运算符
enum class LogicOp { AND, OR };

// 表达式树节点
struct ExprNode {
    enum Type { LEAF, UNARY, BINARY } type;
    // 叶节点：列名/常量值
    std::string colName;
    std::string value;          // 常量值（字符串形式）
    DataType leafType;          // 期望的数据类型（用于比较时转换）
    // 一元/二元
    CompOp comp;                // 比较运算符 (一元)
    LogicOp logic;              // 逻辑联结 (二元)
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;

    // 叶节点：列引用
    static std::unique_ptr<ExprNode> makeLeafCol(const std::string& col, DataType dt) {
        auto node = std::unique_ptr<ExprNode>(new ExprNode);
        node->type = LEAF;
        node->colName = col;
        node->leafType = dt;
        return node;
    }
    // 叶节点：常量
    static std::unique_ptr<ExprNode> makeLeafConst(const std::string& val, DataType dt) {
        auto node = std::unique_ptr<ExprNode>(new ExprNode);
        node->type = LEAF;
        node->value = val;
        node->leafType = dt;
        return node;
    }
    // 比较节点
    static std::unique_ptr<ExprNode> makeComp(CompOp op, std::unique_ptr<ExprNode> l, std::unique_ptr<ExprNode> r) {
        auto node = std::unique_ptr<ExprNode>(new ExprNode);
        node->type = UNARY;
        node->comp = op;
        node->left = std::move(l);
        node->right = std::move(r);
        return node;
    }
    // 逻辑节点
    static std::unique_ptr<ExprNode> makeLogic(LogicOp op, std::unique_ptr<ExprNode> l, std::unique_ptr<ExprNode> r) {
        auto node = std::unique_ptr<ExprNode>(new ExprNode);
        node->type = BINARY;
        node->logic = op;
        node->left = std::move(l);
        node->right = std::move(r);
        return node;
    }
};

// 聚合类型
enum class AggFuncType { NONE, COUNT, SUM, AVG, MIN, MAX };

// 极简 Optional
template <typename T>
class Optional {
public:
    Optional() : has_value_(false) { memset(&value_, 0, sizeof(T)); }
    Optional(const T& val) : has_value_(true), value_(val) {}
    bool has_value() const { return has_value_; }
    explicit operator bool() const { return has_value_; }
    T& value() { return value_; }
    const T& value() const { return value_; }
private:
    bool has_value_;
    T value_;
};

#pragma pack(push, 1)
struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t pad[13];
    void init() {
        memset(this, 0, sizeof(DateTime));
        time_t now = time(nullptr);
        struct tm l;
#ifdef _WIN32
        localtime_s(&l, &now);
#else
        localtime_r(&now, &l);
#endif
        year = l.tm_year + 1900;
        month = l.tm_mon + 1;
        day = l.tm_mday;
    }
};

struct DBInfo {
    char name[MAX_NAME_LEN];
    char path[MAX_PATH_LEN];
    DateTime crtime;
    DBInfo() { memset(this, 0, sizeof(DBInfo)); crtime.init(); }
};

struct TableInfo {
    char name[MAX_NAME_LEN];
    int32_t field_count;
    int32_t record_count;
    DateTime mtime;
    TableInfo() {
        memset(this, 0, sizeof(TableInfo));
        field_count = 0; record_count = 0; mtime.init();
    }
};

#define FIELD_FLAG_NOT_NULL 1   // 非空约束

struct FieldInfo {
    char name[MAX_NAME_LEN];    // 128
    DataType type;              // 4
    int32_t param;              // 4 (varchar长度)
    int32_t order;              // 4
    uint8_t flags;              // 1  (约束位掩码)
    FieldInfo() {
        memset(this, 0, sizeof(FieldInfo));
        order = 0; param = 255; type = DataType::INT; flags = 0;
    }
};
#pragma pack(pop)

// 前向声明
class TransactionManager;
enum class LockType;

// 全局变量声明
extern thread_local std::string g_current_db;
extern std::string g_root;

// 工具函数
void trim(std::string& s);
std::string toUpper(const std::string& s);
std::vector<std::string> split(const std::string& s, char delim);
std::string joinPath(const std::string& base, const std::string& name);
inline void safeStrncpy(char* dest, const char* src, size_t size) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

// 辅助函数：去除字符串首尾空格和引号
std::string unquote(const std::string& s);

// 值比较辅助 (根据数据类型)
bool compareValues(const std::string& a, const std::string& b, DataType type, CompOp op);

// 类型校验
bool validateValue(const std::string& val, DataType type, int param);

#endif
