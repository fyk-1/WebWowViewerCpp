//
// Created by Claude on 2026.
//

#ifndef AWEBWOWVIEWERCPP_CASCSTORAGEDIALOG_H
#define AWEBWOWVIEWERCPP_CASCSTORAGEDIALOG_H

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "../../../../wowViewerLib/src/engine/ApiContainer.h"
#include "../../../database/buildInfoParser/buildDefinition.h"
#include "CascStorageEntry.h"

// Callback type for when a storage is selected/opened
using StorageOpenCallback = std::function<bool(const std::string& cascPath, BuildDefinition& buildDef)>;

class CascStorageDialog {
public:
    CascStorageDialog(const HApiContainer& api,
                      StorageOpenCallback openCallback);
    ~CascStorageDialog() = default;

    // Load/save storage list from/to JSON file
    void loadStorages();
    void saveStorages();

    // Add a storage entry to the list
    void addStorage(const CascStorageEntry& entry);

    // Check if there are any saved storages
    bool hasSavedStorages() const { return !m_storages.empty(); }

    // Opens the first saved storage, as a double-click on it would. False when there is none or it fails.
    bool openFirstStorage();

private:
    HApiContainer m_api;
    StorageOpenCallback m_openCallback;

    std::vector<CascStorageEntry> m_storages;
    std::string m_lastBrowsedFolder;

    // Get JSON file path
    std::string getStoragesFilePath() const;

    // Try to open a storage
    bool tryOpenStorage(const CascStorageEntry& entry);
};

#endif //AWEBWOWVIEWERCPP_CASCSTORAGEDIALOG_H
