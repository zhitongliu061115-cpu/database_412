#include "IndexManager.h"
#include "FieldManager.h"
#include "TableManager.h"
#include "DatabaseManager.h"
#include "RecordManager.h"
#include "FileManager.h"
#include "SecurityManager.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <limits>
#include <memory>
#include <set>
#include <unordered_map>

namespace {
constexpr size_t BTREE_ORDER = 32;

struct BTreeNode {
    explicit BTreeNode(bool isLeaf) : leaf(isLeaf), next(nullptr) {}

    bool leaf;
    std::vector<std::string> keys;
    std::vector<std::unique_ptr<BTreeNode>> children;
    std::vector<std::vector<IndexEntry>> entries;
    BTreeNode* next;
};

struct SplitResult {
    std::string promotedKey;
    std::unique_ptr<BTreeNode> rightNode;
};

class BTreeIndex {
public:
    BTreeIndex() : root_(std::make_unique<BTreeNode>(true)) {}

    void insert(const IndexEntry& entry) {
        auto split = insertRecursive(root_.get(), entry);
        if (!split) return;

        auto newRoot = std::make_unique<BTreeNode>(false);
        newRoot->keys.push_back(split->promotedKey);
        newRoot->children.push_back(std::move(root_));
        newRoot->children.push_back(std::move(split->rightNode));
        root_ = std::move(newRoot);
    }

    std::vector<IndexEntry> lookupEntries(const std::string& key, CompOp op) const {
        std::vector<IndexEntry> result;
        if (op == CompOp::NE) return result;

        if (op == CompOp::EQ) {
            const BTreeNode* leaf = findLeaf(key);
            if (!leaf) return result;

            auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
            if (it == leaf->keys.end() || *it != key) return result;

            const size_t idx = static_cast<size_t>(it - leaf->keys.begin());
            result.insert(result.end(), leaf->entries[idx].begin(), leaf->entries[idx].end());
            return result;
        }

        const BTreeNode* leaf = firstLeaf();
        if (op == CompOp::GT || op == CompOp::GE) {
            leaf = findLeaf(key);
            if (!leaf) return result;
        }

        while (leaf) {
            for (size_t i = 0; i < leaf->keys.size(); ++i) {
                const int cmp = compareKey(leaf->keys[i], key);
                bool matched = false;

                switch (op) {
                case CompOp::LT: matched = cmp < 0; break;
                case CompOp::LE: matched = cmp <= 0; break;
                case CompOp::GT: matched = cmp > 0; break;
                case CompOp::GE: matched = cmp >= 0; break;
                default: break;
                }

                if ((op == CompOp::LT || op == CompOp::LE) && !matched) {
                    return result;
                }

                if (matched) {
                    result.insert(result.end(), leaf->entries[i].begin(), leaf->entries[i].end());
                }
            }
            leaf = leaf->next;
        }

        return result;
    }

    std::vector<IndexEntry> toEntries() const {
        std::vector<IndexEntry> entries;
        const BTreeNode* leaf = firstLeaf();

        while (leaf) {
            for (size_t i = 0; i < leaf->keys.size(); ++i) {
                entries.insert(entries.end(), leaf->entries[i].begin(), leaf->entries[i].end());
            }
            leaf = leaf->next;
        }

        return entries;
    }

private:
    static int compareKey(const std::string& lhs, const std::string& rhs) {
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    }

    std::unique_ptr<SplitResult> insertRecursive(BTreeNode* node, const IndexEntry& entry) {
        const std::string key = entry.key;
        if (node->leaf) {
            auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
            const size_t idx = static_cast<size_t>(it - node->keys.begin());

            if (it != node->keys.end() && *it == key) {
                auto& entryList = node->entries[idx];
                auto entryIt = std::lower_bound(entryList.begin(), entryList.end(), entry.row,
                    [](const IndexEntry& item, int32_t row) {
                        return item.row < row;
                    });
                entryList.insert(entryIt, entry);
            }
            else {
                node->keys.insert(it, key);
                node->entries.insert(node->entries.begin() + idx, std::vector<IndexEntry>{ entry });
            }

            if (node->keys.size() <= BTREE_ORDER) return nullptr;
            return splitLeaf(node);
        }

        auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
        const size_t childIdx = static_cast<size_t>(it - node->keys.begin());
        auto split = insertRecursive(node->children[childIdx].get(), entry);
        if (!split) return nullptr;

        node->keys.insert(node->keys.begin() + childIdx, split->promotedKey);
        node->children.insert(node->children.begin() + childIdx + 1, std::move(split->rightNode));

        if (node->keys.size() <= BTREE_ORDER) return nullptr;
        return splitInternal(node);
    }

