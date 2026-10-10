//
// Created by Claude on 2026.
//

#include "CascStorageDialog.h"
#include "../../../../3rdparty/nlohmann/json.hpp"
#include "../../../../3rdparty/filesystem_impl/include/ghc/filesystem.hpp"
#include <fstream>
#include <iostream>
#include <utility>

using json = nlohmann::json;

CascStorageDialog::CascStorageDialog(const HApiContainer& api,
                                     StorageOpenCallback openCallback)
    : m_api(api), m_openCallback(openCallback)
{
    loadStorages();
}

std::string CascStorageDialog::getStoragesFilePath() const {
    return "./cascStorages.json";
}

void CascStorageDialog::loadStorages() {
    m_storages.clear();

    std::string filePath = getStoragesFilePath();
    if (!ghc::filesystem::exists(filePath)) {
        return;
    }

    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open cascStorages.json for reading" << std::endl;
            return;
        }

        json j;
        file >> j;

        if (j.contains("storages") && j["storages"].is_array()) {
            for (const auto& item : j["storages"]) {
                CascStorageEntry entry;
                from_json(item, entry);
                m_storages.push_back(entry);
            }
        }

        // Load last browsed folder
        if (j.contains("lastBrowsedFolder") && j["lastBrowsedFolder"].is_string()) {
            m_lastBrowsedFolder = j["lastBrowsedFolder"].get<std::string>();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading cascStorages.json: " << e.what() << std::endl;
    }
}

void CascStorageDialog::saveStorages() {
    try {
        json j;
        j["storages"] = json::array();

        for (const auto& entry : m_storages) {
            json entryJson;
            to_json(entryJson, entry);
            j["storages"].push_back(entryJson);
        }

        // Save last browsed folder
        j["lastBrowsedFolder"] = m_lastBrowsedFolder;

        std::ofstream file(getStoragesFilePath());
        if (!file.is_open()) {
            std::cerr << "Failed to open cascStorages.json for writing" << std::endl;
            return;
        }

        file << j.dump(2);  // Pretty print with 2-space indent
    } catch (const std::exception& e) {
        std::cerr << "Error saving cascStorages.json: " << e.what() << std::endl;
    }
}

void CascStorageDialog::addStorage(const CascStorageEntry& entry) {
    //Add new entry at the beginning (most recent first)
    m_storages.insert(m_storages.begin(), entry);
    saveStorages();
}

bool CascStorageDialog::openFirstStorage() {
    if (m_storages.empty()) return false;

    return tryOpenStorage(m_storages.front());
}

bool CascStorageDialog::tryOpenStorage(const CascStorageEntry& entry) {
    if (m_openCallback) {
        BuildDefinition buildDef = entry.buildDef;
        std::string path = entry.cascLocalFolder;
        return m_openCallback(path, buildDef);
    }
    return false;
}
