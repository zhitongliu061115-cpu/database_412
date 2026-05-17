#include "RecordManager.h"
#include "FieldManager.h"
#include "IndexManager.h"
#include "TableManager.h"
#include "FileManager.h"
#include "LockManager.h"
#include "SecurityManager.h"
#include "Transaction.h"
#include <algorithm>    // std::sort, std::min_element, std::max_element
#include <numeric>      // std::accumulate
#include <functional>   // std::greater
#include <map>
#include <set>

namespace {
struct IndexProbe {
    std::string col;
    std::string value;
    CompOp op;
};

CompOp reverseCompOp(CompOp op) {
    switch (op) {
    case CompOp::LT: return CompOp::GT;
    case CompOp::LE: return CompOp::GE;
    case CompOp::GT: return CompOp::LT;
    case CompOp::GE: return CompOp::LE;
    default: return op;
    }
}

bool tryBuildIndexProbe(const std::string& tname, const ExprNode* node, IndexProbe& probe) {
    if (!node) return false;

    if (node->type == ExprNode::BINARY) {
        if (node->logic != LogicOp::AND) return false;
        return tryBuildIndexProbe(tname, node->left.get(), probe) ||
            tryBuildIndexProbe(tname, node->right.get(), probe);
    }

    if (node->type != ExprNode::UNARY || node->comp == CompOp::NE) return false;
    if (!node->left || !node->right ||
        node->left->type != ExprNode::LEAF ||
        node->right->type != ExprNode::LEAF) {
        return false;
    }

    const bool leftIsCol = !node->left->colName.empty();
    const bool rightIsCol = !node->right->colName.empty();

    if (leftIsCol && !rightIsCol &&
        IndexManager::getInstance().hasIndex(tname, node->left->colName)) {
        probe.col = node->left->colName;
        probe.value = unquote(node->right->value);
        probe.op = node->comp;
        return true;
    }

    if (!leftIsCol && rightIsCol &&
        IndexManager::getInstance().hasIndex(tname, node->right->colName)) {
        probe.col = node->right->colName;
        probe.value = unquote(node->left->value);
        probe.op = reverseCompOp(node->comp);
        return true;
    }

    return false;
}
}

// ==================== 构造函数 & 单例 ====================
RecordManager::RecordManager() {
    fileManager = &FileManager::getInstance();
}

RecordManager& RecordManager::getInstance() {
    static RecordManager instance;
    return instance;
}

// ==================== 私有：读写记录文件 ====================
std::vector<std::string> RecordManager::readRecs(const std::string& tname) {
    std::vector<std::string> res;
    std::ifstream ifs(joinPath(TableManager::getInstance().getTableDir(), tname + ".rec"));
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty()) res.push_back(line);
    }
    return res;
}

void RecordManager::writeRecs(const std::string& tname, const std::vector<std::string>& recs) {
    std::ofstream ofs(joinPath(TableManager::getInstance().getTableDir(), tname + ".rec"), std::ios::trunc);
    for (const auto& r : recs) {
        ofs << r << "\n";
    }
    ofs.close();
    if (!g_current_db.empty()) {
        IndexManager::getInstance().rebuildAllIndexes(tname);
    }
}

// ==================== 插入（按顺序） ====================
bool RecordManager::insertRecord(const std::string& tname, const std::vector<std::string>& values) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_INSERT)) return false;
    auto flds = FieldManager::getInstance().getFields(tname);
    if (values.size() != flds.size()) {
        std::cout << "Err: 值数量不匹配 (需要 " << flds.size() << ")\n";
        return false;
    }
    // 类型校验 & NOT NULL
    for (size_t i = 0; i < flds.size(); i++) {
        std::string v = unquote(values[i]);
        if (v.empty() && (flds[i].flags & FIELD_FLAG_NOT_NULL)) {
            std::cout << "Err: 字段 " << flds[i].name << " 不能为空\n";
            return false;
        }
        if (!v.empty() && !validateValue(v, flds[i].type, flds[i].param)) {
            std::cout << "Err: 字段 " << flds[i].name << " 值类型不匹配\n";
            return false;
        }
    }
    std::string line;
    for (size_t i = 0; i < values.size(); i++) {
        std::string v = unquote(values[i]);
        line += v + (i == values.size() - 1 ? "" : "|");
    }
    auto recs = readRecs(tname);
    recs.push_back(line);
    writeRecs(tname, recs);

    // 更新表记录数
    auto tableOpt = TableManager::getInstance().getTable(tname);
    if (tableOpt.has_value()) {
        TableInfo t = tableOpt.value();
        t.record_count = static_cast<int32_t>(recs.size());
        t.mtime.init();
        TableManager::getInstance().updateTable(tname, t);
    }
    std::cout << "OK: 插入成功\n";
    return true;
}

