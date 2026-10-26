#include "TileLightGrid.h"
#include <algorithm>

void TileLightGrid::advanceBlends(const std::shared_ptr<IClientDatabase> &databaseHandler, int mapId,
                                  const std::vector<mapInnerZoneLightRecord> &zoneLights, const Source &source, int maxTiles) {
    if (blendsReady() || databaseHandler == nullptr || !source.ready || !source.exists || !source.ready()) return;

    int done = 0;
    while (m_next < kTiles && done < maxTiles) {
        const int tile = m_next++;
        const int row = tile / kSide;
        const int col = tile % kSide;
        if (!source.exists(row, col)) continue;

        const float x = MathHelper::TILESIZE * (32 - row) - MathHelper::TILESIZE / 2;
        const float y = MathHelper::TILESIZE * (32 - col) - MathHelper::TILESIZE / 2;
        float z = 0.0f;
        if (source.ground) source.ground(x, y, z);

        // 🤖 No StateForConditions: a state would collect every tile's zone lights.
        auto results = calculateLightParamBlends(databaseHandler, mapId, mathfu::vec3(x, y, z), nullptr, zoneLights);
        for (const auto &param : results.params) {
            m_blends[tile].push_back({param.id, param.blend});
        }
        for (const auto &param : results.stormParams) {
            m_stormBlends[tile].push_back({param.id, param.blend});
        }
        done++;
    }

    if (blendsReady()) {
        for (const auto &blends : m_blends) {
            for (const auto &blend : blends) m_paramIds.push_back(blend.id);
        }
        std::sort(m_paramIds.begin(), m_paramIds.end());
        m_paramIds.erase(std::unique(m_paramIds.begin(), m_paramIds.end()), m_paramIds.end());

        for (const auto &blends : m_stormBlends) {
            for (const auto &blend : blends) m_stormParamIds.push_back(blend.id);
        }
        std::sort(m_stormParamIds.begin(), m_stormParamIds.end());
        m_stormParamIds.erase(std::unique(m_stormParamIds.begin(), m_stormParamIds.end()), m_stormParamIds.end());
    }
}

void TileLightGrid::setTile(int tile, bool valid, const ExteriorColors &exterior, const LiquidColors &liquid) {
    mathfu::vec4_packed *out = m_work.data() + tile * kVec4sPerTile;
    out[0] = mathfu::vec4_packed(mathfu::vec4(exterior.exteriorAmbientColor, valid ? 1.0f : 0.0f));
    out[1] = mathfu::vec4_packed(mathfu::vec4(exterior.exteriorHorizontAmbientColor, 1.0f));
    out[2] = mathfu::vec4_packed(mathfu::vec4(exterior.exteriorGroundAmbientColor, 1.0f));
    out[3] = mathfu::vec4_packed(mathfu::vec4(exterior.exteriorDirectColor, 1.0f));
    out[4] = mathfu::vec4_packed(mathfu::vec4(exterior.exteriorSpecularColor, 1.0f));
    out[5] = mathfu::vec4_packed(mathfu::vec4(liquid.closeRiverColor, liquid.riverShallowAlpha));
    out[6] = mathfu::vec4_packed(mathfu::vec4(liquid.farRiverColor, liquid.riverDeepAlpha));
    out[7] = mathfu::vec4_packed(mathfu::vec4(liquid.closeOceanColor, liquid.oceanShallowAlpha));
    out[8] = mathfu::vec4_packed(mathfu::vec4(liquid.farOceanColor, liquid.oceanDeepAlpha));
}

void TileLightGrid::publish(int time) {
    m_published = std::make_shared<const std::vector<mathfu::vec4_packed>>(m_work);
    m_colouredAt = time;
    m_version++;
}
