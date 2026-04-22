#ifndef COMMON_H
#define COMMON_H

#define _CRT_SECURE_NO_WARNINGS  // 解决 strncpy 等函数的安全警告

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

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

// 常量
constexpr uint32_t MAX_NAME_LEN = 128;
constexpr uint32_t MAX_PATH_LEN = 256;
constexpr char SYS_DB_FILE[] = "system.db";

// 数据类型
enum class DataType : uint32_t { INT = 0, VARCHAR = 1, DOUBLE = 2 };

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

// 所有结构体定义
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
    DBInfo() {
        memset(this, 0, sizeof(DBInfo));
        crtime.init();
    }
};

struct TableInfo {
    char name[MAX_NAME_LEN];
    int32_t field_count;
    int32_t record_count;
    DateTime mtime;
    TableInfo() {
        memset(this, 0, sizeof(TableInfo));
        field_count = 0;
        record_count = 0;
        mtime.init();
    }
};

struct FieldInfo {
    char name[MAX_NAME_LEN];
    DataType type;
    int32_t param;
    int32_t order;
    FieldInfo() {
        memset(this, 0, sizeof(FieldInfo));
        order = 0;
        param = 255;
        type = DataType::INT;
    }
};
#pragma pack(pop)

// 全局变量声明
extern std::string g_current_db;
extern std::string g_root;

// 工具函数声明
void trim(std::string& s);
std::string toUpper(const std::string& s);
std::vector<std::string> split(const std::string& s, char delim);
std::string joinPath(const std::string& base, const std::string& name);

// 安全字符串复制函数
inline void safeStrncpy(char* dest, const char* src, size_t size) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

#endif // COMMON_H