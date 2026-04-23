#include "common.h"

//全局变量定义
std::string g_current_db;
std::string g_root = "./";

//工具函数
void trim(std::string& s) {
    s.erase(0, s.find_first_not_of(" \t\n\r;"));
    s.erase(s.find_last_not_of(" \t\n\r;") + 1);
}

std::string toUpper(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), ::toupper);
    return res;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> res;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) res.push_back(item);
    }
    return res;
}

std::string joinPath(const std::string& base, const std::string& name) {
#ifdef _WIN32
    return base + "\\" + name;
#else
    return base + "/" + name;
#endif
}