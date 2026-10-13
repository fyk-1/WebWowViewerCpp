//
// Created by deamon on 11.01.18.
//
#include "wdlFile.h"
#include <cstring>


chunkDef<WdlFile> WdlFile::wdlFileTable = {
    [](WdlFile &file, ChunkData &chunkData) {},
    {
        {
            'MLDD',
            {
                [](WdlFile &file, ChunkData &chunkData) {
                    debuglog("Entered MLDD");

                    file.doodadDefObj_len = chunkData.chunkLen / sizeof(SMDoodadDef);
                    chunkData.readValues(file.doodadDefObj, file.doodadDefObj_len);
                }
            }
        },
        {
            'MLMD',
            {
                [](WdlFile& file, ChunkData& chunkData){
                    debuglog("Entered MLMD");

                    file.mapObjDefObj_len = chunkData.chunkLen / sizeof(SMMapObjDefObj1);
                    chunkData.readValues(file.mapObjDefObj, file.mapObjDefObj_len);
                }
            }
        },
        {
            'MSSO',
            {
                [](WdlFile& file, ChunkData& chunkData){
                    debuglog("Entered MSSO");

                    file.m_msso_len = chunkData.chunkLen / sizeof(msso_t);

                    chunkData.readValues(file.m_msso, file.m_msso_len);
                }
            }
        },
        {
            'MSSN',
            {
                [](WdlFile& file, ChunkData& chunkData){
                    debuglog("Entered MSSN");

                    file.m_mssn_len = chunkData.chunkLen / sizeof(mssn_t);

                    chunkData.readValues(file.m_mssn, file.m_mssn_len);
                }
            }
        },
        {
            'MSSC',
            {
                [](WdlFile& file, ChunkData& chunkData){
                    debuglog("Entered MSSC");

                    file.m_mssc_len = chunkData.chunkLen / sizeof(mssc_t);

                    chunkData.readValues(file.m_mssc, file.m_mssc_len);
                }
            }
        },
        {
            'MSLI',
            {
                [](WdlFile& file, ChunkData& chunkData){
                    debuglog("Entered MSLI");

                    file.m_msli_len = chunkData.chunkLen / sizeof(uint32_t);

                    chunkData.readValues(file.m_msli, file.m_msli_len);
                }
            }
        },
        {
            'MSLD',
            {
                [](WdlFile& file, ChunkData& chunkData){
                    debuglog("Entered MSLD");

                    file.m_msld_len = chunkData.chunkLen / sizeof(msld_t);

                    chunkData.readValues(file.m_msld, file.m_msld_len);
                }
            }
        },
        {
            'MSSF',
            {
                [](WdlFile& file, ChunkData& chunkData){
                    debuglog("Entered MSSF");

                    chunkData.readValues(file.m_mssf, chunkData.chunkLen/8);
                }
            }
        },
        {
            'MAOF',
            {
                [](WdlFile& file, ChunkData& chunkData){
                    debuglog("Entered MAOF");

                    file.m_maof_len = chunkData.chunkLen / sizeof(uint32_t);

                    chunkData.readValues(file.m_maof, file.m_maof_len);
                }
            }
        }
    }
};

/*
robots@Impl: The low-detail terrain read by offset.

ChunkFileReader calls a handler for every chunk of an id, and a MARE chunk belongs to the tile MAOF points at, so the chunks
are not read in file order. MAOF is kept as a pointer into the file, and getTile reads the chunk it names on demand: the 8-byte
header, then 17 * 17 outer and 16 * 16 inner heights as int16 (the inner ones are skipped), and a MAHO chunk of 16 uint16 hole
rows right after, when its id is there. Chunk ids are compared as ChunkFileReader reads them, as a uint32.
*/
bool WdlFile::getTile(int row, int col, Tile &tile) const {
    tile = {};
    if (m_wdlFile == nullptr || m_maof == nullptr) return false;
    if (row < 0 || row >= 64 || col < 0 || col >= 64) return false;
    if (row * 64 + col >= m_maof_len) return false;

    const size_t offset = m_maof[row * 64 + col];
    if (offset == 0) return false;

    constexpr size_t headerSize = 8;
    constexpr size_t outerCount = 17 * 17;
    constexpr size_t bodySize = (outerCount + 16 * 16) * sizeof(int16_t);
    const auto &bytes = *m_wdlFile;
    if (offset + headerSize + bodySize > bytes.size()) return false;

    uint32_t ident = 0;
    std::memcpy(&ident, bytes.data() + offset, sizeof(ident));
    if (ident != 'MARE') return false;

    tile.outer = reinterpret_cast<const int16_t *>(bytes.data() + offset + headerSize);

    const size_t holesOffset = offset + headerSize + bodySize;
    if (holesOffset + headerSize + 16 * sizeof(uint16_t) <= bytes.size()) {
        std::memcpy(&ident, bytes.data() + holesOffset, sizeof(ident));
        if (ident == 'MAHO') {
            tile.holes = reinterpret_cast<const uint16_t *>(bytes.data() + holesOffset + headerSize);
        }
    }
    return true;
}

void WdlFile::process(HFileContent wdlFile) {
    m_wdlFile = wdlFile;
    CChunkFileReader reader(*m_wdlFile.get(), getFileNameOrDataId());
    reader.processFile(*this, &WdlFile::wdlFileTable);

    fsStatus = FileStatus::FSLoaded;
}