// ==================== 插入（指定列） ====================
bool RecordManager::insertRecord(const std::string& tname,
    const std::vector<std::string>& cols,
    const std::vector<std::string>& values) {
    if (cols.empty()) return insertRecord(tname, values);
    auto flds = FieldManager::getInstance().getFields(tname);
    if (cols.size() != values.size() || cols.size() > flds.size()) {
        std::cout << "Err: 列/值数量不匹配\n";
        return false;
    }
    // 构建完整一行
    std::vector<std::string> row(flds.size(), "");
    for (size_t i = 0; i < cols.size(); i++) {
        bool found = false;
        for (size_t j = 0; j < flds.size(); j++) {
            if (std::string(flds[j].name) == cols[i]) {
                std::string v = unquote(values[i]);
                if (v.empty() && (flds[j].flags & FIELD_FLAG_NOT_NULL)) {
                    std::cout << "Err: 字段 " << cols[i] << " 不能为空\n";
                    return false;
                }
                if (!v.empty() && !validateValue(v, flds[j].type, flds[j].param)) {
                    std::cout << "Err: 字段 " << cols[i] << " 值类型不匹配\n";
                    return false;
                }
                row[j] = v;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cout << "Err: 列 " << cols[i] << " 不存在\n";
            return false;
        }
    }
    // 未提供列自动留空或默认值（暂不实现DEFAULT）
    return insertRecord(tname, row);
}

// ==================== 全表查询 ====================
bool RecordManager::selectRecords(const std::string& tname) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_SELECT)) return false;
    auto flds = FieldManager::getInstance().getFields(tname);
    auto recs = readRecs(tname);

    std::cout << "\n--- " << tname << " ---\n";
    for (size_t i = 0; i < flds.size(); i++) {
        std::cout << flds[i].name << (i == flds.size() - 1 ? "\n" : "\t");
    }
    std::cout << std::string(50, '-') << "\n";
    for (const auto& r : recs) {
        auto cols = split(r, '|');
        for (size_t i = 0; i < cols.size(); i++) {
            std::cout << cols[i] << (i == cols.size() - 1 ? "\n" : "\t");
        }
    }
    std::cout << " 共 " << recs.size() << " 条记录\n\n";
    return true;
}

