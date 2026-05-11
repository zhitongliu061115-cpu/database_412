#ifndef FIELD_MANAGER_H
#define FIELD_MANAGER_H
#include "common.h"

class FileManager;
class FieldManager {
public:
    static FieldManager& getInstance();
    FieldManager(const FieldManager&) = delete;
    FieldManager& operator=(const FieldManager&) = delete;

    bool addField(const std::string& tname, const std::string& fname,
        const std::string& type_str, bool notNull = false, bool primaryKey = false);
    bool dropField(const std::string& tname, const std::string& fname);
    bool modifyField(const std::string& tname, const std::string& old_name, const std::string& new_name);
    std::vector<FieldInfo> getFields(const std::string& tname);

private:
    FileManager* fileManager;
    FieldManager();
    void saveFields(const std::string& tname, const std::vector<FieldInfo>& flds);
};

#endif
