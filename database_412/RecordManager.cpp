#include "RecordManager.h"
#include "FieldManager.h"
#include "TableManager.h"
#include "FileManager.h"
#include <algorithm>   // std::sort, std::greater
#include <functional>  // std::greater

RecordManager::RecordManager() {
    fileManager = &FileManager::getInstance();
}

RecordManager& RecordManager::getInstance() {
    static RecordManager instance;
    return instance;
}

// ==================== 原有的读/写记录函数 ====================
std::vector<std::string> RecordManager::readRecs(const std::string& tname) {
    std::vector<std::string> res;
    std::ifstream ifs(joinPath(TableManager::getInstance().getTableDir(), tname + ".rec"));
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty()) res.push_back(line);
    }
    return res;
}

void RecordManager::writeRecs(const std::string& tname, const std::vector<std::string>& recs) {
    std::ofstream ofs(joinPath(TableManager::getInstance().getTableDir(), tname + ".rec"), std::ios::trunc);
    for (const auto& r : recs) {
        ofs << r << "\n";
    }
}

// ==================== 插入记录 ====================
bool RecordManager::insertRecord(const std::string& tname, const std::vector<std::string>& values) {
    auto flds = FieldManager::getInstance().getFields(tname);
    if (values.size() != flds.size()) {
        std::cout << "Err: 值数量不匹配 (需要 " << flds.size() << ")\n";
        return false;
    }

    std::string line;
    for (size_t i = 0; i < values.size(); i++) {
        std::string v = values[i];
        trim(v);
        if (!v.empty() && (v.front() == '\'' || v.front() == '"')) {
            v = v.substr(1, v.size() - 2);
        }
        line += v + (i == values.size() - 1 ? "" : "|");
    }

    auto recs = readRecs(tname);
    recs.push_back(line);
    writeRecs(tname, recs);

    // 更新表记录数
    auto tableOpt = TableManager::getInstance().getTable(tname);
    if (tableOpt.has_value()) {
        TableInfo t = tableOpt.value();
        t.record_count = static_cast<int32_t>(recs.size());
        t.mtime.init();
        TableManager::getInstance().updateTable(tname, t);
    }

    std::cout << "OK: 插入成功\n";
    return true;
}

// ==================== 原有全表查询 ====================
bool RecordManager::selectRecords(const std::string& tname) {
    auto flds = FieldManager::getInstance().getFields(tname);
    auto recs = readRecs(tname);

    std::cout << "\n--- " << tname << " ---" << std::endl;
    for (size_t i = 0; i < flds.size(); i++) {
        std::cout << flds[i].name << (i == flds.size() - 1 ? "\n" : "\t");
    }
    std::cout << std::string(50, '-') << std::endl;

    for (const auto& r : recs) {
        auto cols = split(r, '|');
        for (size_t i = 0; i < cols.size(); i++) {
            std::cout << cols[i] << (i == cols.size() - 1 ? "\n" : "\t");
        }
    }
    std::cout << "共 " << recs.size() << " 条记录\n\n";
    return true;
}

// ==================== 原有按行号更新 ====================
bool RecordManager::updateRecord(const std::string& tname, const std::string& col, const std::string& val, int row) {
    auto flds = FieldManager::getInstance().getFields(tname);
    int col_idx = -1;
    for (size_t i = 0; i < flds.size(); i++) {
        if (std::string(flds[i].name) == col) {
            col_idx = static_cast<int>(i);
            break;
        }
    }
    if (col_idx == -1) {
        std::cout << "Err: 字段不存在\n";
        return false;
    }

    auto recs = readRecs(tname);
    if (row < 0 || row >= static_cast<int>(recs.size())) {
        std::cout << "Err: 行号无效\n";
        return false;
    }

    auto cols = split(recs[row], '|');
    std::string newVal = val;
    trim(newVal);
    if (!newVal.empty() && (newVal.front() == '\'' || newVal.front() == '"')) {
        newVal = newVal.substr(1, newVal.size() - 2);
    }
    cols[col_idx] = newVal;

    std::string newline;
    for (size_t i = 0; i < cols.size(); i++) {
        newline += cols[i] + (i == cols.size() - 1 ? "" : "|");
    }
    recs[row] = newline;

    writeRecs(tname, recs);
    std::cout << "OK: 更新成功\n";
    return true;
}

// ==================== 原有按行号删除 ====================
bool RecordManager::deleteRecord(const std::string& tname, int row) {
    auto recs = readRecs(tname);
    if (row == -1) {
        recs.clear();
    }
    else if (row >= 0 && row < static_cast<int>(recs.size())) {
        recs.erase(recs.begin() + row);
    }
    else {
        std::cout << "Err: 行号无效\n";
        return false;
    }
    writeRecs(tname, recs);

    auto tableOpt = TableManager::getInstance().getTable(tname);
    if (tableOpt.has_value()) {
        TableInfo t = tableOpt.value();
        t.record_count = static_cast<int32_t>(recs.size());
        t.mtime.init();
        TableManager::getInstance().updateTable(tname, t);
    }

    std::cout << "OK: 删除成功\n";
    return true;
}

