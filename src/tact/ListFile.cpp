// Replaces TactCpp's ListFile.cpp, which also downloaded the list from a URL. This one reads the
// local file only. ListFile.h is TactCpp's.
#include "ListFile.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

ListFile::ListFile(const Settings& settings) {
    if (!settings.loadListFile) {
        return;
    }

    if (settings.ListfilePath.has_value()) {
        if (LoadFromLocalFile(settings.ListfilePath.value())) {
            m_loaded = true;
        }
    }
}

bool ListFile::LoadFromLocalFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        std::cout << "ListFile: Local file does not exist: " << path << std::endl;
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "ListFile: Cannot open local file: " << path << std::endl;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    ParseListFile(content);
    std::cout << "ListFile: Loaded " << m_fileMap.size() << " entries from local file: " << path << std::endl;
    return true;
}

void ListFile::ParseListFile(const std::string& content) {
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        size_t semicolonPos = line.find(';');
        if (semicolonPos == std::string::npos) {
            continue;
        }

        std::string numberStr = line.substr(0, semicolonPos);
        std::string filename = line.substr(semicolonPos + 1);

        try {
            uint32_t fileDataID = std::stoul(numberStr);
            if (!filename.empty()) {
                m_fileMap[fileDataID] = filename;
            }
        } catch (const std::exception&) {
            continue;
        }
    }
}

std::string ListFile::GetFilename(uint32_t fileDataID) const {
    auto it = m_fileMap.find(fileDataID);
    if (it != m_fileMap.end()) {
        return it->second;
    }
    return "";
}
