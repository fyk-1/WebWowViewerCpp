#include "farHeights.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
    constexpr char kMagic[4] = {'W', 'P', 'H', '1'};
    constexpr uint16_t kVersion = 1;

    struct Header {
        char magic[4];
        uint16_t version;
        uint16_t size;
        float zMin;
        float zMax;
    };
    static_assert(sizeof(Header) == 16);

    bool readHeaderFrom(std::ifstream &stream, Header &header, std::string &error) {
        if (!stream.read(reinterpret_cast<char *>(&header), sizeof(header))) {
            error = "no header";
            return false;
        }
        if (std::memcmp(header.magic, kMagic, 4) != 0 || header.version != kVersion || header.size != HeightTile::kSize) {
            error = "not a version 1 height tile of 128";
            return false;
        }
        return true;
    }

    bool sizeIsRight(const std::filesystem::path &file, std::string &error) {
        std::error_code code;
        const auto size = std::filesystem::file_size(file, code);
        if (code || size != FarHeights::kFileBytes) {
            error = code ? "cannot read " + file.string() : file.string() + " is not " + std::to_string(FarHeights::kFileBytes) + " bytes";
            return false;
        }
        return true;
    }
}

uint16_t HeightTile::fromZ(float z) {
    const float value = std::round((z + 1024.0f) * 16.0f);
    return (uint16_t) std::clamp(value, 1.0f, 65535.0f);
}

std::string FarHeights::path(const std::string &mapDir, int row, int col) {
    return (std::filesystem::path(mapDir) / (std::to_string(row) + "_" + std::to_string(col) + ".h16")).string();
}

std::string FarHeights::mapDir(const std::string &buildDir, int mapId) {
    return (std::filesystem::path(buildDir) / std::to_string(mapId)).string();
}

bool FarHeights::readHeader(const std::string &file, float &zMin, float &zMax) {
    std::string error;
    const auto native = std::filesystem::u8path(file);
    if (!sizeIsRight(native, error)) return false;

    std::ifstream stream(native, std::ios::binary);
    Header header;
    if (!readHeaderFrom(stream, header, error)) return false;
    zMin = header.zMin;
    zMax = header.zMax;
    return true;
}

bool FarHeights::read(const std::string &file, HeightTile &out, std::string &error) {
    const auto native = std::filesystem::u8path(file);
    if (!sizeIsRight(native, error)) return false;

    std::ifstream stream(native, std::ios::binary);
    Header header;
    if (!readHeaderFrom(stream, header, error)) return false;
    if (!stream.read(reinterpret_cast<char *>(out.v.data()), sizeof(out.v))) {
        error = "cannot read " + file;
        return false;
    }
    out.zMin = header.zMin;
    out.zMax = header.zMax;
    return true;
}

std::string FarHeights::landmarksPath(const std::string &mapDir) {
    return mapDir + "/landmarks.ids";
}

bool FarHeights::writeLandmarks(const std::string &mapDir, const std::vector<IdEntry> &ids, std::string &error) {
    const auto native = std::filesystem::u8path(landmarksPath(mapDir));
    auto temp = native;
    temp += ".tmp";

    std::error_code code;
    std::filesystem::create_directories(native.parent_path(), code);
    {
        std::ofstream stream(temp, std::ios::trunc);
        for (const IdEntry &id : ids) stream << id.text() << "\n";
        stream.flush();
        if (!stream) {
            error = "cannot write " + temp.string();
            return false;
        }
    }
    std::filesystem::rename(temp, native, code);
    if (code) {
        error = "cannot rename " + temp.string() + ": " + code.message();
        return false;
    }
    return true;
}

std::vector<IdEntry> FarHeights::readLandmarks(const std::string &mapDir) {
    std::ifstream stream(std::filesystem::u8path(landmarksPath(mapDir)));
    std::vector<IdEntry> ids;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        IdEntry entry;
        if (!IdEntry::parse(line, entry)) {
            std::cerr << "far heights: " << landmarksPath(mapDir) << ": \"" << line << "\" is no id, so the map has no landmarks" << std::endl;
            return {};
        }
        ids.push_back(entry);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool FarHeights::writeAtomic(const std::string &file, const HeightTile &tile, std::string &error) {
    const auto native = std::filesystem::u8path(file);
    auto temp = native;
    temp += ".tmp";

    std::error_code code;
    std::filesystem::create_directories(native.parent_path(), code);
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        Header header = {{kMagic[0], kMagic[1], kMagic[2], kMagic[3]}, kVersion, HeightTile::kSize, tile.zMin, tile.zMax};
        stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
        stream.write(reinterpret_cast<const char *>(tile.v.data()), sizeof(tile.v));
        stream.flush();
        if (!stream) {
            error = "cannot write " + temp.string();
            return false;
        }
    }
    std::filesystem::rename(temp, native, code);
    if (code) {
        error = "cannot rename " + temp.string() + ": " + code.message();
        return false;
    }
    return true;
}

float FarHeights::edgeStep(const HeightTile &a, const HeightTile &b, int edge) {
    const int last = HeightTile::kSize - 1;
    float step = 0.0f;
    for (int i = 0; i <= last; i++) {
        float own = 0.0f, other = 0.0f;
        switch (edge) {
            case 0: own = a.z(0, i); other = b.z(last, i); break;
            case 1: own = a.z(last, i); other = b.z(0, i); break;
            case 2: own = a.z(i, 0); other = b.z(i, last); break;
            default: own = a.z(i, last); other = b.z(i, 0); break;
        }
        step = std::max(step, std::fabs(own - other));
    }
    return step;
}
