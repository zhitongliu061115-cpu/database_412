#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "common.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// 事务状态
enum class TransactionState {
    ACTIVE,     // 活跃状态
    COMMITTED,  // 已提交
    ABORTED,    // 已中止
};

// 操作类型
enum class OpType {
    INSERT,
    UPDATE,
    DELETE,
    SELECT
};

// 操作记录
struct Operation {
    OpType type;
    std::string tableName;
    std::vector<std::string> oldValues;  // 更新前的值（用于回滚）
    std::vector<std::string> newValues;  // 更新后的值
    int rowIndex;                         // 操作的行索引（-1表示插入，需要新行）

    Operation() : rowIndex(-1) {}
};

// 事务类
class Transaction {
public:
    Transaction(int id);
    ~Transaction();

    int getId() const { return id_; }
    TransactionState getState() const { return state_; }

    void begin();
    void commit();
    void rollback();

    // 记录操作
    void logInsert(const std::string& tableName, const std::vector<std::string>& values, int rowIndex);
    void logUpdate(const std::string& tableName, const std::vector<std::string>& oldVals,
        const std::vector<std::string>& newVals, int rowIndex);
    void logDelete(const std::string& tableName, const std::vector<std::string>& oldVals, int rowIndex);

    // 获取操作历史
    const std::vector<Operation>& getOperations() const { return operations_; }

    // 清除操作历史
    void clearOperations();

    // 锁相关
    void addLock(const std::string& resource);
    bool hasLock(const std::string& resource) const;
    void clearLocks();
    const std::unordered_set<std::string>& getLocks() const { return locks_; }

private:
    int id_;
    TransactionState state_;
    std::vector<Operation> operations_;
    std::unordered_set<std::string> locks_;  // 持有的锁资源
};

// 事务管理器
class TransactionManager {
public:
    static TransactionManager& getInstance();

    TransactionManager(const TransactionManager&) = delete;
    TransactionManager& operator=(const TransactionManager&) = delete;

    // 开始新事务
    int beginTransaction();

    // 提交事务
    bool commitTransaction(int txId);

    // 回滚事务
    bool rollbackTransaction(int txId);

    // 获取当前活动事务
    Transaction* getCurrentTransaction();

    // 设置当前事务
    void setCurrentTransaction(int txId);

    // 检查是否有活动事务
    bool hasActiveTransaction() const;

    // 获取当前事务ID
    int getCurrentTxId() const;

    // 记录操作到当前事务
    bool logInsertToCurrent(const std::string& tableName, const std::vector<std::string>& values, int rowIndex);
    bool logUpdateToCurrent(const std::string& tableName, const std::vector<std::string>& oldVals,
        const std::vector<std::string>& newVals, int rowIndex);
    bool logDeleteToCurrent(const std::string& tableName, const std::vector<std::string>& oldVals, int rowIndex);

    // 执行回滚（根据事务记录）
    bool executeRollback(Transaction* tx);

    // 获取所有活动事务
    std::vector<Transaction*> getActiveTransactions();

    // 锁相关
    bool acquireLock(const std::string& resource, LockType type, int timeoutMs = 5000);
    void releaseAllLocks(int txId);

private:
    TransactionManager() : nextTxId_(1) {}
    ~TransactionManager();

    std::unordered_map<int, std::unique_ptr<Transaction>> transactions_;
    thread_local static int currentTxId_;   // 线程局部存储，每个线程独立
    int nextTxId_;
};

#endif // TRANSACTION_H
