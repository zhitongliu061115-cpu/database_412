#include "RecordManager.h"
#include "FieldManager.h"
#include "TableManager.h"
#include "FileManager.h"

RecordManager::RecordManager() {
    fileManager = &FileManager::getInstance();  // 在构造函数体内初始化
}

RecordManager& RecordManager::getInstance() {
    static RecordManager instance;
    return instance;
}

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
        // 去除引号
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

    // 更新表记录数
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