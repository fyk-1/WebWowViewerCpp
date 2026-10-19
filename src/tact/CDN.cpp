// Replaces TactCpp's CDN.cpp. The viewer reads the user's own install, so this keeps the local
// paths (CASC indices, the cache, a local TACT folder) and has no HTTP client. CDN.h is TactCpp's.
#include "CDN.h"
#include "Error.h"
#include "ReadTiming.h"
#include "utils/KeyService.h"
#include "utils/SalsaDecrypt.h"
#include "utils/stringUtils.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <mutex>

using namespace TACTLibUtils;

namespace {

bool readRawFile(const std::string& path, std::vector<uint8_t>& buffer) {
    if (!std::filesystem::exists(path)) {
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Unable to open file: " + path);
    }

    std::streamsize size = file.tellg();
    if (size < 0) throw std::runtime_error("Unable to determine file size: " + path);
    buffer.resize(size);

    file.seekg(0, std::ios::beg);
    if (!file.read((char*)(buffer.data()), size)) {
        throw std::runtime_error("Unable to read file: " + path);
    }

    return true;
}

std::string PadLeft(const std::string& input, std::size_t totalLength, char symbol) {
    if (input.size() >= totalLength) {
        return input;
    }
    return std::string(totalLength - input.size(), symbol) + input;
}

}

CDN::CDN(const Settings &settings) : settings_(settings) {

    auto fileCache  = std::make_unique<FileCache>(settings_.CacheDir);
    fileCache_ = std::move(fileCache);

    armadilloKey_ = settings.ArmadilloKey;
}

CDN::~CDN() {
}

void CDN::OpenLocal() {
    if (!settings_.BaseDir.has_value())
        return;

    try {
        auto start = std::chrono::steady_clock::now();
        LoadCASCIndices();
        hasLocalCasc = true;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "Loaded local CASC indices in " << elapsed << "ms" << std::endl << std::flush;
    } catch (const std::exception &e) {
        hasLocalCasc = false;
        throw std::runtime_error(std::string("Failed to load CASC indices: ") + e.what());
    }
}

void CDN::SetCDNs(const std::vector<std::string> &cdns) {
    std::lock_guard<std::mutex> lock(cdnSettingMutex_);
    for (auto &url: cdns) {
        if (std::find(cdnServers_.begin(), cdnServers_.end(), url) == cdnServers_.end())
            cdnServers_.push_back(url);
    }
}

std::string CDN::GetPatchServiceFile(const std::string &, const std::string &) {
    return "";
}

std::vector<uint8_t> CDN::GetFile(const std::string &type,
                                  const std::string &hash,
                                  uint64_t compressedSize,
                                  uint64_t decompressedSize,
                                  bool decode) {

    auto data = DownloadFile(type, hash, "", 0, compressedSize);
    if (!decode)
        return data;
    const auto start = std::chrono::steady_clock::now();
    auto decoded = BLTE::Decode(data, decompressedSize);
    tactReadTiming.inflate += std::chrono::steady_clock::now() - start;
    return decoded;
}

std::vector<uint8_t> CDN::GetFileFromArchive(const std::string &eKey,
                                             const std::string &archive,
                                             size_t offset,
                                             size_t length,
                                             uint64_t decompressedSize,
                                             bool decode) {
    auto data = DownloadFile("", eKey, archive, offset, length);
    if (!decode)
        return data;
    const auto start = std::chrono::steady_clock::now();
    auto decoded = BLTE::Decode(data, decompressedSize);
    tactReadTiming.inflate += std::chrono::steady_clock::now() - start;
    return decoded;
}

