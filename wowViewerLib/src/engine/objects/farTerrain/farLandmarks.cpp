#include "farLandmarks.h"

#include <iostream>
#include "../../algorithms/mathHelper.h"
#include "../../geometry/m2Geom.h"
#include "../../geometry/skinGeom.h"
#include "../../geometry/wmoGroupGeom.h"
#include "../../geometry/wmoMainGeom.h"
#include "../../persistance/adtFile.h"

/*
robots@Impl: Finding and flattening the landmarks.

The placements come from the object file (obj0) of every tile the WDT's MAID lists, kScanInFlight at a time so the tiles the camera
waits for keep their place in the loading queue, each dropped once read. A WMO
is listed in every tile it overlaps under one uniqueId, and so is a large model, so a uniqueId counts once. The placement matrix is
the one M2Object and WmoObject build from the same entry. Then each model is loaded with its first skin, and each WMO with its
exterior groups, the ones seen from outside; the rest add vertices nobody sees from afar. The geometry is in world space, so every
placement holds its own copy, and the far pass's vertex shader takes the camera off each vertex as it does for the far land.

A model draws the batches of its skin whose texture is a plain file and whose material is opaque or alpha-keyed; a replaceable
texture (a creature's skin) and a blended material (glows, particles) are left out. A WMO draws every batch of its exterior groups
with its material's first texture. Animation is ignored: a model stands in its bind pose. A batch whose lookups or indices fall outside
its file is skipped, so an odd file costs that batch and never the flight.
*/
namespace {
    constexpr size_t kScanInFlight = 16;

    mathfu::mat4 placementMatrix(const C3Vector &position, const C3Vector &rotation, float scale) {
        mathfu::mat4 matrix = MathHelper::getAdtToWorldMat4();
        matrix *= mathfu::mat4::FromTranslationVector(mathfu::vec3(position));
        matrix *= mathfu::mat4::FromScaleVector(mathfu::vec3(-1, 1, -1));
        matrix *= MathHelper::RotationY(toRadian(rotation.y - 270));
        matrix *= MathHelper::RotationZ(toRadian(-rotation.x));
        matrix *= MathHelper::RotationX(toRadian(rotation.z - 90));
        matrix *= mathfu::mat4::FromScaleVector(mathfu::vec3(scale, scale, scale));
        return matrix;
    }

    FarTerrainVertex vertexOf(const mathfu::mat4 &matrix, const mathfu::vec3 &position, const mathfu::vec3 &normal, const mathfu::vec2 &uv) {
        const mathfu::vec3 world = (matrix * mathfu::vec4(position, 1.0f)).xyz();
        mathfu::vec3 n = (matrix * mathfu::vec4(normal, 0.0f)).xyz();
        n = n.Length() > 0.0f ? n.Normalized() : mathfu::vec3(0.0f, 0.0f, 1.0f);
        return {{world.x, world.y, world.z}, {n.x, n.y, n.z}, {uv.x, uv.y}};
    }

    bool settled(FileStatus status) {
        return status == FileStatus::FSLoaded || status == FileStatus::FSRejected;
    }
}

FarLandmarks::FarLandmarks(const HApiContainer &api, const HWdtFile &wdt, std::vector<int> ids) :
    m_api(api), m_wdt(wdt), m_ids(std::move(ids)), m_idSet(m_ids.begin(), m_ids.end()) {
}

bool FarLandmarks::poll() {
    if (m_phase == Phase::Done) return true;

    if (m_phase == Phase::Scan) {
        for (auto &file : m_objectFiles) {
            if (file == nullptr || !settled(file->getStatus())) continue;
            if (file->getStatus() == FileStatus::FSLoaded) scan(*file);
            file = nullptr;
        }
        std::erase(m_objectFiles, nullptr);
        while (m_objectFiles.size() < kScanInFlight && m_nextTile < 64 * 64) {
            const uint32_t obj0 = m_wdt->mapFileDataIDs != nullptr ? m_wdt->mapFileDataIDs->array[m_nextTile].obj0ADT : 0;
            m_nextTile++;
            if (obj0 != 0) m_objectFiles.push_back(m_api->cacheStorage->getAdtGeomCache()->getFileId((int) obj0));
        }
        if (!m_objectFiles.empty() || m_nextTile < 64 * 64) return false;

        request();
        m_phase = Phase::Load;
    }

    if (!loaded()) return false;

    build();
    m_m2s.clear();
    m_skins.clear();
    m_wmos.clear();
    m_groups.clear();
    m_phase = Phase::Done;
    std::cerr << "far landmarks: " << m_placements.size() << " placement(s), " << m_geometry.vertices.size() << " vertices, "
              << m_geometry.draws.size() << " draws" << std::endl;
    return true;
}

