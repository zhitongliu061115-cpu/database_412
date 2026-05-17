#include "LockManager.h"
#include <iostream>
#include <algorithm>

LockManager& LockManager::getInstance() {
    static LockManager instance;
    return instance;
}

LockManager::LockManager() {}

LockManager::~LockManager() {}

bool LockManager::isCompatible(LockType held, LockType requested) {
    // 共享锁和共享锁兼容
    if (held == LockType::SHARED && requested == LockType::SHARED) {
        return true;
    }
    // 其他情况都不兼容
    return false;
}

bool LockManager::acquireLock(int txId, const std::string& resource, LockType type, int timeoutMs) {
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);

        auto it = locks_.find(resource);
        // 清理可能的僵尸锁（有资源条目但无任何持有者）
        if (it != locks_.end()) {
            ResourceLockInfo& resLock = it->second;
            if (resLock.ownerTxId == -1 && resLock.sharedOwners.empty() && resLock.waitQueue.empty()) {
                locks_.erase(it);
                it = locks_.end();
            }
        }

        // 资源没有被锁定
        if (it == locks_.end()) {
            ResourceLockInfo info;
            info.currentLock = type;
            if (type == LockType::EXCLUSIVE) {
                info.ownerTxId = txId;
            }
            else {
                info.sharedOwners.push_back(txId);
            }
            locks_[resource] = info;
            txLocks_[txId].insert(resource);
            return true;
        }

        ResourceLockInfo& resLock = it->second;

        // 检查当前事务是否已经持有锁
        if (resLock.ownerTxId == txId) {
            return true;
        }
        auto itShared = std::find(resLock.sharedOwners.begin(), resLock.sharedOwners.end(), txId);
        if (itShared != resLock.sharedOwners.end()) {
            return true;
        }

        // 检查兼容性
        bool compatible = isCompatible(resLock.currentLock, type);

        if (compatible) {
            if (type == LockType::SHARED) {
                resLock.sharedOwners.push_back(txId);
                txLocks_[txId].insert(resource);
            }
            return true;
        }

        // 不兼容，加入等待队列（如果尚未在队列中）
        auto itWait = std::find(resLock.waitQueue.begin(), resLock.waitQueue.end(), txId);
        if (itWait == resLock.waitQueue.end()) {
            resLock.waitQueue.push_back(txId);
        }

        // 等待被唤醒或超时
        lock.unlock();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime);
        if (elapsed.count() >= timeoutMs) {
            // 超时，从等待队列中移除
            std::lock_guard<std::mutex> lock2(mutex_);
            auto it2 = locks_.find(resource);
            if (it2 != locks_.end()) {
                auto& queue = it2->second.waitQueue;
                queue.erase(std::remove(queue.begin(), queue.end(), txId), queue.end());
            }
            // 可选：输出超时信息（避免干扰测试，可注释）
            // std::cout << "[锁] 事务 #" << txId << " 获取锁超时: " << resource << "\n";
            return false;
        }

        // 等待100ms后重试
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void LockManager::releaseAllLocks(int txId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto txIt = txLocks_.find(txId);
    if (txIt == txLocks_.end()) return;

    for (const auto& resource : txIt->second) {
        auto it = locks_.find(resource);
        if (it == locks_.end()) continue;

        ResourceLockInfo& resLock = it->second;

        // 释放排他锁
        if (resLock.ownerTxId == txId) {
            resLock.ownerTxId = -1;
            // 注意：这里不改变 currentLock，因为可能还有其他共享锁存在
            // 后面会通过 sharedOwners 判断是否还有锁
        }

        // 从共享锁持有者列表中移除
        resLock.sharedOwners.erase(
            std::remove(resLock.sharedOwners.begin(), resLock.sharedOwners.end(), txId),
            resLock.sharedOwners.end());

        // 从等待队列中移除
        resLock.waitQueue.erase(
            std::remove(resLock.waitQueue.begin(), resLock.waitQueue.end(), txId),
            resLock.waitQueue.end());

        // 如果没有任何持有者且无等待者，删除该资源
        if (resLock.ownerTxId == -1 && resLock.sharedOwners.empty() && resLock.waitQueue.empty()) {
            locks_.erase(it);
        }
        else if (resLock.ownerTxId == -1 && !resLock.sharedOwners.empty()) {
            // 如果排他锁释放后仍有共享锁，将当前锁类型纠正为共享锁
            resLock.currentLock = LockType::SHARED;
        }
    }

    txLocks_.erase(txIt);
}

void LockManager::printLocks() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "\n========== |当前锁状态| ==========\n";
    for (const auto& pair : locks_) {
        std::cout << "资源: " << pair.first << "\n";
        std::cout << "  锁类型|: " << (pair.second.currentLock == LockType::SHARED ? "|共享锁|" : "|排他锁|") << "\n";
        if (pair.second.ownerTxId != -1) {
            std::cout << "  排他持有者|: 事务 #" << pair.second.ownerTxId << "\n";
        }
        if (!pair.second.sharedOwners.empty()) {
            std::cout << "  共享持有者|: ";
            for (int txId : pair.second.sharedOwners) {
                std::cout << "#" << txId << " ";
            }
            std::cout << "\n";
        }
        if (!pair.second.waitQueue.empty()) {
            std::cout << "  等待队列|: ";
            for (int txId : pair.second.waitQueue) {
                std::cout << "#" << txId << " ";
            }
            std::cout << "\n";
        }
    }
    if (locks_.empty()) {
        std::cout << "无锁\n";
    }
    std::cout << "================================\n";
}
