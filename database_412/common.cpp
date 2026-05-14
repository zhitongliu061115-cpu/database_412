#include "common.h"

thread_local std::string g_current_db;
std::string g_root = "./";

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

std::string unquote(const std::string& s) {
    std::string res = s;
    trim(res);
    if (res.size() >= 2 && (res.front() == '\'' || res.front() == '"') && res.front() == res.back()) {
        res = res.substr(1, res.size() - 2);
    }
    return res;
}

// 值比较
bool compareValues(const std::string& a, const std::string& b, DataType type, CompOp op) {
    if (type == DataType::INT) {
        int ia = std::stoi(a), ib = std::stoi(b);
        switch (op) {
        case CompOp::EQ: return ia == ib;
        case CompOp::NE: return ia != ib;
        case CompOp::LT: return ia < ib;
        case CompOp::LE: return ia <= ib;
        case CompOp::GT: return ia > ib;
        case CompOp::GE: return ia >= ib;
        }
    }
    else if (type == DataType::DOUBLE) {
        double da = std::stod(a), db = std::stod(b);
        switch (op) {
        case CompOp::EQ: return da == db;
        case CompOp::NE: return da != db;
        case CompOp::LT: return da < db;
        case CompOp::LE: return da <= db;
        case CompOp::GT: return da > db;
        case CompOp::GE: return da >= db;
        }
    }
    else { // VARCHAR
        switch (op) {
        case CompOp::EQ: return a == b;
        case CompOp::NE: return a != b;
        case CompOp::LT: return a < b;
        case CompOp::LE: return a <= b;
        case CompOp::GT: return a > b;
        case CompOp::GE: return a >= b;
        }
    }
    return false;
}

// 值校验
bool validateValue(const std::string& val, DataType type, int param) {
    if (type == DataType::INT) {
        try { std::stoi(val); }
        catch (...) { return false; }
        return true;
    }
    else if (type == DataType::DOUBLE) {
        try { std::stod(val); }
        catch (...) { return false; }
        return true;
    }
    else if (type == DataType::VARCHAR) {
        return val.length() <= (size_t)param;
    }
    return false;
}