// ==================== 高级查询（列选择、WHERE、ORDER BY、GROUP BY、聚合） ====================
bool RecordManager::selectRecords(const std::string& tname,
    const std::vector<std::string>& outCols,
    const ExprNode* whereCond,
    const std::string& orderByCol, bool orderAsc,
    const std::string& groupByCol,
    AggFuncType aggFunc, const std::string& aggCol) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_SELECT)) return false;
    auto flds = FieldManager::getInstance().getFields(tname);
    auto recs = readRecs(tname);

    // 解析每行为 vector<string>
    std::vector<std::vector<std::string>> rows;
    IndexProbe probe;
    bool usedIndex = false;
    if (whereCond && tryBuildIndexProbe(tname, whereCond, probe)) {
        const auto indexedRows = IndexManager::getInstance().lookup(tname, probe.col, probe.value, probe.op);
        std::set<int> rowSet;
        for (int row : indexedRows) {
            if (row >= 0 && row < static_cast<int>(recs.size())) {
                rowSet.insert(row);
            }
        }
        for (size_t i = 0; i < recs.size(); ++i) {
            if (rowSet.count(static_cast<int>(i)) != 0) {
                rows.push_back(split(recs[i], '|'));
            }
        }
        usedIndex = true;
    }

    if (!usedIndex) {
        for (const auto& r : recs) {
            rows.push_back(split(r, '|'));
        }
    }

    if (TransactionManager::getInstance().hasActiveTransaction()) {
        std::string tableLock = "table:" + tname;
        if (!TransactionManager::getInstance().acquireLock(tableLock, LockType::SHARED)) {
            std::cout << "Err: 无法获取读锁，请重试\n";
            return false;
        }
    }

    // WHERE 过滤
    std::vector<std::vector<std::string>> filtered;
    for (auto& row : rows) {
        if (!whereCond || evaluateExpr(whereCond, flds, row)) {
            filtered.push_back(row);
        }
    }

    // GROUP BY 和聚合
    bool hasGroup = !groupByCol.empty();
    int groupIdx = -1;
    if (hasGroup) {
        for (size_t i = 0; i < flds.size(); i++) {
            if (std::string(flds[i].name) == groupByCol) {
                groupIdx = static_cast<int>(i);
                break;
            }
        }
        if (groupIdx == -1) {
            std::cout << "Err: 分组列不存在\n";
            return false;
        }
    }

    int aggIdx = -1;
    DataType aggType = DataType::VARCHAR;
    if (aggFunc != AggFuncType::NONE && !aggCol.empty()) {
        for (size_t i = 0; i < flds.size(); i++) {
            if (std::string(flds[i].name) == aggCol) {
                aggIdx = static_cast<int>(i);
                aggType = flds[i].type;
                break;
            }
        }
        if (aggIdx == -1) {
            std::cout << "Err: 聚合列不存在\n";
            return false;
        }
    }

    // 分组
    std::map<std::string, std::vector<std::vector<std::string>>> groups;
    if (hasGroup) {
        for (auto& row : filtered) {
            std::string key = (groupIdx < static_cast<int>(row.size())) ? row[groupIdx] : "";
            groups[key].push_back(row);
        }
    }
    else {
        groups[""] = filtered;
    }

    // 输出列索引（用于非聚合查询）
    std::vector<int> outIdx;
    std::vector<std::string> outHeaders;
    if (aggFunc == AggFuncType::NONE) {
        if (outCols.empty()) {
            for (size_t i = 0; i < flds.size(); i++) {
                outIdx.push_back(static_cast<int>(i));
                outHeaders.push_back(flds[i].name);
            }
        }
        else {
            for (const auto& col : outCols) {
                int idx = -1;
                for (size_t i = 0; i < flds.size(); i++) {
                    if (std::string(flds[i].name) == col) {
                        idx = static_cast<int>(i);
                        break;
                    }
                }
                if (idx == -1) {
                    std::cout << "Err: 输出列 " << col << " 不存在\n";
                    return false;
                }
                outIdx.push_back(idx);
                outHeaders.push_back(col);
            }
        }
    }

    // 排序（按分组键或排序列）
    // 构建可排序的分组列表
    std::vector<std::pair<std::string, std::vector<std::vector<std::string>>>> sortedGroups;
    for (auto& g : groups) {
        sortedGroups.push_back(g);
    }
    // 如果指定了 ORDER BY，对组内行排序（聚合查询时没有行内排序意义，这里按分组列排序）
    if (!orderByCol.empty()) {
        int orderIdx = -1;
        for (size_t i = 0; i < flds.size(); i++) {
            if (std::string(flds[i].name) == orderByCol) {
                orderIdx = static_cast<int>(i);
                break;
            }
        }
        if (orderIdx != -1) {
            // 对整个 filtered 的行排序（无视分组），再重新分组？这里简化：对分组列表本身排序（按分组键对应的值）
            // 但用户可能期望对普通查询的行排序。我们这里对 filtered 全局排序，然后重新生成分组（如果无分组，直接排序即可）
            if (!hasGroup) {
                // 无分组：直接对 filtered 排序
                std::sort(filtered.begin(), filtered.end(),
                    [&](const std::vector<std::string>& a, const std::vector<std::string>& b) {
                        const std::string& va = orderIdx < static_cast<int>(a.size()) ? a[orderIdx] : "";
                        const std::string& vb = orderIdx < static_cast<int>(b.size()) ? b[orderIdx] : "";
                        return orderAsc ? (va < vb) : (va > vb);
                    });
                // re-group
                sortedGroups.clear();
                sortedGroups.push_back({ "", filtered });
            }
            else {
                // 有分组：按分组键排序
                std::sort(sortedGroups.begin(), sortedGroups.end(),
                    [&](const auto& a, const auto& b) {
                        const std::string& va = a.second.empty() ? "" : a.second[0][orderIdx];
                        const std::string& vb = b.second.empty() ? "" : b.second[0][orderIdx];
                        return orderAsc ? (va < vb) : (va > vb);
                    });
            }
        }
    }

    // 输出
    std::cout << "\n--- " << tname << " ---\n";
    if (aggFunc != AggFuncType::NONE) {
        // 聚合输出表头
        std::cout << (hasGroup ? groupByCol : "ALL") << "\t"
            << aggCol << "(";
        switch (aggFunc) {
        case AggFuncType::COUNT: std::cout << "COUNT"; break;
        case AggFuncType::SUM:   std::cout << "SUM"; break;
        case AggFuncType::AVG:   std::cout << "AVG"; break;
        case AggFuncType::MIN:   std::cout << "MIN"; break;
        case AggFuncType::MAX:   std::cout << "MAX"; break;
        default: break;
        }
        std::cout << ")\n" << std::string(50, '-') << "\n";

        for (auto& g : sortedGroups) {
            std::string groupVal = g.first;
            std::vector<double> vals;
            for (auto& row : g.second) {
                if (aggIdx < static_cast<int>(row.size()) && !row[aggIdx].empty()) {
                    try {
                        vals.push_back(std::stod(row[aggIdx]));
                    }
                    catch (...) {
                        // skip invalid numbers for aggregation
                    }
                }
            }
            double result = 0.0;
            switch (aggFunc) {
            case AggFuncType::COUNT: result = static_cast<double>(vals.size()); break;
            case AggFuncType::SUM: result = std::accumulate(vals.begin(), vals.end(), 0.0); break;
            case AggFuncType::AVG: result = vals.empty() ? 0.0 : (std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size()); break;
            case AggFuncType::MIN: result = vals.empty() ? 0.0 : *std::min_element(vals.begin(), vals.end()); break;
            case AggFuncType::MAX: result = vals.empty() ? 0.0 : *std::max_element(vals.begin(), vals.end()); break;
            default: break;
            }
            if (aggFunc == AggFuncType::COUNT)
                std::cout << groupVal << "\t" << (int)result << "\n";
            else
                std::cout << groupVal << "\t" << result << "\n";
        }
    }
    else {
        // 普通输出
        for (const auto& h : outHeaders) std::cout << h << "\t";
        std::cout << "\n" << std::string(50, '-') << "\n";
        for (auto& g : sortedGroups) {
            for (auto& row : g.second) {
                for (size_t j = 0; j < outIdx.size(); j++) {
                    int idx = outIdx[j];
                    std::cout << (idx < static_cast<int>(row.size()) ? row[idx] : "")
                        << (j == outIdx.size() - 1 ? "\n" : "\t");
                }
            }
        }
    }
    std::cout << "共\ " << filtered.size() << " 条记录\n\n";
    return true;
}

