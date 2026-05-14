#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include "common.h"

constexpr uint32_t PRIV_SELECT = 1u << 0;
constexpr uint32_t PRIV_INSERT = 1u << 1;
constexpr uint32_t PRIV_UPDATE = 1u << 2;
constexpr uint32_t PRIV_DELETE = 1u << 3;
constexpr uint32_t PRIV_ALTER = 1u << 4;
constexpr uint32_t PRIV_DROP = 1u << 5;
constexpr uint32_t PRIV_ALL = PRIV_SELECT | PRIV_INSERT | PRIV_UPDATE | PRIV_DELETE | PRIV_ALTER | PRIV_DROP;

#pragma pack(push, 1)
struct UserInfo {
    char username[MAX_NAME_LEN];
    char password_hash[MAX_NAME_LEN];
    uint8_t is_admin;
    DateTime ctime;

    UserInfo() {
        memset(this, 0, sizeof(UserInfo));
        is_admin = 0;
        ctime.init();
    }
};

struct PrivilegeInfo {
    char username[MAX_NAME_LEN];
    char db_name[MAX_NAME_LEN];
    char table_name[MAX_NAME_LEN];
    uint32_t mask;
    DateTime mtime;

    PrivilegeInfo() {
        memset(this, 0, sizeof(PrivilegeInfo));
        mask = 0;
        mtime.init();
    }
};
#pragma pack(pop)

class FileManager;

class SecurityManager {
public:
    static SecurityManager& getInstance();

    SecurityManager(const SecurityManager&) = delete;
    SecurityManager& operator=(const SecurityManager&) = delete;

    void initialize();

    bool createUser(const std::string& username, const std::string& password, bool isAdmin = false);
    bool dropUser(const std::string& username);
    bool login(const std::string& username, const std::string& password);
    void logout(bool silent = false);

    bool isLoggedIn() const;
    bool isCurrentAdmin() const;
    std::string currentUser() const;

    bool requireLogin() const;
    bool requireAdmin() const;
    bool requirePrivilege(const std::string& dbName, const std::string& tableName, uint32_t mask) const;

    bool grantPrivilege(const std::string& username, const std::string& dbName,
        const std::string& tableName, uint32_t mask);
    bool revokePrivilege(const std::string& username, const std::string& dbName,
        const std::string& tableName, uint32_t mask);

    bool hasPrivilege(const std::string& username, const std::string& dbName,
        const std::string& tableName, uint32_t mask) const;

    void showUsers() const;
    void showGrants(const std::string& username) const;

    static bool privilegeFromName(const std::string& name, uint32_t& mask);
    static std::string privilegeMaskToString(uint32_t mask);

private:
    SecurityManager();
    ~SecurityManager() = default;

    std::vector<UserInfo> loadUsers() const;
    std::vector<PrivilegeInfo> loadPrivileges() const;
    void saveUsers(const std::vector<UserInfo>& users) const;
    void savePrivileges(const std::vector<PrivilegeInfo>& privileges) const;

    bool userExists(const std::string& username) const;
    Optional<UserInfo> getUser(const std::string& username) const;

    FileManager* fileManager;
    std::string currentUser_;
};

#endif
