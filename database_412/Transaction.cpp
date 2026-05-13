#include "Transaction.h"
#include "RecordManager.h"
#include "FieldManager.h"
#include "TableManager.h"
#include "FileManager.h"
#include <iostream>

// ==================== Transaction 实现 ====================

Transaction::Transaction(int id) : id_(id), state_(TransactionState::ACTIVE) {}

Transaction::~Transaction() {}

void Transaction::begin() {
    state_ = TransactionState::ACTIVE;
    operations_.clear();
}

void Transaction::commit() {
    if (state_ == TransactionState::ACTIVE) {
        state_ = TransactionState::COMMITTED;
    }
}

void Transaction::rollback() {
    if (state_ == TransactionState::ACTIVE) {
        state_ = TransactionState::ABORTED;
    }
}

void Transaction::logInsert(const std::string& tableName, const std::vector<std::string>& values, int rowIndex) {
    Operation op;
    op.type = OpType::INSERT;
    op.tableName = tableName;
    op.newValues = values;
    op.rowIndex = rowIndex;
    operations_.push_back(op);
}

void Transaction::logUpdate(const std::string& tableName, const std::vector<std::string>& oldVals,
    const std::vector<std::string>& newVals, int rowIndex) {
    Operation op;
    op.type = OpType::UPDATE;
    op.tableName = tableName;
    op.oldValues = oldVals;
    op.newValues = newVals;
    op.rowIndex = rowIndex;
    operations_.push_back(op);
}

void Transaction::logDelete(const std::string& tableName, const std::vector<std::string>& oldVals, int rowIndex) {
    Operation op;
    op.type = OpType::DELETE;
    op.tableName = tableName;
    op.oldValues = oldVals;
    op.rowIndex = rowIndex;
    operations_.push_back(op);
}

void Transaction::clearOperations() {
    operations_.clear();
}

// ==================== TransactionManager 实现 ====================

TransactionManager& TransactionManager::getInstance() {
    static TransactionManager instance;
    return instance;
}

TransactionManager::~TransactionManager() {}

int TransactionManager::beginTransaction() {
    int txId = nextTxId_++;
    auto tx = std::make_unique<Transaction>(txId);
    tx->begin();
    transactions_[txId] = std::move(tx);
    currentTxId_ = txId;
    std::cout << "[事务] 开始事务\n#" << txId << "\n";
    return txId;
}

bool TransactionManager::commitTransaction(int txId) {
    auto it = transactions_.find(txId);
    if (it == transactions_.end()) {
        std::cout << "[事务] 错误: 事务 #" << txId << " 不存在\n";
        return false;
    }

    Transaction* tx = it->second.get();
    if (tx->getState() != TransactionState::ACTIVE) {
        std::cout << "[事务] 错误: 事务 #" << txId << " 不是活跃状态\n";
        return false;
    }

    tx->commit();

    // 清除事务记录（已提交，不再需要回滚信息）
    tx->clearOperations();

    if (currentTxId_ == txId) {
        currentTxId_ = -1;
    }

    std::cout << "[事务] 事务 #" << txId << " 提交成功\n";
    return true;
}

bool TransactionManager::rollbackTransaction(int txId) {
    auto it = transactions_.find(txId);
    if (it == transactions_.end()) {
        std::cout << "[事务] 错误: 事务 #" << txId << " 不存在\n";
        return false;
    }

    Transaction* tx = it->second.get();
    if (tx->getState() != TransactionState::ACTIVE) {
        std::cout << "[事务] 错误: 事务 #" << txId << " 不是活跃状态\n";
        return false;
    }

    // 执行回滚
    bool success = executeRollback(tx);

    if (success) {
        tx->rollback();
        tx->clearOperations();

        if (currentTxId_ == txId) {
            currentTxId_ = -1;
        }

        std::cout << "[事务] 事务 #" << txId << " 回滚成功\n";
    }
    else {
        std::cout << "[事务] 错误: 事务 #" << txId << " 回滚失败\n";
    }

    return success;
}

