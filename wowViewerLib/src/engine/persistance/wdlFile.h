//
// Created by deamon on 11.01.18.
//

#ifndef WEBWOWVIEWERCPP_WDLFILE_H
#define WEBWOWVIEWERCPP_WDLFILE_H

#include <vector>
#include <string>
#include "helper/ChunkFileReader.h"
#include "header/adtFileHeader.h"
#include "../../include/sharedFile.h"
#include "PersistentFile.h"
#include "header/wdlHeader.h"


class WdlFile : public PersistentFile {
public:
    WdlFile(const std::string &fileName) : PersistentFile(fileName) {};
    WdlFile(int fileDataId) : PersistentFile(fileDataId) {};

    void process(HFileContent wdlFile) override;

public:
    SMDoodadDef * doodadDefObj = nullptr;
    int doodadDefObj_len = 0;

    SMMapObjDefObj1 * mapObjDefObj = nullptr;
    int mapObjDefObj_len = 0;

    msso_t *m_msso = nullptr;
    int m_msso_len = -1;

    mssn_t *m_mssn = nullptr;
    int m_mssn_len = -1;

    mssc_t *m_mssc = nullptr;
    int m_mssc_len = -1;

    uint32_t *m_msli = nullptr;
    int m_msli_len = -1;

    msld_t *m_msld = nullptr;
    int m_msld_len = -1;

    uint64_t *m_mssf = nullptr;

    // The low-detail terrain: MAOF's absolute file offsets of the tiles' MARE chunks, indexed row * 64 + col, 0 for a tile with none.
    uint32_t *m_maof = nullptr;
    int m_maof_len = -1;

    // What a tile's MARE chunk gives: its 17 x 17 outer heights, first index along the tile's rows (world x falling) and second
    // along its columns (world y falling), in yards, and the 16 hole rows of its MAHO chunk, one bit a cell, nullptr when it has none.
    struct Tile {
        const int16_t *outer = nullptr;
        const uint16_t *holes = nullptr;
    };
    // Answers false for a tile with no low-detail terrain, and for an offset that leaves the file or does not name a MARE chunk.
    bool getTile(int row, int col, Tile &tile) const;

private:

    HFileContent m_wdlFile;
    static chunkDef<WdlFile> wdlFileTable;
};


#endif //WEBWOWVIEWERCPP_WDLFILE_H