// ==================== 按行更新 ====================
bool RecordManager::updateRecord(const std::string& tname, const std::string& col,
    const std::string& val, int row) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_UPDATE)) return false;
    auto flds = FieldManager::getInstance().getFields(tname);
    int col_idx = -1;
    for (size_t i = 0; i < flds.size(); i++) {
        if (std::string(flds[i].name) == col) {
            col_idx = static_cast<int>(i);
            break;
        }
    }
    if (col_idx == -1) {
        std::cout << "Err: 字段不存在\n";
        return false;
    }
    auto recs = readRecs(tname);
    if (row < 0 || row >= static_cast<int>(recs.size())) {
        std::cout << "Err: 行号无效\n";
        return false;
    }
    std::string newVal = unquote(val);
    if (!newVal.empty() && !validateValue(newVal, flds[col_idx].type, flds[col_idx].param)) {
        std::cout << "Err: 值类型不匹配\n";
        return false;
    }
    auto cols = split(recs[row], '|');
    cols[col_idx] = newVal;
    std::string newline;
    for (size_t i = 0; i < cols.size(); i++) {
        newline += cols[i] + (i == cols.size() - 1 ? "" : "|");
    }
    recs[row] = newline;
    writeRecs(tname, recs);
    std::cout << "OK: 更新成功\n";
    return true;
}