std::string CDN::GetFilePath(const std::string &type, const std::string &hash, uint64_t compressedSize) {

    std::filesystem::path cache = settings_.CacheDir / productDirectory_ / type / hash;

    if (std::filesystem::exists(cache)) {
        if (compressedSize > 0 && std::filesystem::file_size(cache) != compressedSize)
            std::filesystem::remove(cache);
        else
            return cache.string();
    }

    auto data = DownloadFile(type, hash, "", 0, compressedSize);
    std::filesystem::create_directories(cache.parent_path());
    std::ofstream ofs(cache, std::ios::binary);
    if (!ofs.write(reinterpret_cast<const char *>(data.data()), data.size()) || !ofs.flush())
        throw std::runtime_error("Unable to write cache file: " + cache.string());
    return cache.string();
}

std::string CDN::GetDecodedFilePath(const std::string &type,
                                    const std::string &hash,
                                    uint64_t compressedSize,
                                    uint64_t decompressedSize) {
    std::filesystem::path path = settings_.CacheDir / productDirectory_ / type / (hash + ".decoded");
    if (std::filesystem::exists(path))
        return path.string();

    auto data = DownloadFile(type, hash, "", 0, compressedSize);
    auto decoded = BLTE::Decode(data, decompressedSize);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.write(reinterpret_cast<const char *>(decoded.data()), decoded.size()) || !ofs.flush())
        throw std::runtime_error("Unable to write decoded cache file: " + path.string());
    return path.string();
}

void CDN::LoadCASCIndices() {
    if (!settings_.BaseDir.has_value()) return;

    std::filesystem::path dataDir = settings_.BaseDir.value();
    dataDir /= "Data/data";

    if (!std::filesystem::exists(dataDir))
        throw std::runtime_error("CASC data folder not found: " + dataDir.string());

    for (auto &entry: std::filesystem::directory_iterator(dataDir)) {
        if (entry.path().extension() != ".idx") continue;

        auto name = entry.path().stem().string();
        if (name.rfind("tempfile", 0) == 0) continue;

        uint8_t bucket = std::stoul(name.substr(0, 2), nullptr, 16);
        cascIndices_.emplace(bucket, std::make_unique<CASCIndexInstance>(entry.path().string()));
    }
}

std::vector<uint8_t> CDN::DownloadFile(
    const std::string& type,
    const std::string& key,
    const std::string& archive,
    int offset,
    uint64_t expectedSize,
    int)
{
    std::string localFailure;
    auto localFailureCategory = TACT::ErrorCategory::FileRead;

    // 1) The install's own CASC archives
    if (hasLocalCasc) {
        try {
            std::vector<uint8_t> data;

            std::filesystem::path p;

            if (archive.empty()) {
                if (type == "data" && key.rfind(".index") == key.size() - 6) {
                    p = std::filesystem::path(settings_.BaseDir.value_or("")) / "Data" / "indices" / key;
                } else if (type == "config" && key.size() >= 4) {
                    p = std::filesystem::path(settings_.BaseDir.value_or("")) / "Data" / "config" /
                        key.substr(0,2) / key.substr(2,2) / key;
                }
            }

            if (!p.empty() && settings_.BaseDir.has_value() && readRawFile(p.string(), data)) {
                return data;
            } else if (TryGetLocalFile(key, data)) {
                return data;
            }
        } catch (const std::exception& e) {
            localFailure = e.what();
            if (auto typed = dynamic_cast<const TACT::Exception*>(&e))
                localFailureCategory = typed->category;
        }
    }

    // 2) The cache
    std::vector<uint8_t> cachedData = fileCache_->TryLoadFromCache(key, type, archive, productDirectory_, expectedSize);
    if (!cachedData.empty()) {
        return cachedData;
    }

    // 3) A local TACT folder
    if (settings_.useTactLocal)
        return DownloadFileFromLocal(type, key, archive, offset, expectedSize);

    if (!localFailure.empty()) throw TACT::Exception(localFailureCategory, localFailure);
    throw TACT::Exception(TACT::ErrorCategory::FileNotFound, "File/archive " + type + " " + key + " is unavailable locally");
}

