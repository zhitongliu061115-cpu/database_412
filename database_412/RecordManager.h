#ifndef RECORD_MANAGER_H
#define RECORD_MANAGER_H

#include "common.h"

class FileManager;  // 前向声明

class RecordManager {
public:
    static RecordManager& getInstance();

    RecordManager(const RecordManager&) = delete;
    RecordManager& operator=(const RecordManager&) = delete;

    bool insertRecord(const std::string& tname, const std::vector<std::string>& values);
    bool selectRecords(const std::string& tname);
    bool updateRecord(const std::string& tname, const std::string& col, const std::string& val, int row);
    bool deleteRecord(const std::string& tname, int row);

private:
    FileManager* fileManager;  // 改为指针
    RecordManager();
    ~RecordManager() = default;
    std::vector<std::string> readRecs(const std::string& tname);
    void writeRecs(const std::string& tname, const std::vector<std::string>& recs);
};

#endif // RECORD_MANAGER_H