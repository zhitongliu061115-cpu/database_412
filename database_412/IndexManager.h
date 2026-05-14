#ifndef INDEX_MANAGER_H
#define INDEX_MANAGER_H

#include "common.h"
#include <vector>
#include <string>

class FileManager;

constexpr uint32_t MAX_VALUE_LEN = 256;

#pragma pack(push, 1)
struct IndexInfo {
    char name[MAX_NAME_LEN];
    char table_name[MAX_NAME_LEN];
    char col_name[MAX_NAME_LEN];
    uint8_t is_unique;
    DateTime mtime;

    IndexInfo() {
        memset(this, 0, sizeof(IndexInfo));
        is_unique = 0;
        mtime.init();
    }
};

struct IndexEntry {
    char key[MAX_VALUE_LEN];
    int32_t row;

    IndexEntry() {
        memset(this, 0, sizeof(IndexEntry));
        row = -1;
    }
};
#pragma pack(pop)

class IndexManager {
public:
    static IndexManager& getInstance();

    IndexManager(const IndexManager&) = delete;
    IndexManager& operator=(const IndexManager&) = delete;

    // 创建索引（自动从 .rec 文件构建）
    bool createIndex(const std::string& tname, const std::string& col,
        const std::string& idxName, bool unique = false);

    // 删除索引
    bool dropIndex(const std::string& tname, const std::string& col);
    bool dropIndexByName(const std::string& idxName, const std::string& tname = "");

    // 检查某表某列上是否有索引
    bool hasIndex(const std::string& tname, const std::string& col);

    // 获取索引信息
    Optional<IndexInfo> getIndex(const std::string& tname, const std::string& col);

    // 重建单个索引（在 INSERT/UPDATE/DELETE 后调用）
    void rebuildIndex(const std::string& tname, const std::string& col);

    // 重建某表所有索引
    void rebuildAllIndexes(const std::string& tname);
    void dropAllIndexes(const std::string& tname);

    // ====== 索引查找（核心） ======
    // 根据条件返回匹配的行号列表。NE 操作返回空 vector 表示走全表扫描
    std::vector<int> lookup(const std::string& tname, const std::string& col,
        const std::string& value, CompOp op);

private:
    FileManager* fileManager;
    IndexManager();
    ~IndexManager() = default;

    // 索引文件路径
    std::string getIndexPath(const std::string& tname, const std::string& col);
    std::string getIndexMetaPath();

    // 读写索引
    std::vector<IndexEntry> loadIndex(const std::string& tname, const std::string& col);
    void saveIndex(const std::string& tname, const std::string& col,
        const std::vector<IndexEntry>& entries);

    // 元数据管理
    std::vector<IndexInfo> loadMeta();
    void saveMeta(const std::vector<IndexInfo>& meta);
};

#endif // INDEX_MANAGER_H