void FarLandmarks::scan(const AdtFile &file) {
    for (int i = 0; i < file.doodadDef_len; i++) {
        const SMDoodadDef &def = file.doodadDef[i];
        if (!def.flags.mddf_entry_is_filedata_id || m_idSet.count((int) def.nameId) == 0) continue;
        if (!m_seen.insert(def.uniqueId).second) continue;
        m_placements.push_back({false, (int) def.nameId, placementMatrix(def.position, def.rotation, def.scale / 1024.0f)});
    }
    for (int i = 0; i < file.mapObjDef_len; i++) {
        const SMMapObjDef &def = file.mapObjDef[i];
        if (!def.flags.modf_entry_is_filedata_id || m_idSet.count((int) def.nameId) == 0) continue;
        if (!m_seen.insert(def.uniqueId).second) continue;
        m_placements.push_back({true, (int) def.nameId, placementMatrix(def.position, def.rotation, def.unk != 0 ? def.unk / 1024.0f : 1.0f)});
    }
}

void FarLandmarks::request() {
    auto *cache = m_api->cacheStorage.get();
    for (const auto &placement : m_placements) {
        if (placement.wmo) {
            if (m_wmos.count(placement.fileId) == 0) m_wmos[placement.fileId] = cache->getWmoMainCache()->getFileId(placement.fileId);
        } else {
            if (m_m2s.count(placement.fileId) == 0) m_m2s[placement.fileId] = cache->getM2GeomCache()->getFileId(placement.fileId);
        }
    }
}

bool FarLandmarks::loaded() {
    auto *cache = m_api->cacheStorage.get();
    bool all = true;
    for (auto &[fileId, m2] : m_m2s) {
        if (m2 == nullptr || !settled(m2->getStatus())) { all = all && m2 == nullptr; continue; }
        if (m2->getStatus() != FileStatus::FSLoaded || m2->skinFileDataIDs.empty()) continue;
        auto &skin = m_skins[fileId];
        if (skin == nullptr) skin = cache->getSkinGeomCache()->getFileId((int) m2->skinFileDataIDs[0]);
        if (skin != nullptr && !settled(skin->getStatus())) all = false;
    }
    for (auto &[fileId, wmo] : m_wmos) {
        if (wmo == nullptr || !settled(wmo->getStatus())) { all = all && wmo == nullptr; continue; }
        if (wmo->getStatus() != FileStatus::FSLoaded || wmo->gfids.empty()) continue;
        auto &groups = m_groups[fileId];
        if (groups.empty()) {
            groups.resize(wmo->groupsLen);
            for (int i = 0; i < wmo->groupsLen && i < (int) wmo->gfids[0].size(); i++) {
                if (wmo->groups[i].flags.EXTERIOR && wmo->gfids[0][i] != 0) {
                    groups[i] = cache->getWmoGroupGeomCache()->getFileId((int) wmo->gfids[0][i]);
                }
            }
        }
        for (const auto &group : groups) {
            if (group != nullptr && !settled(group->getStatus())) all = false;
        }
    }
    return all;
}

uint16_t FarLandmarks::textureSlot(const HBlpTexture &texture) {
    auto it = m_textureSlots.find(texture.get());
    if (it != m_textureSlots.end()) return it->second;
    const auto slot = (uint16_t) m_geometry.textures.size();
    m_geometry.textures.push_back(texture);
    m_textureSlots[texture.get()] = slot;
    return slot;
}