// ==================== 按行删除 ====================
bool RecordManager::deleteRecord(const std::string& tname, int row) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_DELETE)) return false;
    auto recs = readRecs(tname);
    if (row == -1) {
        recs.clear();
    }
    else if (row >= 0 && row < static_cast<int>(recs.size())) {
        recs.erase(recs.begin() + row);
    }
    else {
        std::cout << "Err: 行号无效\n";
        return false;
    }
    writeRecs(tname, recs);
    auto tableOpt = TableManager::getInstance().getTable(tname);
    if (tableOpt.has_value()) {
        TableInfo t = tableOpt.value();
        t.record_count = static_cast<int32_t>(recs.size());
        t.mtime.init();
        TableManager::getInstance().updateTable(tname, t);
    }
    std::cout << "OK: 删除成功\n";
    return true;
}

// ==================== 条件更新 ====================
bool RecordManager::updateRecords(const std::string& tname,
    const std::string& setCol, const std::string& setVal,
    const ExprNode* whereCond) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_UPDATE)) return false;
    auto flds = FieldManager::getInstance().getFields(tname);
    int setIdx = -1;
    for (size_t i = 0; i < flds.size(); i++) {
        if (std::string(flds[i].name) == setCol) {
            setIdx = static_cast<int>(i);
            break;
        }
    }
    if (setIdx == -1) {
        std::cout << "Err: SET 列不存在\n";
        return false;
    }
    std::string val = unquote(setVal);
    if (!val.empty() && !validateValue(val, flds[setIdx].type, flds[setIdx].param)) {
        std::cout << "Err: 更新值类型不匹配\n";
        return false;
    }
    auto recs = readRecs(tname);
    int updated = 0;
    std::vector<size_t> candidateRows;
    IndexProbe probe;
    if (whereCond && tryBuildIndexProbe(tname, whereCond, probe)) {
        const auto indexedRows = IndexManager::getInstance().lookup(tname, probe.col, probe.value, probe.op);
        std::set<int> rowSet;
        for (int row : indexedRows) {
            if (row >= 0 && row < static_cast<int>(recs.size())) rowSet.insert(row);
        }
        for (int row : rowSet) candidateRows.push_back(static_cast<size_t>(row));
    }
    else {
        for (size_t i = 0; i < recs.size(); ++i) candidateRows.push_back(i);
    }

    for (size_t i : candidateRows) {
        auto rowCols = split(recs[i], '|');
        if (!whereCond || evaluateExpr(whereCond, flds, rowCols)) {
            rowCols[setIdx] = val;
            std::string newLine;
            for (size_t j = 0; j < rowCols.size(); j++) {
                newLine += rowCols[j] + (j == rowCols.size() - 1 ? "" : "|");
            }
            recs[i] = newLine;
            updated++;
        }
    }
    writeRecs(tname, recs);
    auto t = TableManager::getInstance().getTable(tname).value();
    t.mtime.init();
    TableManager::getInstance().updateTable(tname, t);
    std::cout << "OK: 更新 " << updated << " 行\n\n";
    return true;
}

