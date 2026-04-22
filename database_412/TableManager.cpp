#include "TableManager.h"
#include "DatabaseManager.h"
#include "FileManager.h"

TableManager::TableManager() {
    fileManager = &FileManager::getInstance();  // 在构造函数体内初始化
}

TableManager& TableManager::getInstance() {
    static TableManager instance;
    return instance;
}

std::string TableManager::getTableMetaPath() {
    return joinPath(DatabaseManager::getInstance().getDBPath(g_current_db), g_current_db + ".meta");
}

std::string TableManager::getTableDir() {
    return DatabaseManager::getInstance().getDBPath(g_current_db);
}

void TableManager::saveTables(const std::vector<TableInfo>& tables) {
    fileManager->writeAllStruct(getTableMetaPath(), tables);
}

bool TableManager::createTable(const std::string& name) {
    if (g_current_db.empty()) {
        std::cout << "Err: 请先 USE 数据库\n";
        return false;
    }
    if (isTableExists(name)) {
        std::cout << "Err: 表已存在\n";
        return false;
    }

    TableInfo new_t;
    safeStrncpy(new_t.name, name.c_str(), MAX_NAME_LEN);

    auto tables = getAllTables();
    tables.push_back(new_t);
    saveTables(tables);

    // 创建空文件
    std::vector<FieldInfo> empty_flds;
    fileManager->writeAllStruct(joinPath(getTableDir(), name + ".fld"), empty_flds);
    std::ofstream ofs(joinPath(getTableDir(), name + ".rec"));
    ofs.close();

    std::cout << "OK: 表 " << name << " 创建成功\n";
    return true;
}

bool TableManager::dropTable(const std::string& name) {
    if (g_current_db.empty()) {
        std::cout << "Err: 请先 USE 数据库\n";
        return false;
    }
    auto tables = getAllTables();
    auto it = std::remove_if(tables.begin(), tables.end(), [&](const TableInfo& t) {
        return std::string(t.name) == name;
        });
    if (it == tables.end()) {
        std::cout << "Err: 表不存在\n";
        return false;
    }

    tables.erase(it, tables.end());
    saveTables(tables);

    std::remove(joinPath(getTableDir(), name + ".fld").c_str());
    std::remove(joinPath(getTableDir(), name + ".rec").c_str());

    std::cout << "OK: 表 " << name << " 删除成功\n";
    return true;
}

bool TableManager::isTableExists(const std::string& name) {
    return getTable(name).has_value();
}

Optional<TableInfo> TableManager::getTable(const std::string& name) {
    auto tables = getAllTables();
    for (const auto& t : tables) {
        if (std::string(t.name) == name) {
            return Optional<TableInfo>(t);
        }
    }
    return Optional<TableInfo>();
}

std::vector<TableInfo> TableManager::getAllTables() {
    return fileManager->readAllStruct<TableInfo>(getTableMetaPath());
}

void TableManager::updateTable(const std::string& name, const TableInfo& newInfo) {
    auto tables = getAllTables();
    for (auto& t : tables) {
        if (std::string(t.name) == name) {
            t = newInfo;
            break;
        }
    }
    saveTables(tables);
}