    std::unique_ptr<SplitResult> splitLeaf(BTreeNode* node) {
        const size_t mid = node->keys.size() / 2;
        auto right = std::make_unique<BTreeNode>(true);

        right->keys.assign(node->keys.begin() + mid, node->keys.end());
        right->entries.assign(node->entries.begin() + mid, node->entries.end());
        node->keys.erase(node->keys.begin() + mid, node->keys.end());
        node->entries.erase(node->entries.begin() + mid, node->entries.end());

        right->next = node->next;
        node->next = right.get();

        auto split = std::make_unique<SplitResult>();
        split->promotedKey = right->keys.front();
        split->rightNode = std::move(right);
        return split;
    }

    std::unique_ptr<SplitResult> splitInternal(BTreeNode* node) {
        const size_t mid = node->keys.size() / 2;
        auto right = std::make_unique<BTreeNode>(false);
        const std::string promoted = node->keys[mid];

        right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
        right->children.reserve(node->children.size() - mid - 1);
        for (size_t i = mid + 1; i < node->children.size(); ++i) {
            right->children.push_back(std::move(node->children[i]));
        }

        node->keys.erase(node->keys.begin() + mid, node->keys.end());
        node->children.erase(node->children.begin() + mid + 1, node->children.end());

        auto split = std::make_unique<SplitResult>();
        split->promotedKey = promoted;
        split->rightNode = std::move(right);
        return split;
    }

    const BTreeNode* findLeaf(const std::string& key) const {
        const BTreeNode* node = root_.get();
        while (node && !node->leaf) {
            auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
            const size_t idx = static_cast<size_t>(it - node->keys.begin());
            node = node->children[idx].get();
        }
        return node;
    }

    const BTreeNode* firstLeaf() const {
        const BTreeNode* node = root_.get();
        while (node && !node->leaf) {
            node = node->children.front().get();
        }
        return node;
    }

    std::unique_ptr<BTreeNode> root_;
};

struct CachedIndex {
    std::string path;
    std::time_t mtime;
    BTreeIndex tree;
};

std::unordered_map<std::string, CachedIndex> g_indexCache;

std::time_t getFileMTime(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return st.st_mtime;
}

std::string formatIndexKey(const std::string& value, DataType type) {
    std::string key = value;

    if (type == DataType::INT) {
        try {
            const long long num = std::stoll(value);
            const unsigned long long shifted =
                static_cast<unsigned long long>(num) ^
                (1ull << (std::numeric_limits<unsigned long long>::digits - 1));
            char buf[MAX_VALUE_LEN];
            std::snprintf(buf, sizeof(buf), "%020llu", shifted);
            key = buf;
        }
        catch (...) {
        }
    }
    else if (type == DataType::DOUBLE) {
        try {
            const double num = std::stod(value);
            uint64_t bits = 0;
            memcpy(&bits, &num, sizeof(bits));
            if ((bits & (1ull << 63)) != 0) {
                bits = ~bits;
            }
            else {
                bits ^= (1ull << 63);
            }

            char buf[MAX_VALUE_LEN];
            std::snprintf(buf, sizeof(buf), "%016llx",
                static_cast<unsigned long long>(bits));
            key = buf;
        }
        catch (...) {
        }
    }

    return key;
}

DataType getColumnType(const std::string& tname, const std::string& col) {
    auto flds = FieldManager::getInstance().getFields(tname);
    for (const auto& f : flds) {
        if (std::string(f.name) == col) return f.type;
    }
    return DataType::VARCHAR;
}

BTreeIndex buildTreeFromEntries(const std::vector<IndexEntry>& entries) {
    BTreeIndex tree;
    for (const auto& entry : entries) {
        tree.insert(entry);
    }
    return tree;
}

}

IndexManager::IndexManager() {
    fileManager = &FileManager::getInstance();
}

IndexManager& IndexManager::getInstance() {
    static IndexManager instance;
    return instance;
}

std::string IndexManager::getIndexPath(const std::string& tname, const std::string& col) {
    const std::string dir = TableManager::getInstance().getTableDir();
    return joinPath(dir, tname + ".idx_" + col);
}

std::string IndexManager::getIndexMetaPath() {
    const std::string dir = DatabaseManager::getInstance().getDBPath(g_current_db);
    return joinPath(dir, "indexes.meta");
}

std::vector<IndexEntry> IndexManager::loadIndex(const std::string& tname, const std::string& col) {
    return fileManager->readAllStruct<IndexEntry>(getIndexPath(tname, col));
}

void IndexManager::saveIndex(const std::string& tname, const std::string& col,
    const std::vector<IndexEntry>& entries) {
    const std::string path = getIndexPath(tname, col);
    fileManager->writeAllStruct(path, entries);
    g_indexCache.erase(path);
}

