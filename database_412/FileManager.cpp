#include "FileManager.h"

FileManager& FileManager::getInstance() {
    static FileManager instance;
    return instance;
}