void FarLandmarks::build() {
    auto *textures = m_api->cacheStorage->getTextureCache();
    for (const auto &placement : m_placements) {
        if (!placement.wmo) {
            const auto m2It = m_m2s.find(placement.fileId);
            const auto skinIt = m_skins.find(placement.fileId);
            if (m2It == m_m2s.end() || skinIt == m_skins.end() || m2It->second == nullptr || skinIt->second == nullptr) continue;
            const M2Geom &geom = *m2It->second;
            const M2Data *m2 = geom.getM2Data();
            M2SkinProfile *skin = skinIt->second->getSkinData();
            if (m2 == nullptr || skin == nullptr) continue;

            const auto base = (int32_t) m_geometry.vertices.size();
            bool vertices = true;
            for (int k = 0; k < skin->vertices.size && vertices; k++) {
                const unsigned short *index = skin->vertices.getElement(k);
                const M2Vertex *v = index != nullptr ? m2->vertices.getElement(*index) : nullptr;
                if (v == nullptr) { vertices = false; break; }
                m_geometry.vertices.push_back(vertexOf(placement.matrix, mathfu::vec3(v->pos), mathfu::vec3(v->normal), mathfu::vec2(v->tex_coords[0])));
            }
            if (!vertices) {
                m_geometry.vertices.resize(base);
                continue;
            }
            for (int b = 0; b < skin->batches.size; b++) {
                const M2Batch &batch = *skin->batches.getElement(b);
                const M2Material *materialOf = m2->materials.getElement(batch.materialIndex);
                const int16_t *lookup = m2->texture_lookup_table.getElement(batch.textureComboIndex);
                const M2SkinSection *sectionOf = skin->skinSections.getElement(batch.skinSectionIndex);
                if (materialOf == nullptr || lookup == nullptr || *lookup < 0 || sectionOf == nullptr) continue;
                const M2Material &material = *materialOf;
                if (material.blending_mode > 1) continue;
                const int textureIndex = *lookup;
                const M2Texture *textureOf = m2->textures.getElement(textureIndex);
                if (textureOf == nullptr || textureOf->type != 0) continue;
                const M2Texture &texture = *textureOf;

                HBlpTexture blp;
                if (textureIndex < (int) geom.textureFileDataIDs.size() && geom.textureFileDataIDs[textureIndex] != 0) {
                    blp = textures->getFileId((int) geom.textureFileDataIDs[textureIndex]);
                } else if (texture.filename.size > 0) {
                    blp = textures->get(std::string(texture.filename.getElement(0), texture.filename.size - 1));
                }
                if (blp == nullptr) continue;

                const M2SkinSection &section = *sectionOf;
                const uint32_t start = section.indexStart + ((uint32_t) section.Level << 16);
                if (start + section.indexCount > (uint32_t) skin->indices.size) continue;
                FarLandmarks::Draw draw;
                draw.firstIndex = (uint32_t) m_geometry.indices.size();
                draw.indexCount = section.indexCount;
                draw.vertexOffset = base;
                draw.texture = textureSlot(blp);
                draw.alphaTest = material.blending_mode == 1;
                for (uint32_t i = 0; i < section.indexCount; i++) m_geometry.indices.push_back(*skin->indices.getElement((int) (start + i)));
                m_geometry.draws.push_back(draw);
            }
            continue;
        }

        const auto wmoIt = m_wmos.find(placement.fileId);
        const auto groupsIt = m_groups.find(placement.fileId);
        if (wmoIt == m_wmos.end() || groupsIt == m_groups.end() || wmoIt->second == nullptr) continue;
        const WmoMainGeom &wmo = *wmoIt->second;
        for (const auto &group : groupsIt->second) {
            if (group == nullptr || group->getStatus() != FileStatus::FSLoaded) continue;
            const WmoGroupGeom &geom = *group;

            const auto base = (int32_t) m_geometry.vertices.size();
            for (int v = 0; v < geom.verticesLen; v++) {
                const mathfu::vec3 normal = v < geom.normalsLen ? mathfu::vec3(geom.normals[v]) : mathfu::vec3(0.0f, 0.0f, 1.0f);
                const mathfu::vec2 uv = v < geom.textureCoordsLen ? mathfu::vec2(geom.textCoords[v]) : mathfu::vec2(0.0f, 0.0f);
                m_geometry.vertices.push_back(vertexOf(placement.matrix, mathfu::vec3(geom.verticles[v]), normal, uv));
            }
            for (int b = 0; b < geom.batchesLen; b++) {
                const SMOBatch &batch = geom.batches[b];
                if ((int64_t) batch.first_index + batch.num_indices > geom.indicesLen) continue;
                const int materialId = batch.flag_use_material_id_large ? batch.postLegion.material_id_large : batch.material_id;
                if (materialId >= wmo.materialsLen) continue;
                const SMOMaterial &material = wmo.materials[materialId];
                if (material.blendMode > 1 || material.diffuseNameIndex == 0) continue;

                HBlpTexture blp;
                if (wmo.textureNamesField != nullptr) {
                    if ((int) material.diffuseNameIndex >= wmo.textureNamesFieldLen) continue;
                    std::string name(&wmo.textureNamesField[material.diffuseNameIndex]);
                    if (name.empty()) continue;
                    blp = textures->get(name);
                } else {
                    blp = textures->getFileId((int) material.diffuseNameIndex);
                }
                if (blp == nullptr) continue;

                FarLandmarks::Draw draw;
                draw.firstIndex = (uint32_t) m_geometry.indices.size();
                draw.indexCount = batch.num_indices;
                draw.vertexOffset = base;
                draw.texture = textureSlot(blp);
                draw.alphaTest = material.blendMode == 1;
                for (uint32_t i = 0; i < batch.num_indices; i++) m_geometry.indices.push_back(geom.indicies[(int) (batch.first_index + i)]);
                m_geometry.draws.push_back(draw);
            }
        }
    }
}
