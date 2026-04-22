#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include "common.h"

class FileManager;  // 前向声明

class DatabaseManager {
public:
    static DatabaseManager& getInstance();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool createDB(const std::string& name);
    bool dropDB(const std::string& name);
    bool useDB(const std::string& name);
    bool isDBExists(const std::string& name);
    Optional<DBInfo> getDB(const std::string& name);
    std::vector<DBInfo> getAllDBs();
    std::string getDBPath(const std::string& name);

private:
    FileManager* fileManager;  // 改为指针
    DatabaseManager();
    ~DatabaseManager() = default;
    void saveDBs(const std::vector<DBInfo>& dbs);
};

#endif // DATABASE_MANAGER_H