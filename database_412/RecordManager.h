#ifndef RECORD_MANAGER_H
#define RECORD_MANAGER_H
#include "common.h"
#include <memory>

class FileManager;

class RecordManager {
public:
    static RecordManager& getInstance();
    RecordManager(const RecordManager&) = delete;
    RecordManager& operator=(const RecordManager&) = delete;

    // 插入
    bool insertRecord(const std::string& tname, const std::vector<std::string>& values);
    bool insertRecord(const std::string& tname,
        const std::vector<std::string>& cols,
        const std::vector<std::string>& values);

    // 全表查询
    bool selectRecords(const std::string& tname);

    // 带列选择和条件的查询
    bool selectRecords(const std::string& tname,
        const std::vector<std::string>& outCols,
        const ExprNode* whereCond,
        const std::string& orderByCol, bool orderAsc,
        const std::string& groupByCol,
        AggFuncType aggFunc, const std::string& aggCol);

    // 原有按行更新/删除
    bool updateRecord(const std::string& tname, const std::string& col, const std::string& val, int row);
    bool deleteRecord(const std::string& tname, int row);

    // 条件更新/删除
    bool updateRecords(const std::string& tname, const std::string& setCol, const std::string& setVal,
        const ExprNode* whereCond);
    bool deleteRecords(const std::string& tname, const ExprNode* whereCond);

private:
    FileManager* fileManager;
    RecordManager();
    ~RecordManager() = default;
    std::vector<std::string> readRecs(const std::string& tname);
    void writeRecs(const std::string& tname, const std::vector<std::string>& recs);

    // 内部辅助
    static bool evaluateExpr(const ExprNode* node, const std::vector<FieldInfo>& fields,
        const std::vector<std::string>& rowCols);
    static double getColumnValue(const std::string& val, DataType type);
    static std::string getColumnString(const std::string& val);
};

#endif
