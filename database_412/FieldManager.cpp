#include "FieldManager.h"
#include "TableManager.h"
#include "FileManager.h"

FieldManager::FieldManager() {
    fileManager = &FileManager::getInstance();  // 在构造函数体内初始化
}

FieldManager& FieldManager::getInstance() {
    static FieldManager instance;
    return instance;
}

std::vector<FieldInfo> FieldManager::getFields(const std::string& tname) {
    return fileManager->readAllStruct<FieldInfo>(joinPath(TableManager::getInstance().getTableDir(), tname + ".fld"));
}

void FieldManager::saveFields(const std::string& tname, const std::vector<FieldInfo>& flds) {
    fileManager->writeAllStruct(joinPath(TableManager::getInstance().getTableDir(), tname + ".fld"), flds);
}

bool FieldManager::addField(const std::string& tname, const std::string& fname, const std::string& type_str) {
    if (!TableManager::getInstance().isTableExists(tname)) {
        std::cout << "Err: 表不存在\n";
        return false;
    }

    auto flds = getFields(tname);
    for (const auto& f : flds) {
        if (std::string(f.name) == fname) {
            std::cout << "Err: 字段已存在\n";
            return false;
        }
    }

    FieldInfo new_f;
    safeStrncpy(new_f.name, fname.c_str(), MAX_NAME_LEN);
    new_f.order = static_cast<int32_t>(flds.size());

    std::string t_upper = toUpper(type_str);
    if (t_upper.find("VARCHAR") != std::string::npos) {
        new_f.type = DataType::VARCHAR;
        size_t start = t_upper.find('('), end = t_upper.find(')');
        if (start != std::string::npos && end != std::string::npos) {
            new_f.param = std::stoi(t_upper.substr(start + 1, end - start - 1));
        }
        else {
            new_f.param = 255;
        }
    }
    else if (t_upper == "INT") {
        new_f.type = DataType::INT;
        new_f.param = 0;
    }
    else if (t_upper == "DOUBLE") {
        new_f.type = DataType::DOUBLE;
        new_f.param = 0;
    }
    else {
        std::cout << "Err: 类型不支持 (INT, DOUBLE, VARCHAR(n))\n";
        return false;
    }

    flds.push_back(new_f);
    saveFields(tname, flds);

    // 更新表信息
    auto tableOpt = TableManager::getInstance().getTable(tname);
    if (tableOpt.has_value()) {
        TableInfo t = tableOpt.value();
        t.field_count = static_cast<int32_t>(flds.size());
        t.mtime.init();
        TableManager::getInstance().updateTable(tname, t);
    }

    std::cout << "OK: 字段 " << fname << " 添加成功\n";
    return true;
}

bool FieldManager::dropField(const std::string& tname, const std::string& fname) {
    auto flds = getFields(tname);
    int idx = -1;
    for (size_t i = 0; i < flds.size(); i++) {
        if (std::string(flds[i].name) == fname) {
            idx = static_cast<int>(i);
            break;
        }
    }
    if (idx == -1) {
        std::cout << "Err: 字段不存在\n";
        return false;
    }

    flds.erase(flds.begin() + idx);
    for (size_t i = idx; i < flds.size(); i++) {
        flds[i].order--;
    }
    saveFields(tname, flds);

    // 清空记录
    std::ofstream ofs(joinPath(TableManager::getInstance().getTableDir(), tname + ".rec"), std::ios::trunc);
    ofs.close();

    std::cout << "OK: 字段 " << fname << " 删除成功（记录已清空）\n";
    return true;
}

bool FieldManager::modifyField(const std::string& tname, const std::string& old_name, const std::string& new_name) {
    auto flds = getFields(tname);
    bool found = false;
    for (auto& f : flds) {
        if (std::string(f.name) == old_name) {
            safeStrncpy(f.name, new_name.c_str(), MAX_NAME_LEN);
            found = true;
            break;
        }
    }
    if (!found) {
        std::cout << "Err: 字段不存在\n";
        return false;
    }
    saveFields(tname, flds);
    std::cout << "OK: 字段名已修改\n";
    return true;
}