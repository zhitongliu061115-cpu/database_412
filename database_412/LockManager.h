#ifndef LOCK_MANAGER_H
#define LOCK_MANAGER_H

#include "common.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>

// 锁类型
enum class LockType {
    SHARED,     // 共享锁（读锁）
    EXCLUSIVE   // 排他锁（写锁）
};

// 锁管理器（单例）
class LockManager {
public:
    static LockManager& getInstance();

    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;

    // 请求锁（阻塞直到获得锁或超时）
    bool acquireLock(int txId, const std::string& resource, LockType type, int timeoutMs = 15000);

    // 释放事务持有的所有锁
    void releaseAllLocks(int txId);

    // 打印当前锁状态（调试用）
    void printLocks();

private:
    LockManager();
    ~LockManager();

    // 资源锁信息
    struct ResourceLockInfo {
        LockType currentLock;           // 当前持有的锁类型
        int ownerTxId;                  // 排他锁持有者（-1表示无）
        std::vector<int> sharedOwners;  // 共享锁持有者列表
        std::vector<int> waitQueue;     // 等待队列（事务ID）

        ResourceLockInfo() : currentLock(LockType::SHARED), ownerTxId(-1) {}
    };

    // 检查锁兼容性（共享锁兼容共享锁，其他都不兼容）
    bool isCompatible(LockType held, LockType requested);

    std::unordered_map<std::string, ResourceLockInfo> locks_;
    std::unordered_map<int, std::unordered_set<std::string>> txLocks_;  // 事务 -> 持有的资源

    std::mutex mutex_;
    std::condition_variable cv_;
};

#endif
