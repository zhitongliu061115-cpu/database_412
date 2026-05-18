#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include "common.h"

class FileManager;

class BackupManager {
public:
    static BackupManager& getInstance();

    BackupManager(const BackupManager&) = delete;
    BackupManager& operator=(const BackupManager&) = delete;

    bool backupDatabase(const std::string& dbName, const std::string& filePath);
    bool restoreDatabase(const std::string& dbName, const std::string& filePath);
    bool appendLog(const std::string& dbName, const std::string& sql);
    std::string getLogPath(const std::string& dbName) const;

private:
    BackupManager();
    ~BackupManager() = default;

    std::string getDatabaseDir(const std::string& dbName) const;
    std::vector<TableInfo> loadTables(const std::string& dbName) const;
    std::vector<FieldInfo> loadFields(const std::string& dbName, const std::string& tableName) const;
    std::vector<std::string> loadRecords(const std::string& dbName, const std::string& tableName) const;
    bool cleanupDetachedDatabaseDir(const std::string& dbName) const;

    static std::string fieldTypeToSql(const FieldInfo& field);
    static std::string quoteSqlValue(const std::string& value);

    FileManager* fileManager;
};

#endif