Transaction* TransactionManager::getCurrentTransaction() {
    if (currentTxId_ == -1) return nullptr;
    auto it = transactions_.find(currentTxId_);
    if (it == transactions_.end()) return nullptr;
    return it->second.get();
}

void TransactionManager::setCurrentTransaction(int txId) {
    if (transactions_.find(txId) != transactions_.end()) {
        currentTxId_ = txId;
    }
}

bool TransactionManager::logInsertToCurrent(const std::string& tableName,
    const std::vector<std::string>& values,
    int rowIndex) {
    Transaction* tx = getCurrentTransaction();
    if (!tx) return false;
    tx->logInsert(tableName, values, rowIndex);
    return true;
}

bool TransactionManager::logUpdateToCurrent(const std::string& tableName,
    const std::vector<std::string>& oldVals,
    const std::vector<std::string>& newVals,
    int rowIndex) {
    Transaction* tx = getCurrentTransaction();
    if (!tx) return false;
    tx->logUpdate(tableName, oldVals, newVals, rowIndex);
    return true;
}

bool TransactionManager::logDeleteToCurrent(const std::string& tableName,
    const std::vector<std::string>& oldVals,
    int rowIndex) {
    Transaction* tx = getCurrentTransaction();
    if (!tx) return false;
    tx->logDelete(tableName, oldVals, rowIndex);
    return true;
}

bool TransactionManager::executeRollback(Transaction* tx) {
    const auto& ops = tx->getOperations();

    // 从后往前回滚操作
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        const Operation& op = *it;

        switch (op.type) {
        case OpType::INSERT: {
            // 回滚插入：删除插入的行
            auto recs = RecordManager::getInstance().readRecs(op.tableName);
            if (op.rowIndex >= 0 && op.rowIndex < static_cast<int>(recs.size())) {
                recs.erase(recs.begin() + op.rowIndex);
                RecordManager::getInstance().writeRecs(op.tableName, recs);
                std::cout << "[回滚] 撤销 INSERT 到\ " << op.tableName << " 行\ " << op.rowIndex << "\n";
            }
            break;
        }
        case OpType::UPDATE: {
            // 回滚更新：恢复旧值
            auto recs = RecordManager::getInstance().readRecs(op.tableName);
            if (op.rowIndex >= 0 && op.rowIndex < static_cast<int>(recs.size())) {
                std::string newLine;
                for (size_t i = 0; i < op.oldValues.size(); i++) {
                    newLine += op.oldValues[i] + (i == op.oldValues.size() - 1 ? "" : "|");
                }
                recs[op.rowIndex] = newLine;
                RecordManager::getInstance().writeRecs(op.tableName, recs);
                std::cout << "[回滚] 撤销 UPDATE 到\  " << op.tableName << " 行\ " << op.rowIndex << "\n";
            }
            break;
        }
        case OpType::DELETE: {
            // 回滚删除：重新插入
            auto recs = RecordManager::getInstance().readRecs(op.tableName);
            std::string newLine;
            for (size_t i = 0; i < op.oldValues.size(); i++) {
                newLine += op.oldValues[i] + (i == op.oldValues.size() - 1 ? "" : "|");
            }
            if (op.rowIndex >= 0 && op.rowIndex <= static_cast<int>(recs.size())) {
                recs.insert(recs.begin() + op.rowIndex, newLine);
                RecordManager::getInstance().writeRecs(op.tableName, recs);
                std::cout << "[回滚] 撤销 DELETE 到\ " << op.tableName << " 行\ " << op.rowIndex << "\n";
            }
            break;
        }
        default:
            break;
        }
    }

    return true;
}

std::vector<Transaction*> TransactionManager::getActiveTransactions() {
    std::vector<Transaction*> active;
    for (auto& pair : transactions_) {
        if (pair.second->getState() == TransactionState::ACTIVE) {
            active.push_back(pair.second.get());
        }
    }
    return active;
}