// ==================== 条件删除 ====================
bool RecordManager::deleteRecords(const std::string& tname, const ExprNode* whereCond) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_DELETE)) return false;
    auto flds = FieldManager::getInstance().getFields(tname);
    auto recs = readRecs(tname);
    std::vector<std::string> newRecs;
    int deleted = 0;
    std::set<int> candidateRows;
    bool usedIndex = false;
    IndexProbe probe;
    if (whereCond && tryBuildIndexProbe(tname, whereCond, probe)) {
        const auto indexedRows = IndexManager::getInstance().lookup(tname, probe.col, probe.value, probe.op);
        for (int row : indexedRows) {
            if (row >= 0 && row < static_cast<int>(recs.size())) candidateRows.insert(row);
        }
        usedIndex = true;
    }

    for (size_t i = 0; i < recs.size(); ++i) {
        const auto& r = recs[i];
        auto rowCols = split(r, '|');
        const bool shouldCheck = !usedIndex || candidateRows.count(static_cast<int>(i)) != 0;
        if (shouldCheck && (!whereCond || evaluateExpr(whereCond, flds, rowCols))) {
            deleted++;
        }
        else {
            newRecs.push_back(r);
        }
    }
    writeRecs(tname, newRecs);
    auto t = TableManager::getInstance().getTable(tname).value();
    t.record_count = static_cast<int32_t>(newRecs.size());
    t.mtime.init();
    TableManager::getInstance().updateTable(tname, t);
    std::cout << "OK: 删除 " << deleted << " 行\n\n";
    return true;
}

// ==================== 内部辅助：表达式求值 ====================
bool RecordManager::evaluateExpr(const ExprNode* node, const std::vector<FieldInfo>& fields,
    const std::vector<std::string>& rowCols) {
    if (!node) return true;
    switch (node->type) {
    case ExprNode::LEAF:
        return true;  // 不应出现
    case ExprNode::UNARY: {
        std::string lval, rval;
        DataType dt = DataType::VARCHAR;
        // 获取左值
        if (node->left && node->left->type == ExprNode::LEAF) {
            if (!node->left->colName.empty()) {
                // 列引用
                int idx = -1;
                for (size_t i = 0; i < fields.size(); i++) {
                    if (std::string(fields[i].name) == node->left->colName) {
                        idx = static_cast<int>(i);
                        dt = fields[i].type;
                        break;
                    }
                }
                if (idx >= 0 && idx < static_cast<int>(rowCols.size())) {
                    lval = rowCols[idx];
                }
            }
            else {
                lval = node->left->value;
            }
        }
        // 获取右值
        if (node->right && node->right->type == ExprNode::LEAF) {
            if (!node->right->colName.empty()) {
                int idx = -1;
                for (size_t i = 0; i < fields.size(); i++) {
                    if (std::string(fields[i].name) == node->right->colName) {
                        idx = static_cast<int>(i);
                        dt = fields[i].type;
                        break;
                    }
                }
                if (idx >= 0 && idx < static_cast<int>(rowCols.size())) {
                    rval = rowCols[idx];
                }
            }
            else {
                rval = node->right->value;
            }
        }
        return compareValues(lval, rval, dt, node->comp);
    }
    case ExprNode::BINARY: {
        bool l = evaluateExpr(node->left.get(), fields, rowCols);
        bool r = evaluateExpr(node->right.get(), fields, rowCols);
        if (node->logic == LogicOp::AND) return l && r;
        else return l || r;
    }
    }
    return true;
}