std::vector<IndexInfo> IndexManager::loadMeta() {
    return fileManager->readAllStruct<IndexInfo>(getIndexMetaPath());
}

void IndexManager::saveMeta(const std::vector<IndexInfo>& meta) {
    fileManager->writeAllStruct(getIndexMetaPath(), meta);
}

bool IndexManager::createIndex(const std::string& tname, const std::string& col,
    const std::string& idxName, bool unique) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_ALTER)) {
        return false;
    }

    if (!TableManager::getInstance().isTableExists(tname)) {
        std::cout << "Err: 表不存在\n";
        return false;
    }

    auto flds = FieldManager::getInstance().getFields(tname);
    bool colExists = false;
    for (const auto& f : flds) {
        if (std::string(f.name) == col) {
            colExists = true;
            break;
        }
    }
    if (!colExists) {
        std::cout << "Err: 列 " << col << " 不存在\n";
        return false;
    }

    if (unique) {
        int colIdx = -1;
        for (size_t i = 0; i < flds.size(); ++i) {
            if (std::string(flds[i].name) == col) {
                colIdx = static_cast<int>(i);
                break;
            }
        }
        std::set<std::string> seen;
        const auto recs = RecordManager::getInstance().readRecs(tname);
        for (const auto& rec : recs) {
            auto cols = split(rec, '|');
            if (colIdx >= 0 && colIdx < static_cast<int>(cols.size()) && !cols[colIdx].empty()) {
                if (!seen.insert(cols[colIdx]).second) {
                    std::cout << "Err: UNIQUE index column has duplicate values\n";
                    return false;
                }
            }
        }
    }

    if (hasIndex(tname, col)) {
        std::cout << "Err: 该列上已有索引\n";
        return false;
    }

    auto meta = loadMeta();
    for (const auto& m : meta) {
        if (std::string(m.name) == idxName && std::string(m.table_name) == tname) {
            std::cout << "Err: 索引名 " << idxName << " 已存在\n";
            return false;
        }
    }

    IndexInfo info;
    safeStrncpy(info.name, idxName.c_str(), MAX_NAME_LEN);
    safeStrncpy(info.table_name, tname.c_str(), MAX_NAME_LEN);
    safeStrncpy(info.col_name, col.c_str(), MAX_NAME_LEN);
    info.is_unique = unique ? 1 : 0;
    meta.push_back(info);
    saveMeta(meta);

    rebuildIndex(tname, col);

    std::cout << "OK: B树索引 " << idxName << " 创建成功 ("
        << (unique ? "UNIQUE" : "NON-UNIQUE") << ")\n";
    return true;
}

bool IndexManager::dropIndex(const std::string& tname, const std::string& col) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_ALTER)) {
        return false;
    }

    if (!hasIndex(tname, col)) {
        std::cout << "Err: 索引不存在\n";
        return false;
    }

    const std::string indexPath = getIndexPath(tname, col);
    std::remove(indexPath.c_str());
    g_indexCache.erase(indexPath);

    auto meta = loadMeta();
    meta.erase(std::remove_if(meta.begin(), meta.end(), [&](const IndexInfo& info) {
        return std::string(info.table_name) == tname &&
            std::string(info.col_name) == col;
        }), meta.end());
    saveMeta(meta);

    std::cout << "OK: 索引已删除\n";
    return true;
}

bool IndexManager::hasIndex(const std::string& tname, const std::string& col) {
    return FileManager::fileExists(getIndexPath(tname, col));
}

bool IndexManager::dropIndexByName(const std::string& idxName, const std::string& tname) {
    if (!SecurityManager::getInstance().requireLogin()) return false;
    if (g_current_db.empty()) {
        std::cout << "Err: 璇峰厛 USE 鏁版嵁搴揬n";
        return false;
    }

    auto meta = loadMeta();
    std::vector<IndexInfo> matched;
    for (const auto& info : meta) {
        if (std::string(info.name) != idxName) continue;
        if (!tname.empty() && std::string(info.table_name) != tname) continue;
        matched.push_back(info);
    }

    if (matched.empty()) {
        std::cout << "Err: 绱㈠紩涓嶅瓨鍦╘n";
        return false;
    }
    if (matched.size() > 1 && tname.empty()) {
        std::cout << "Err: 绱㈠紩鍚嶄笉鍞竴锛岃浣跨敤 DROP INDEX "
            << idxName << " ON <table>\n";
        return false;
    }

    return dropIndex(matched.front().table_name, matched.front().col_name);
}