// ==================== 新增：辅助函数 ====================
std::vector<int> RecordManager::findRowsByCondition(const std::string& tname,
    const std::string& colName, const std::string& val) {
    auto flds = FieldManager::getInstance().getFields(tname);
    int colIdx = -1;
    for (size_t i = 0; i < flds.size(); i++) {
        if (std::string(flds[i].name) == colName) {
            colIdx = static_cast<int>(i);
            break;
        }
    }
    if (colIdx == -1) {
        std::cout << "Err: 条件字段 " << colName << " 不存在\n";
        return {};
    }

    auto recs = RecordManager::getInstance().readRecs(tname);
    std::vector<int> rows;
    for (size_t i = 0; i < recs.size(); i++) {
        auto cols = split(recs[i], '|');
        if (colIdx < static_cast<int>(cols.size()) && cols[colIdx] == val) {
            rows.push_back(static_cast<int>(i));
        }
    }
    return rows;
}

// ==================== 带 WHERE 的查询 ====================
bool RecordManager::selectRecords(const std::string& tname,
    const std::string& whereCol, const std::string& whereVal) {
    auto flds = FieldManager::getInstance().getFields(tname);
    auto recs = readRecs(tname);
    auto rows = findRowsByCondition(tname, whereCol, whereVal);

    std::cout << "\n--- " << tname << " (WHERE " << whereCol << " = " << whereVal << ") ---" << std::endl;
    for (size_t i = 0; i < flds.size(); i++) {
        std::cout << flds[i].name << (i == flds.size() - 1 ? "\n" : "\t");
    }
    std::cout << std::string(50, '-') << std::endl;

    for (int r : rows) {
        auto cols = split(recs[r], '|');
        for (size_t i = 0; i < cols.size(); i++) {
            std::cout << cols[i] << (i == cols.size() - 1 ? "\n" : "\t");
        }
    }
    std::cout << "共 " << rows.size() << " 条记录\n\n";
    return true;
}

// ==================== 带 WHERE 的更新（更新所有匹配行） ====================
bool RecordManager::updateRecords(const std::string& tname,
    const std::string& setCol, const std::string& setVal,
    const std::string& whereCol, const std::string& whereVal) {
    auto flds = FieldManager::getInstance().getFields(tname);
    int setIdx = -1;
    for (size_t i = 0; i < flds.size(); i++) {
        if (std::string(flds[i].name) == setCol) {
            setIdx = static_cast<int>(i);
            break;
        }
    }
    if (setIdx == -1) {
        std::cout << "Err: 更新字段 " << setCol << " 不存在\n";
        return false;
    }

    auto rows = findRowsByCondition(tname, whereCol, whereVal);
    if (rows.empty()) {
        std::cout << "未找到匹配的记录\n";
        return true;
    }

    auto recs = readRecs(tname);
    for (int r : rows) {
        auto cols = split(recs[r], '|');
        cols[setIdx] = setVal;
        std::string newLine;
        for (size_t i = 0; i < cols.size(); i++) {
            newLine += cols[i] + (i == cols.size() - 1 ? "" : "|");
        }
        recs[r] = newLine;
    }

    writeRecs(tname, recs);

    auto tableOpt = TableManager::getInstance().getTable(tname);
    if (tableOpt.has_value()) {
        TableInfo t = tableOpt.value();
        t.record_count = static_cast<int32_t>(recs.size());
        t.mtime.init();
        TableManager::getInstance().updateTable(tname, t);
    }

    std::cout << "OK: 成功更新 " << rows.size() << " 条记录\n";
    return true;
}

// ==================== 带 WHERE 的删除 ====================
bool RecordManager::deleteRecords(const std::string& tname,
    const std::string& whereCol, const std::string& whereVal) {
    auto rows = findRowsByCondition(tname, whereCol, whereVal);
    if (rows.empty()) {
        std::cout << "未找到匹配的记录\n";
        return true;
    }

    auto recs = readRecs(tname);
    // 索引降序删除，避免越界
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) {
        recs.erase(recs.begin() + r);
    }

    writeRecs(tname, recs);

    auto tableOpt = TableManager::getInstance().getTable(tname);
    if (tableOpt.has_value()) {
        TableInfo t = tableOpt.value();
        t.record_count = static_cast<int32_t>(recs.size());
        t.mtime.init();
        TableManager::getInstance().updateTable(tname, t);
    }

    std::cout << "OK: 成功删除 " << rows.size() << " 条记录\n";
    return true;
}