double RecordManager::getColumnValue(const std::string& val, DataType type) {
    if (val.empty()) return 0.0;
    if (type == DataType::INT || type == DataType::DOUBLE) {
        try {
            return std::stod(val);
        }
        catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

std::string RecordManager::getColumnString(const std::string& val) {
    return val;
}

// ==================== 事务相关方法 ====================

bool RecordManager::insertRecordTx(const std::string& tname, const std::vector<std::string>& values) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_INSERT)) return false;

    // 获取表级排他锁
    std::string tableLock = "table:" + tname;
    if (!TransactionManager::getInstance().acquireLock(tableLock, LockType::EXCLUSIVE)) {
        std::cout << "Err: 无法获取表锁，请重试\n";
        return false;
    }
    auto flds = FieldManager::getInstance().getFields(tname);
    if (values.size() != flds.size()) {
        std::cout << "Err: 值数量不匹配 (需要\n" << flds.size() << ")\n";
        return false;
    }

    // 类型校验 & NOT NULL
    for (size_t i = 0; i < flds.size(); i++) {
        std::string v = unquote(values[i]);
        if (v.empty() && (flds[i].flags & FIELD_FLAG_NOT_NULL)) {
            std::cout << "Err: 字段 " << flds[i].name << " 不能为空\n";
            return false;
        }
        if (!v.empty() && !validateValue(v, flds[i].type, flds[i].param)) {
            std::cout << "Err: 字段 " << flds[i].name << " 值类型不匹配\n";
            return false;
        }
    }

    std::string line;
    for (size_t i = 0; i < values.size(); i++) {
        std::string v = unquote(values[i]);
        line += v + (i == values.size() - 1 ? "" : "|");
    }

    auto recs = readRecs(tname);
    int newRowIndex = static_cast<int>(recs.size());
    recs.push_back(line);
    writeRecs(tname, recs);

    // 记录到事务日志
    std::vector<std::string> processedValues = values;
    for (auto& v : processedValues) {
        v = unquote(v);
    }
    TransactionManager::getInstance().logInsertToCurrent(tname, processedValues, newRowIndex);

    // 更新表记录数
    auto tableOpt = TableManager::getInstance().getTable(tname);
    if (tableOpt.has_value()) {
        TableInfo t = tableOpt.value();
        t.record_count = static_cast<int32_t>(recs.size());
        t.mtime.init();
        TableManager::getInstance().updateTable(tname, t);
    }

    std::cout << "OK: 插入成功 (事务 #" << TransactionManager::getInstance().getCurrentTxId() << ")\n";
    return true;
}

bool RecordManager::insertRecordTx(const std::string& tname,
    const std::vector<std::string>& cols,
    const std::vector<std::string>& values) {
    if (cols.empty()) return insertRecordTx(tname, values);

    auto flds = FieldManager::getInstance().getFields(tname);
    if (cols.size() != values.size() || cols.size() > flds.size()) {
        std::cout << "Err: 列/值数量不匹配\n";
        return false;
    }

    // 构建完整一行
    std::vector<std::string> row(flds.size(), "");
    for (size_t i = 0; i < cols.size(); i++) {
        bool found = false;
        for (size_t j = 0; j < flds.size(); j++) {
            if (std::string(flds[j].name) == cols[i]) {
                std::string v = unquote(values[i]);
                if (v.empty() && (flds[j].flags & FIELD_FLAG_NOT_NULL)) {
                    std::cout << "Err: 字段 " << cols[i] << " 不能为空\n";
                    return false;
                }
                if (!v.empty() && !validateValue(v, flds[j].type, flds[j].param)) {
                    std::cout << "Err: 字段 " << cols[i] << " 值类型不匹配\n";
                    return false;
                }
                row[j] = v;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cout << "Err: 列 " << cols[i] << " 不存在\n";
            return false;
        }
    }
    return insertRecordTx(tname, row);
}

bool RecordManager::updateRecordsTx(const std::string& tname,
    const std::string& setCol, const std::string& setVal,
    const ExprNode* whereCond) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_UPDATE)) return false;

    // 获取表级排他锁
    std::string tableLock = "table:" + tname;
    if (!TransactionManager::getInstance().acquireLock(tableLock, LockType::EXCLUSIVE)) {
        std::cout << "Err: 无法获取表锁，请重试\n";
        return false;
    }


    auto flds = FieldManager::getInstance().getFields(tname);
    int setIdx = -1;
    for (size_t i = 0; i < flds.size(); i++) {
        if (std::string(flds[i].name) == setCol) {
            setIdx = static_cast<int>(i);
            break;
        }
    }
    if (setIdx == -1) {
        std::cout << "Err: SET 列不存在\n";
        return false;
    }

    std::string val = unquote(setVal);
    if (!val.empty() && !validateValue(val, flds[setIdx].type, flds[setIdx].param)) {
        std::cout << "Err: 更新值类型不匹配\n";
        return false;
    }

    auto recs = readRecs(tname);
    int updated = 0;

    std::vector<size_t> candidateRows;
    IndexProbe probe;
    if (whereCond && tryBuildIndexProbe(tname, whereCond, probe)) {
        const auto indexedRows = IndexManager::getInstance().lookup(tname, probe.col, probe.value, probe.op);
        std::set<int> rowSet;
        for (int row : indexedRows) {
            if (row >= 0 && row < static_cast<int>(recs.size())) rowSet.insert(row);
        }
        for (int row : rowSet) candidateRows.push_back(static_cast<size_t>(row));
    }
    else {
        for (size_t i = 0; i < recs.size(); ++i) candidateRows.push_back(i);
    }

    for (size_t i : candidateRows) {
        auto rowCols = split(recs[i], '|');
        if (!whereCond || evaluateExpr(whereCond, flds, rowCols)) {
            // 获取行级排他锁
            std::string rowLock = "table:" + tname + ":row:" + std::to_string(i);
            if (!TransactionManager::getInstance().acquireLock(rowLock, LockType::EXCLUSIVE)) {
                std::cout << "Err: 无法获取行锁，请重试\n";
                return false;
            }

            // 记录旧值到事务日志
            TransactionManager::getInstance().logUpdateToCurrent(tname, rowCols, rowCols, static_cast<int>(i));

            // 更新值
            rowCols[setIdx] = val;
            std::string newLine;
            for (size_t j = 0; j < rowCols.size(); j++) {
                newLine += rowCols[j] + (j == rowCols.size() - 1 ? "" : "|");
            }
            recs[i] = newLine;
            updated++;
        }
    }

    writeRecs(tname, recs);

    auto t = TableManager::getInstance().getTable(tname).value();
    t.mtime.init();
    TableManager::getInstance().updateTable(tname, t);

    std::cout << "OK: 更新 " << updated << " 行 (事务 #"
        << TransactionManager::getInstance().getCurrentTxId() << ")\n";
    return true;
}

