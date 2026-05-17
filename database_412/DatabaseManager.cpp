#include "DatabaseManager.h"
#include "FileManager.h"
#include "SecurityManager.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

namespace {
const std::string DEFAULT_LOCAL_DB = "local";

bool directoryExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

#ifdef _WIN32
bool removeDirectoryRecursive(const std::string& path) {
    WIN32_FIND_DATAA findData;
    HANDLE handle = FindFirstFileA((joinPath(path, "*")).c_str(), &findData);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            const std::string name = findData.cFileName;
            if (name == "." || name == "..") continue;

            const std::string fullPath = joinPath(path, name);
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if (!removeDirectoryRecursive(fullPath)) {
                    FindClose(handle);
                    return false;
                }
            }
            else {
                SetFileAttributesA(fullPath.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileA(fullPath.c_str())) {
                    FindClose(handle);
                    return false;
                }
            }
        } while (FindNextFileA(handle, &findData) != 0);
        FindClose(handle);
    }

    if (RemoveDirectoryA(path.c_str()) != 0) return true;
    return GetLastError() == ERROR_FILE_NOT_FOUND;
}
#else
bool removeDirectoryRecursive(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) return rmdir(path.c_str()) == 0;

    dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        const std::string fullPath = joinPath(path, name);
        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0) {
            closedir(dir);
            return false;
        }

        if ((st.st_mode & S_IFDIR) != 0) {
            if (!removeDirectoryRecursive(fullPath)) {
                closedir(dir);
                return false;
            }
        }
        else {
            if (std::remove(fullPath.c_str()) != 0) {
                closedir(dir);
                return false;
            }
        }
    }

    closedir(dir);
    return rmdir(path.c_str()) == 0;
}
#endif
}

DatabaseManager::DatabaseManager() {
    fileManager = &FileManager::getInstance();  // 在构造函数体内初始化
}

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

const std::string& DatabaseManager::defaultDBName() {
    return DEFAULT_LOCAL_DB;
}

void DatabaseManager::saveDBs(const std::vector<DBInfo>& dbs) {
    fileManager->writeAllStruct(joinPath(g_root, SYS_DB_FILE), dbs);
}

bool DatabaseManager::createDB(const std::string& name) {
    if (!SecurityManager::getInstance().requireAdmin()) return false;
    if (name.empty() || name.length() > MAX_NAME_LEN) {
        std::cout << "Err: 名字无效\n\n";
        return false;
    }
    if (isDBExists(name)) {
        std::cout << "Err: 数据库已存在\n\n";
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

    fileManager->writeAllStruct(joinPath(db_path, name + ".meta"), std::vector<TableInfo>{});
    std::ofstream logOfs(joinPath(db_path, name + ".log"), std::ios::trunc);
    logOfs.close();

    std::cout << "OK: 数据库 " << name << " 创建成功\n";
    return true;
}

bool DatabaseManager::ensureDefaultDB() {
    if (isDBExists(defaultDBName())) return true;

    DBInfo new_db;
    safeStrncpy(new_db.name, defaultDBName().c_str(), MAX_NAME_LEN);
    std::string db_path = joinPath(joinPath(g_root, "data"), defaultDBName());
    safeStrncpy(new_db.path, db_path.c_str(), MAX_PATH_LEN);

    MKDIR(joinPath(g_root, "data").c_str());
    MKDIR(db_path.c_str());

    auto dbs = getAllDBs();
    dbs.push_back(new_db);
    saveDBs(dbs);

    fileManager->writeAllStruct(joinPath(db_path, defaultDBName() + ".meta"), std::vector<TableInfo>{});
    std::ofstream logOfs(joinPath(db_path, defaultDBName() + ".log"), std::ios::trunc);
    logOfs.close();

    std::cout << "[系统] 已初始化默认本地数据库: " << defaultDBName() << "\n";
    return true;
}

bool DatabaseManager::dropDB(const std::string& name) {
    if (!SecurityManager::getInstance().requireAdmin()) return false;
    if (name == defaultDBName()) {
        std::cout << "Err: 默认本地数据库不可删除\n";
        return false;
    }
    auto dbs = getAllDBs();
    auto it = std::remove_if(dbs.begin(), dbs.end(), [&](const DBInfo& db) {
        return std::string(db.name) == name;
        });
    if (it == dbs.end()) {
        std::cout << "Err: 数据库不存在\n\n";
        return false;
    }

    const std::string dbPath = joinPath(joinPath(g_root, "data"), name);
    if (directoryExists(dbPath) && !removeDirectoryRecursive(dbPath)) {
        std::cout << "Err: 数据库目录删除失败\n";
        return false;
    }

    dbs.erase(it, dbs.end());
    saveDBs(dbs);
    if (g_current_db == name) g_current_db.clear();

    std::cout << "OK: 数据库 " << name << " 删除成功\n";
    return true;
}

bool DatabaseManager::useDB(const std::string& name) {
    if (!SecurityManager::getInstance().requireLogin()) return false;
    if (isDBExists(name)) {
        g_current_db = name;
        std::cout << "OK: 切换到 " << name << "\n";
        return true;
    }
    std::cout << "Err: 数据库不存在\n\n";
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
    return joinPath(joinPath(g_root, "data"), name);
}
