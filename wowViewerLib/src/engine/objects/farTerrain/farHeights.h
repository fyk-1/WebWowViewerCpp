#ifndef WOWVIEWERLIB_FARHEIGHTS_H
#define WOWVIEWERLIB_FARHEIGHTS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/*
 * One baked height tile of the far terrain: 128 x 128 heights of the tile with its buildings, in the frame of the WDL's grid.
 * Row r runs along world x (falling) and column c along world y (falling), both from the tile's corner at
 * x0 = 17066.666 - row * TILESIZE, y0 = 17066.666 - col * TILESIZE. Sample (r, c) lies at x0 - r * TILESIZE / 127, y0 - c * TILESIZE / 127,
 * so samples 0 and 127 sit on the tile's edges and a neighbour's edge samples lie at the same world positions.
 *
 * The file is `<row>_<col>.h16`, little-endian: "WPH1", u16 version 1, u16 size 128, f32 zMin, f32 zMax (the baked heights' own
 * lowest and highest, in yards), then 128 x 128 u16 heights in row-major [r][c]. A height v is the z of v / 16 - 1024 yards; 0
 * means no data and no baked file holds one.
 */
struct HeightTile {
    static constexpr int kSize = 128;
    float zMin = 0;
    float zMax = 0;
    std::array<uint16_t, kSize * kSize> v = {};

    static float toZ(uint16_t value) { return value / 16.0f - 1024.0f; }
    // The nearest height value, kept within 1 to 65535.
    static uint16_t fromZ(float z);
    float z(int r, int c) const { return toZ(v[r * kSize + c]); }
};

struct FarHeights {
    static constexpr size_t kFileBytes = 16 + sizeof(HeightTile::v);

    // The tile's file under a map's folder.
    static std::string path(const std::string &mapDir, int row, int col);
    // The map's folder under the build's: `<buildDir>/<mapId>`.
    static std::string mapDir(const std::string &buildDir, int mapId);

    // False with the reason in error when the file is missing, is not kFileBytes long, or has another magic, version or size.
    static bool read(const std::string &file, HeightTile &out, std::string &error);
    // The header alone, false on the same grounds.
    static bool readHeader(const std::string &file, float &zMin, float &zMax);
    // Writes `<file>.tmp` and renames it over `file`, so a reader never sees half of one.
    static bool writeAtomic(const std::string &file, const HeightTile &tile, std::string &error);

    // The map's landmarks, `<mapDir>/landmarks.ids`: the FileDataIDs of the M2 models and WMOs the bake left out of the heights and the
    // far pass draws as themselves (FarLandmarks), one decimal id a line. Written whole by the bake, read when the far land opens a map.
    static std::string landmarksPath(const std::string &mapDir);
    static bool writeLandmarks(const std::string &mapDir, const std::vector<int> &ids, std::string &error);
    // The ids, sorted; none when the file is missing or a line does not parse.
    static std::vector<int> readLandmarks(const std::string &mapDir);

    // The largest height difference in yards along the edge two neighbouring tiles share. `edge` is the side of `a` that touches `b`:
    // 0 row 0 (a's higher x), 1 row 127, 2 column 0 (a's higher y), 3 column 127.
    static float edgeStep(const HeightTile &a, const HeightTile &b, int edge);
};

#endif //WOWVIEWERLIB_FARHEIGHTS_H