bool RecordManager::deleteRecordsTx(const std::string& tname, const ExprNode* whereCond) {
    if (!SecurityManager::getInstance().requirePrivilege(g_current_db, tname, PRIV_DELETE)) return false;

    // 获取表级排他锁
    std::string tableLock = "table:" + tname;
    if (!TransactionManager::getInstance().acquireLock(tableLock, LockType::EXCLUSIVE)) {
        std::cout << "Err: 无法获取表锁，请重试\n";
        return false;
    }
    auto flds = FieldManager::getInstance().getFields(tname);
    auto recs = readRecs(tname);

    std::vector<std::string> newRecs;
    std::vector<int> deletedIndices;
    std::set<int> candidateRows;
    bool usedIndex = false;
    IndexProbe probe;
    if (whereCond && tryBuildIndexProbe(tname, whereCond, probe)) {
        const auto indexedRows = IndexManager::getInstance().lookup(tname, probe.col, probe.value, probe.op);
        for (int row : indexedRows) {
            if (row >= 0 && row < static_cast<int>(recs.size())) candidateRows.insert(row);
        }
        usedIndex = true;
    }

    for (size_t i = 0; i < recs.size(); i++) {
        auto rowCols = split(recs[i], '|');
        const bool shouldCheck = !usedIndex || candidateRows.count(static_cast<int>(i)) != 0;
        if (shouldCheck && (!whereCond || evaluateExpr(whereCond, flds, rowCols))) {
            // 获取行级排他锁
            std::string rowLock = "table:" + tname + ":row:" + std::to_string(i);
            if (!TransactionManager::getInstance().acquireLock(rowLock, LockType::EXCLUSIVE)) {
                std::cout << "Err: 无法获取行锁，请重试\n";
                return false;
            }
            // 记录删除的行到事务日志
            TransactionManager::getInstance().logDeleteToCurrent(tname, rowCols, static_cast<int>(i));
            deletedIndices.push_back(static_cast<int>(i));
        }
        else {
            newRecs.push_back(recs[i]);
        }
    }

    writeRecs(tname, newRecs);

    auto t = TableManager::getInstance().getTable(tname).value();
    t.record_count = static_cast<int32_t>(newRecs.size());
    t.mtime.init();
    TableManager::getInstance().updateTable(tname, t);

    std::cout << "OK: 删除 " << deletedIndices.size() << "行 (事务 #"
        << TransactionManager::getInstance().getCurrentTxId() << ")\n";
    return true;
}
