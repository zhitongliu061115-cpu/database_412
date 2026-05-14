#include "SecurityManager.h"
#include "FileManager.h"

#include <iomanip>

namespace {
constexpr char USERS_FILE[] = "users.db";
constexpr char PRIVILEGES_FILE[] = "privileges.db";
constexpr char DEFAULT_ADMIN_NAME[] = "admin";
constexpr char DEFAULT_ADMIN_PASSWORD[] = "admin123";
constexpr char PASSWORD_SALT[] = "database_412::security";

std::string makeUserPath() {
    return joinPath(g_root, USERS_FILE);
}

std::string makePrivilegePath() {
    return joinPath(g_root, PRIVILEGES_FILE);
}

std::string hashPassword(const std::string& password) {
    uint64_t hash = 1469598103934665603ull;
    std::string mixed = std::string(PASSWORD_SALT) + ":" + password;
    for (unsigned char ch : mixed) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

bool matchesTarget(const PrivilegeInfo& privilege, const std::string& dbName, const std::string& tableName) {
    const std::string storedDb = privilege.db_name;
    const std::string storedTable = privilege.table_name;
    const bool dbMatched = (storedDb == "*" || storedDb == dbName);
    const bool tableMatched = (storedTable == "*" || storedTable == tableName);
    return dbMatched && tableMatched;
}
}

SecurityManager::SecurityManager() : fileManager(&FileManager::getInstance()) {}

SecurityManager& SecurityManager::getInstance() {
    static SecurityManager instance;
    return instance;
}

void SecurityManager::initialize() {
    auto users = loadUsers();
    if (!users.empty()) return;

    createUser(DEFAULT_ADMIN_NAME, DEFAULT_ADMIN_PASSWORD, true);
    std::cout << "[安全] 已初始化默认管理员账号: " << DEFAULT_ADMIN_NAME
        << " / " << DEFAULT_ADMIN_PASSWORD << "\n";
}

bool SecurityManager::createUser(const std::string& username, const std::string& password, bool isAdmin) {
    if (username.empty() || username.length() >= MAX_NAME_LEN) {
        std::cout << "Err: 用户名无效\n";
        return false;
    }
    if (password.empty()) {
        std::cout << "Err: 密码不能为空\n";
        return false;
    }
    if (userExists(username)) {
        std::cout << "Err: 用户已存在\n";
        return false;
    }

    auto users = loadUsers();
    UserInfo user;
    safeStrncpy(user.username, username.c_str(), MAX_NAME_LEN);
    const std::string hash = hashPassword(password);
    safeStrncpy(user.password_hash, hash.c_str(), MAX_NAME_LEN);
    user.is_admin = isAdmin ? 1 : 0;
    users.push_back(user);
    saveUsers(users);

    std::cout << "OK: 用户 " << username << " 创建成功\n";
    return true;
}

bool SecurityManager::dropUser(const std::string& username) {
    if (username.empty()) {
        std::cout << "Err: 用户名无效\n";
        return false;
    }
    if (username == DEFAULT_ADMIN_NAME) {
        std::cout << "Err: 默认管理员不可删除\n";
        return false;
    }
    if (currentUser_ == username) {
        std::cout << "Err: 当前登录用户不可删除\n";
        return false;
    }

    auto users = loadUsers();
    auto it = std::remove_if(users.begin(), users.end(), [&](const UserInfo& user) {
        return std::string(user.username) == username;
        });
    if (it == users.end()) {
        std::cout << "Err: 用户不存在\n";
        return false;
    }
    users.erase(it, users.end());
    saveUsers(users);

    auto privileges = loadPrivileges();
    privileges.erase(std::remove_if(privileges.begin(), privileges.end(), [&](const PrivilegeInfo& privilege) {
        return std::string(privilege.username) == username;
        }), privileges.end());
    savePrivileges(privileges);

    std::cout << "OK: 用户 " << username << " 删除成功\n";
    return true;
}

bool SecurityManager::login(const std::string& username, const std::string& password) {
    auto userOpt = getUser(username);
    if (!userOpt.has_value()) {
        std::cout << "Err: 用户不存在\n";
        return false;
    }

    const std::string expectedHash = userOpt.value().password_hash;
    if (expectedHash != hashPassword(password)) {
        std::cout << "Err: 密码错误\n";
        return false;
    }

    currentUser_ = username;
    std::cout << "OK: 用户 " << username << " 登录成功\n";
    return true;
}

void SecurityManager::logout(bool silent) {
    if (currentUser_.empty()) {
        if (!silent) std::cout << "Err: 当前没有登录用户\n";
        return;
    }

    const std::string oldUser = currentUser_;
    currentUser_.clear();
    if (!silent) std::cout << "OK: 用户 " << oldUser << " 已退出登录\n";
}

bool SecurityManager::isLoggedIn() const {
    return !currentUser_.empty();
}

bool SecurityManager::isCurrentAdmin() const {
    if (currentUser_.empty()) return false;
    auto userOpt = getUser(currentUser_);
    return userOpt.has_value() && userOpt.value().is_admin != 0;
}

std::string SecurityManager::currentUser() const {
    return currentUser_;
}

bool SecurityManager::requireLogin() const {
    if (!isLoggedIn()) {
        std::cout << "Err: 请先登录\n";
        return false;
    }
    return true;
}

bool SecurityManager::requireAdmin() const {
    if (!requireLogin()) return false;
    if (!isCurrentAdmin()) {
        std::cout << "Err: 仅管理员可执行此操作\n";
        return false;
    }
    return true;
}

bool SecurityManager::requirePrivilege(const std::string& dbName, const std::string& tableName, uint32_t mask) const {
    if (!requireLogin()) return false;
    if (dbName.empty()) {
        std::cout << "Err: 请先 USE 数据库\n";
        return false;
    }
    if (isCurrentAdmin()) return true;
    if (!hasPrivilege(currentUser_, dbName, tableName, mask)) {
        std::cout << "Err: 用户 " << currentUser_ << " 缺少权限 "
            << privilegeMaskToString(mask) << " ON " << dbName << "." << tableName << "\n";
        return false;
    }
    return true;
}

bool SecurityManager::grantPrivilege(const std::string& username, const std::string& dbName,
    const std::string& tableName, uint32_t mask) {
    if (!userExists(username)) {
        std::cout << "Err: 用户不存在\n";
        return false;
    }
    if (dbName.empty() || tableName.empty()) {
        std::cout << "Err: 权限目标无效\n";
        return false;
    }
    if (mask == 0) {
        std::cout << "Err: 权限列表为空\n";
        return false;
    }

    auto privileges = loadPrivileges();
    bool found = false;
    for (auto& privilege : privileges) {
        if (std::string(privilege.username) == username &&
            std::string(privilege.db_name) == dbName &&
            std::string(privilege.table_name) == tableName) {
            privilege.mask |= mask;
            privilege.mtime.init();
            found = true;
            break;
        }
    }

    if (!found) {
        PrivilegeInfo privilege;
        safeStrncpy(privilege.username, username.c_str(), MAX_NAME_LEN);
        safeStrncpy(privilege.db_name, dbName.c_str(), MAX_NAME_LEN);
        safeStrncpy(privilege.table_name, tableName.c_str(), MAX_NAME_LEN);
        privilege.mask = mask;
        privileges.push_back(privilege);
    }

    savePrivileges(privileges);
    std::cout << "OK: 已授予用户 " << username << " 权限 "
        << privilegeMaskToString(mask) << " ON " << dbName << "." << tableName << "\n";
    return true;
}

bool SecurityManager::revokePrivilege(const std::string& username, const std::string& dbName,
    const std::string& tableName, uint32_t mask) {
    auto privileges = loadPrivileges();
    bool found = false;

    for (auto it = privileges.begin(); it != privileges.end(); ++it) {
        if (std::string(it->username) == username &&
            std::string(it->db_name) == dbName &&
            std::string(it->table_name) == tableName) {
            it->mask &= ~mask;
            it->mtime.init();
            if (it->mask == 0) {
                privileges.erase(it);
            }
            found = true;
            break;
        }
    }

    if (!found) {
        std::cout << "Err: 未找到对应的权限记录\n";
        return false;
    }

    savePrivileges(privileges);
    std::cout << "OK: 已收回用户 " << username << " 权限 "
        << privilegeMaskToString(mask) << " ON " << dbName << "." << tableName << "\n";
    return true;
}

bool SecurityManager::hasPrivilege(const std::string& username, const std::string& dbName,
    const std::string& tableName, uint32_t mask) const {
    auto userOpt = getUser(username);
    if (!userOpt.has_value()) return false;
    if (userOpt.value().is_admin != 0) return true;

    const auto privileges = loadPrivileges();
    uint32_t effectiveMask = 0;
    for (const auto& privilege : privileges) {
        if (std::string(privilege.username) != username) continue;
        if (!matchesTarget(privilege, dbName, tableName)) continue;
        effectiveMask |= privilege.mask;
    }
    return (effectiveMask & mask) == mask;
}

void SecurityManager::showUsers() const {
    if (!requireAdmin()) return;

    const auto users = loadUsers();
    std::cout << "\n--- Users ---\n";
    for (const auto& user : users) {
        std::cout << user.username << "\t" << (user.is_admin ? "ADMIN" : "NORMAL") << "\n";
    }
    std::cout << "共 " << users.size() << " 个用户\n";
}

void SecurityManager::showGrants(const std::string& username) const {
    if (!requireLogin()) return;
    if (!isCurrentAdmin() && currentUser_ != username) {
        std::cout << "Err: 仅管理员或用户本人可查看权限\n";
        return;
    }
    if (!userExists(username)) {
        std::cout << "Err: 用户不存在\n";
        return;
    }

    const auto privileges = loadPrivileges();
    std::cout << "\n--- Grants For " << username << " ---\n";
    bool found = false;
    for (const auto& privilege : privileges) {
        if (std::string(privilege.username) != username) continue;
        found = true;
        std::cout << privilege.db_name << "." << privilege.table_name
            << "\t" << privilegeMaskToString(privilege.mask) << "\n";
    }
    if (!found) std::cout << "(无权限记录)\n";
}

bool SecurityManager::privilegeFromName(const std::string& name, uint32_t& mask) {
    const std::string upper = toUpper(name);
    if (upper == "SELECT") mask = PRIV_SELECT;
    else if (upper == "INSERT") mask = PRIV_INSERT;
    else if (upper == "UPDATE") mask = PRIV_UPDATE;
    else if (upper == "DELETE") mask = PRIV_DELETE;
    else if (upper == "ALTER") mask = PRIV_ALTER;
    else if (upper == "DROP") mask = PRIV_DROP;
    else if (upper == "ALL") mask = PRIV_ALL;
    else return false;
    return true;
}

std::string SecurityManager::privilegeMaskToString(uint32_t mask) {
    if (mask == PRIV_ALL) return "ALL";

    std::vector<std::string> names;
    if (mask & PRIV_SELECT) names.push_back("SELECT");
    if (mask & PRIV_INSERT) names.push_back("INSERT");
    if (mask & PRIV_UPDATE) names.push_back("UPDATE");
    if (mask & PRIV_DELETE) names.push_back("DELETE");
    if (mask & PRIV_ALTER) names.push_back("ALTER");
    if (mask & PRIV_DROP) names.push_back("DROP");

    if (names.empty()) return "NONE";

    std::ostringstream oss;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) oss << ", ";
        oss << names[i];
    }
    return oss.str();
}

std::vector<UserInfo> SecurityManager::loadUsers() const {
    return fileManager->readAllStruct<UserInfo>(makeUserPath());
}

std::vector<PrivilegeInfo> SecurityManager::loadPrivileges() const {
    return fileManager->readAllStruct<PrivilegeInfo>(makePrivilegePath());
}

void SecurityManager::saveUsers(const std::vector<UserInfo>& users) const {
    fileManager->writeAllStruct(makeUserPath(), users);
}

void SecurityManager::savePrivileges(const std::vector<PrivilegeInfo>& privileges) const {
    fileManager->writeAllStruct(makePrivilegePath(), privileges);
}

bool SecurityManager::userExists(const std::string& username) const {
    return getUser(username).has_value();
}

Optional<UserInfo> SecurityManager::getUser(const std::string& username) const {
    const auto users = loadUsers();
    for (const auto& user : users) {
        if (std::string(user.username) == username) {
            return Optional<UserInfo>(user);
        }
    }
    return Optional<UserInfo>();
}