Optional<IndexInfo> IndexManager::getIndex(const std::string& tname, const std::string& col) {
    auto meta = loadMeta();
    for (const auto& info : meta) {
        if (std::string(info.table_name) == tname &&
            std::string(info.col_name) == col) {
            return Optional<IndexInfo>(info);
        }
    }
    return Optional<IndexInfo>();
}

void IndexManager::rebuildIndex(const std::string& tname, const std::string& col) {
    auto flds = FieldManager::getInstance().getFields(tname);

    int colIdx = -1;
    DataType colType = DataType::VARCHAR;
    for (size_t i = 0; i < flds.size(); ++i) {
        if (std::string(flds[i].name) == col) {
            colIdx = static_cast<int>(i);
            colType = flds[i].type;
            break;
        }
    }
    if (colIdx == -1) return;

    BTreeIndex tree;

    const std::string recPath = joinPath(TableManager::getInstance().getTableDir(), tname + ".rec");
    std::ifstream ifs(recPath, std::ios::binary);
    std::string rec;
    int32_t row = 0;
    while (ifs) {
        const std::streampos pos = ifs.tellg();
        if (!std::getline(ifs, rec)) break;
        if (rec.empty()) continue;

        auto cols = split(rec, '|');
        if (colIdx < static_cast<int>(cols.size()) && !cols[colIdx].empty()) {
            IndexEntry entry;
            const std::string key = formatIndexKey(cols[colIdx], colType);
            safeStrncpy(entry.key, key.c_str(), MAX_VALUE_LEN);
            entry.row = row;
            entry.offset = static_cast<int64_t>(pos);
            tree.insert(entry);
        }
        ++row;
    }

    saveIndex(tname, col, tree.toEntries());
}

void IndexManager::rebuildAllIndexes(const std::string& tname) {
    auto meta = loadMeta();
    for (const auto& info : meta) {
        if (std::string(info.table_name) == tname) {
            rebuildIndex(tname, info.col_name);
        }
    }
}

void IndexManager::dropAllIndexes(const std::string& tname) {
    auto meta = loadMeta();
    std::vector<IndexInfo> kept;
    for (const auto& info : meta) {
        if (std::string(info.table_name) == tname) {
            const std::string indexPath = getIndexPath(tname, info.col_name);
            std::remove(indexPath.c_str());
            g_indexCache.erase(indexPath);
        }
        else {
            kept.push_back(info);
        }
    }
    saveMeta(kept);
}

std::vector<int> IndexManager::lookup(const std::string& tname, const std::string& col,
    const std::string& value, CompOp op) {
    std::vector<int> rows;
    if (op == CompOp::NE) return rows;

    const DataType colType = getColumnType(tname, col);
    const std::string formattedKey = formatIndexKey(value, colType);
    const std::string indexPath = getIndexPath(tname, col);
    const std::time_t mtime = getFileMTime(indexPath);

    auto cacheIt = g_indexCache.find(indexPath);
    if (cacheIt == g_indexCache.end() || cacheIt->second.mtime != mtime) {
        auto entries = loadIndex(tname, col);
        if (entries.empty()) return rows;

        CachedIndex cached;
        cached.path = indexPath;
        cached.mtime = mtime;
        cached.tree = buildTreeFromEntries(entries);

        g_indexCache.erase(indexPath);
        auto inserted = g_indexCache.emplace(indexPath, std::move(cached));
        cacheIt = inserted.first;
    }

    const auto matches = cacheIt->second.tree.lookupEntries(formattedKey, op);
    for (const auto& entry : matches) {
        rows.push_back(entry.row);
    }
    return rows;
}

std::vector<int64_t> IndexManager::lookupOffsets(const std::string& tname, const std::string& col,
    const std::string& value, CompOp op) {
    std::vector<int64_t> offsets;
    if (op == CompOp::NE) return offsets;

    const DataType colType = getColumnType(tname, col);
    const std::string formattedKey = formatIndexKey(value, colType);
    const std::string indexPath = getIndexPath(tname, col);
    const std::time_t mtime = getFileMTime(indexPath);

    auto cacheIt = g_indexCache.find(indexPath);
    if (cacheIt == g_indexCache.end() || cacheIt->second.mtime != mtime) {
        auto entries = loadIndex(tname, col);
        if (entries.empty()) return offsets;

        CachedIndex cached;
        cached.path = indexPath;
        cached.mtime = mtime;
        cached.tree = buildTreeFromEntries(entries);

        g_indexCache.erase(indexPath);
        auto inserted = g_indexCache.emplace(indexPath, std::move(cached));
        cacheIt = inserted.first;
    }

    const auto matches = cacheIt->second.tree.lookupEntries(formattedKey, op);
    offsets.reserve(matches.size());
    for (const auto& entry : matches) {
        if (entry.offset >= 0) offsets.push_back(entry.offset);
    }
    return offsets;
}
