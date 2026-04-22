#ifndef TABLE_MANAGER_H
#define TABLE_MANAGER_H

#include "common.h"

class FileManager;  // 前向声明

class TableManager {
public:
    static TableManager& getInstance();

    TableManager(const TableManager&) = delete;
    TableManager& operator=(const TableManager&) = delete;

    bool createTable(const std::string& name);
    bool dropTable(const std::string& name);
    bool isTableExists(const std::string& name);
    Optional<TableInfo> getTable(const std::string& name);
    std::vector<TableInfo> getAllTables();
    void updateTable(const std::string& name, const TableInfo& newInfo);
    std::string getTableMetaPath();
    std::string getTableDir();

private:
    FileManager* fileManager;  // 改为指针
    TableManager();
    ~TableManager() = default;
    void saveTables(const std::vector<TableInfo>& tables);
};

#endif // TABLE_MANAGER_H