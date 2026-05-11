#ifndef RECORD_MANAGER_H
#define RECORD_MANAGER_H

#include "common.h"

class FileManager;  // 前向声明

class RecordManager {
public:
    static RecordManager& getInstance();

    RecordManager(const RecordManager&) = delete;
    RecordManager& operator=(const RecordManager&) = delete;

    // 原有接口
    bool insertRecord(const std::string& tname, const std::vector<std::string>& values);
    bool selectRecords(const std::string& tname);
    bool updateRecord(const std::string& tname, const std::string& col, const std::string& val, int row);
    bool deleteRecord(const std::string& tname, int row);

    // 新增接口：支持列值条件
    bool selectRecords(const std::string& tname, const std::string& whereCol, const std::string& whereVal);
    bool updateRecords(const std::string& tname, const std::string& setCol, const std::string& setVal,
        const std::string& whereCol, const std::string& whereVal);
    bool deleteRecords(const std::string& tname, const std::string& whereCol, const std::string& whereVal);

private:
    FileManager* fileManager;
    RecordManager();
    ~RecordManager() = default;

    std::vector<std::string> readRecs(const std::string& tname);
    void writeRecs(const std::string& tname, const std::vector<std::string>& recs);

    // 静态辅助函数：根据列名和值返回匹配的行号（0-based）
    static std::vector<int> findRowsByCondition(const std::string& tname,
        const std::string& colName, const std::string& val);
};

#endif // RECORD_MANAGER_H
