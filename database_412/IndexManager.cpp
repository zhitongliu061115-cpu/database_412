#include "IndexManager.h"
#include "FieldManager.h"
#include "TableManager.h"
#include "DatabaseManager.h"
#include "RecordManager.h"
#include "FileManager.h"
#include "SecurityManager.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <set>

namespace {
constexpr size_t BTREE_ORDER = 32;

struct BTreeNode {
    explicit BTreeNode(bool isLeaf) : leaf(isLeaf), next(nullptr) {}

    bool leaf;
    std::vector<std::string> keys;
    std::vector<std::unique_ptr<BTreeNode>> children;
    std::vector<std::vector<int>> rows;
    BTreeNode* next;
};

struct SplitResult {
    std::string promotedKey;
    std::unique_ptr<BTreeNode> rightNode;
};

class BTreeIndex {
public:
    BTreeIndex() : root_(std::make_unique<BTreeNode>(true)) {}

    void insert(const std::string& key, int row) {
        auto split = insertRecursive(root_.get(), key, row);
        if (!split) return;

        auto newRoot = std::make_unique<BTreeNode>(false);
        newRoot->keys.push_back(split->promotedKey);
        newRoot->children.push_back(std::move(root_));
        newRoot->children.push_back(std::move(split->rightNode));
        root_ = std::move(newRoot);
    }

    std::vector<int> lookup(const std::string& key, CompOp op) const {
        std::vector<int> result;
        if (op == CompOp::NE) return result;

        if (op == CompOp::EQ) {
            const BTreeNode* leaf = findLeaf(key);
            if (!leaf) return result;

            auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
            if (it == leaf->keys.end() || *it != key) return result;

            const size_t idx = static_cast<size_t>(it - leaf->keys.begin());
            result.insert(result.end(), leaf->rows[idx].begin(), leaf->rows[idx].end());
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
                    result.insert(result.end(), leaf->rows[i].begin(), leaf->rows[i].end());
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
                for (int row : leaf->rows[i]) {
                    IndexEntry entry;
                    safeStrncpy(entry.key, leaf->keys[i].c_str(), MAX_VALUE_LEN);
                    entry.row = static_cast<int32_t>(row);
                    entries.push_back(entry);
                }
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

    std::unique_ptr<SplitResult> insertRecursive(BTreeNode* node, const std::string& key, int row) {
        if (node->leaf) {
            auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
            const size_t idx = static_cast<size_t>(it - node->keys.begin());

            if (it != node->keys.end() && *it == key) {
                auto& rowList = node->rows[idx];
                auto rowIt = std::lower_bound(rowList.begin(), rowList.end(), row);
                rowList.insert(rowIt, row);
            }
            else {
                node->keys.insert(it, key);
                node->rows.insert(node->rows.begin() + idx, std::vector<int>{ row });
            }

            if (node->keys.size() <= BTREE_ORDER) return nullptr;
            return splitLeaf(node);
        }

        auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
        const size_t childIdx = static_cast<size_t>(it - node->keys.begin());
        auto split = insertRecursive(node->children[childIdx].get(), key, row);
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
        right->rows.assign(node->rows.begin() + mid, node->rows.end());
        node->keys.erase(node->keys.begin() + mid, node->keys.end());
        node->rows.erase(node->rows.begin() + mid, node->rows.end());

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
    fileManager->writeAllStruct(getIndexPath(tname, col), entries);
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

    std::remove(getIndexPath(tname, col).c_str());

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

    auto recs = RecordManager::getInstance().readRecs(tname);
    BTreeIndex tree;

    for (size_t row = 0; row < recs.size(); ++row) {
        auto cols = split(recs[row], '|');
        if (colIdx < static_cast<int>(cols.size()) && !cols[colIdx].empty()) {
            const std::string key = formatIndexKey(cols[colIdx], colType);
            tree.insert(key, static_cast<int>(row));
        }
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
            std::remove(getIndexPath(tname, info.col_name).c_str());
        }
        else {
            kept.push_back(info);
        }
    }
    saveMeta(kept);
}

std::vector<int> IndexManager::lookup(const std::string& tname, const std::string& col,
    const std::string& value, CompOp op) {
    if (op == CompOp::NE) return {};

    auto entries = loadIndex(tname, col);
    if (entries.empty()) return {};

    const DataType colType = getColumnType(tname, col);
    const std::string formattedKey = formatIndexKey(value, colType);

    BTreeIndex tree;
    for (const auto& entry : entries) {
        tree.insert(entry.key, entry.row);
    }
    return tree.lookup(formattedKey, op);
}