std::vector<uint8_t> CDN::DownloadFileFromLocal(
    const std::string& type,
    const std::string& key,
    const std::string& archive,
    int offset,
    uint64_t expectedSize)
{
    std::string fileType = archive.empty() ? type : "data";

    std::string seg1 = archive.empty() ? key.substr(0,2) : archive.substr(0,2);
    std::string seg2 = archive.empty() ? key.substr(2,2) : archive.substr(2,2);
    std::string resource = archive.empty() ? key : archive;

    // Build local path: {productDirectory_}/{fileType}/{seg1}/{seg2}/{resource}
    std::filesystem::path localPath = std::filesystem::path(*settings_.LocalTactPath) / productDirectory_/ fileType / seg1 / seg2 / resource;

    if (!std::filesystem::exists(localPath)) {
        throw TACT::Exception(TACT::ErrorCategory::FileNotFound,
            archive.empty()
            ? "File not found locally: " + localPath.string()
            : "Archive not found locally: " + localPath.string());
    }

    std::ifstream file(localPath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open local file: " + localPath.string());
    }

    size_t readSize = expectedSize;
    if (archive.empty()) {
        if (readSize == 0) {
            file.seekg(0, std::ios::end);
            readSize = file.tellg();
            file.seekg(0, std::ios::beg);
        }
    } else {
        file.seekg(offset, std::ios::beg);
        if (readSize == 0) {
            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(offset, std::ios::beg);
            readSize = fileSize - offset;
        }
    }

    std::vector<uint8_t> resultFile(readSize);
    if (!file.read(reinterpret_cast<char*>(resultFile.data()), readSize))
        throw std::runtime_error("Failed to read complete local file: " + localPath.string());

    std::array<uint8_t, 16> decodeKey;
    if (fileType != "prodConfig" && !armadilloKey_.empty()) {
        if (!KeyService::TryGetArmadilloKey(armadilloKey_, decodeKey))
            throw TACT::Exception(TACT::ErrorCategory::MissingKey, "Missing Armadillo key " + armadilloKey_);
        SalsaDecrypt::Decrypt(resultFile, offset, resource.substr(16, 16), decodeKey);
    }

    return resultFile;
}

bool CDN::TryGetLocalFile(const std::string &eKey, std::vector<uint8_t> &outData) {
    auto bytes = hexToBytes(eKey);
    if (bytes.size() < 9) return false;

    uint8_t i = 0;
    for (int idx = 0; idx < 9; ++idx) i ^= bytes[idx];

    uint8_t bucket = (i & 0xF) ^ (i >> 4);

    auto it = cascIndices_.find(bucket);
    if (it == cascIndices_.end()) return false;

    auto info = it->second->GetIndexInfo(bytes);
    if (info.archiveOffset == -1) return false;

    const auto openStart = std::chrono::steady_clock::now();
    std::filesystem::path archivePath =
        settings_.BaseDir.value() / ("Data/data/data." + PadLeft(std::to_string(info.archiveIndex), 3, '0'));

    if (!std::filesystem::exists(archivePath))
        throw TACT::Exception(TACT::ErrorCategory::FileNotFound, "Archive not found: " + archivePath.string());
    size_t fileLen = std::filesystem::file_size(archivePath);
    if (info.archiveOffset + info.archiveSize > fileLen) return false;
    std::ifstream ifs(archivePath, std::ios::binary);
    if (!ifs) throw std::runtime_error("Unable to open archive: " + archivePath.string());
    const auto readStart = std::chrono::steady_clock::now();
    tactReadTiming.open += readStart - openStart;
    ifs.seekg(info.archiveOffset);
    outData.resize(info.archiveSize);
    if (!ifs.read(reinterpret_cast<char *>(outData.data()), info.archiveSize)) {
        outData.clear();
        throw std::runtime_error("Unable to read archive: " + archivePath.string());
    }
    tactReadTiming.read += std::chrono::steady_clock::now() - readStart;
    return true;
}
