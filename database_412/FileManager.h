#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "common.h"
#include <fstream>
#include <vector>

class FileManager {
public:
    static FileManager& getInstance();

    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;

    // 文件存在检查
    static bool fileExists(const std::string& path) {
        struct stat st;
        return stat(path.c_str(), &st) == 0;
    }

    // 写入单个结构体
    template<typename T>
    void writeStruct(const std::string& path, const T& data, bool append = false) {
        std::ios::openmode mode = std::ios::binary | (append ? std::ios::app : std::ios::out);
        std::ofstream ofs(path, mode);
        if (!ofs.is_open()) return;
        ofs.write(reinterpret_cast<const char*>(&data), sizeof(T));
    }

    // 读取所有结构体
    template<typename T>
    std::vector<T> readAllStruct(const std::string& path) {
        std::vector<T> res;
        if (!fileExists(path)) return res;
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) return res;
        T item;
        while (ifs.read(reinterpret_cast<char*>(&item), sizeof(T))) {
            res.push_back(item);
        }
        return res;
    }

    // 覆盖写入所有结构体
    template<typename T>
    void writeAllStruct(const std::string& path, const std::vector<T>& data) {
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return;
        for (const auto& item : data) {
            ofs.write(reinterpret_cast<const char*>(&item), sizeof(T));
        }
    }

private:
    FileManager() = default;
    ~FileManager() = default;
};

#endif // FILE_MANAGER_H