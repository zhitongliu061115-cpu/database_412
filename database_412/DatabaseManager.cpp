#include "DatabaseManager.h"
#include "FileManager.h"

DatabaseManager::DatabaseManager() {
    fileManager = &FileManager::getInstance();  // 在构造函数体内初始化
}

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

void DatabaseManager::saveDBs(const std::vector<DBInfo>& dbs) {
    fileManager->writeAllStruct(joinPath(g_root, SYS_DB_FILE), dbs);
}

bool DatabaseManager::createDB(const std::string& name) {
    if (name.empty() || name.length() > MAX_NAME_LEN) {
        std::cout << "Err: 名字无效\n";
        return false;
    }
    if (isDBExists(name)) {
        std::cout << "Err: 数据库已存在\n";
        return false;
    }

    DBInfo new_db;
    safeStrncpy(new_db.name, name.c_str(), MAX_NAME_LEN);
    std::string db_path = joinPath(joinPath(g_root, "data"), name);
    safeStrncpy(new_db.path, db_path.c_str(), MAX_PATH_LEN);

    MKDIR(joinPath(g_root, "data").c_str());
    MKDIR(db_path.c_str());

    auto dbs = getAllDBs();
    dbs.push_back(new_db);
    saveDBs(dbs);
    std::cout << "OK: 数据库 " << name << " 创建成功\n";
    return true;
}

bool DatabaseManager::dropDB(const std::string& name) {
    auto dbs = getAllDBs();
    auto it = std::remove_if(dbs.begin(), dbs.end(), [&](const DBInfo& db) {
        return std::string(db.name) == name;
        });
    if (it == dbs.end()) {
        std::cout << "Err: 数据库不存在\n";
        return false;
    }

    dbs.erase(it, dbs.end());
    saveDBs(dbs);
    if (g_current_db == name) g_current_db.clear();
    std::cout << "OK: 数据库 " << name << " 删除成功\n";
    return true;
}

bool DatabaseManager::useDB(const std::string& name) {
    if (isDBExists(name)) {
        g_current_db = name;
        std::cout << "OK: 切换到 " << name << "\n";
        return true;
    }
    std::cout << "Err: 数据库不存在\n";
    return false;
}

bool DatabaseManager::isDBExists(const std::string& name) {
    return getDB(name).has_value();
}

Optional<DBInfo> DatabaseManager::getDB(const std::string& name) {
    auto dbs = getAllDBs();
    for (const auto& db : dbs) {
        if (std::string(db.name) == name) {
            return Optional<DBInfo>(db);
        }
    }
    return Optional<DBInfo>();
}

std::vector<DBInfo> DatabaseManager::getAllDBs() {
    return fileManager->readAllStruct<DBInfo>(joinPath(g_root, SYS_DB_FILE));
}

std::string DatabaseManager::getDBPath(const std::string& name) {
    auto dbOpt = getDB(name);
    if (dbOpt.has_value()) {
        return dbOpt.value().path;
    }
    return "";
}