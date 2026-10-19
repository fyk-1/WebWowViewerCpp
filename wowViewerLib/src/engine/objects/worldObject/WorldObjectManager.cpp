//
// Created by Deamon on 7/15/2026.
//

#include "WorldObjectManager.h"
#include "../wmo/wmoObject.h"


WorldObjectManager::WorldObjectManager(HApiContainer api, int mapId) : m_api(api), m_mapId(mapId) {
}

std::shared_ptr<PlacedCreature> WorldObjectManager::createCreature(HApiContainer api, const CreatureSpawnRecord &spawn) {
    if (spawn.ModelFileDataID <= 0) return nullptr;

    auto textureCache = api->cacheStorage->getTextureCache();
    std::vector<HBlpTexture> replaceTextures(14, nullptr);
    for (int i = 0; i < 3; i++) {
        if (spawn.Textures[i] > 0) replaceTextures[11 + i] = textureCache->getFileId(spawn.Textures[i]);
    }
    auto fill = [&](int type, int fileId) { if (fileId > 0) replaceTextures[type] = textureCache->getFileId(fileId); };
    fill(1, spawn.BodyTexture);
    fill(2, spawn.CapeTexture);
    fill(6, spawn.HairTexture);
    fill(8, spawn.SkinExtraTexture);

    auto m2Object = m2Factory->createObject(api);
    m2Object->markSpawn();
    m2Object->setLoadParams(0, spawn.MeshIds, replaceTextures);
    m2Object->hideUnfilledBatches();
    m2Object->setAlpha(spawn.Alpha / 255.0f);
    m2Object->setModelFileId(spawn.ModelFileDataID);
    if (spawn.Anim >= 0) m2Object->setInitialAnimation(spawn.Anim);

    mathfu::mat4 rotationMat = MathHelper::RotationZ(spawn.Facing);
    m2Object->createPlacementMatrix(mathfu::vec3(spawn.Pos[0], spawn.Pos[1], spawn.Pos[2]), 0.0f,
                                    mathfu::vec3(spawn.Scale, spawn.Scale, spawn.Scale), &rotationMat);
    m2Object->calcWorldPosition();

    for (auto const &attachment : spawn.Attachments) {
        if (attachment.ModelFileDataID <= 0) continue;

        //The item texture is the model's type 2, and the textures after it fill the types after that
        std::vector<HBlpTexture> itemTextures(14, nullptr);
        for (size_t i = 0; i < attachment.Textures.size() && 2 + i < itemTextures.size(); i++) {
            if (attachment.Textures[i] > 0) itemTextures[2 + i] = textureCache->getFileId(attachment.Textures[i]);
        }

        auto item = m2Factory->createObject(api);
        item->markSpawn();
        item->setLoadParams(0, {}, itemTextures);
        item->hideUnfilledBatches();
        item->setModelFileId(attachment.ModelFileDataID);
        m2Object->attach(item, attachment.Attachment);
    }

    auto placed = m2Object;
    if (spawn.MountModelFileDataID > 0) {
        std::vector<HBlpTexture> mountTextures(14, nullptr);
        for (int i = 0; i < 3; i++) {
            if (spawn.MountTextures[i] > 0) mountTextures[11 + i] = textureCache->getFileId(spawn.MountTextures[i]);
        }

        auto mount = m2Factory->createObject(api);
        mount->markSpawn();
        mount->setLoadParams(0, {}, mountTextures);
        mount->hideUnfilledBatches();
        if (spawn.MountAnim >= 0) mount->setInitialAnimation(spawn.MountAnim);
        mount->setModelFileId(spawn.MountModelFileDataID);
        mount->createPlacementMatrix(mathfu::vec3(spawn.Pos[0], spawn.Pos[1], spawn.Pos[2]), 0.0f,
                                     mathfu::vec3(spawn.Scale, spawn.Scale, spawn.Scale), &rotationMat);
        mount->calcWorldPosition();
        mount->attach(m2Object, MountAttachment);
        placed = mount;
    }

    auto creature = std::make_shared<PlacedCreature>();
    creature->m2 = placed;
    creature->id = placed->getObjectId();
    creature->humanoid = spawn.BodyTexture > 0;
    creature->events = spawn.Events;
    return creature;
}

std::shared_ptr<PlacedCreature> WorldObjectManager::createGameObjectSpawn(HApiContainer api, const GameObjectSpawnRecord &spawn) {
    if (spawn.ModelFileDataID <= 0) return nullptr;

    auto m2Object = m2Factory->createObject(api);
    m2Object->markSpawn();
    m2Object->setLoadParams(0, {}, {});
    m2Object->hideUnfilledBatches();
    m2Object->setModelFileId(spawn.ModelFileDataID);
    if (spawn.Anim >= 0) m2Object->setInitialAnimation(spawn.Anim);

    //The row's quaternion is (x, y, z, w), the same as the GameObjects table's Rot_0 to Rot_3
    mathfu::mat4 rotationMat = mathfu::quat(spawn.Rotation[3], spawn.Rotation[0], spawn.Rotation[1], spawn.Rotation[2]).ToMatrix4();
    m2Object->createPlacementMatrix(mathfu::vec3(spawn.Pos[0], spawn.Pos[1], spawn.Pos[2]), 0.0f,
                                    mathfu::vec3(spawn.Scale, spawn.Scale, spawn.Scale), &rotationMat);
    m2Object->calcWorldPosition();

    auto placed = std::make_shared<PlacedCreature>();
    placed->m2 = m2Object;
    placed->id = m2Object->getObjectId();
    placed->gameObject = true;
    placed->events = spawn.Events;
    return placed;
}

std::shared_ptr<WmoObject> WorldObjectManager::createGameObjectWmo(HApiContainer api, const GameObjectSpawnRecord &spawn,
                                                                  const std::array<float, 3> &geoBoxMin, const std::array<float, 3> &geoBoxMax) {
    if (spawn.ModelFileDataID <= 0) return nullptr;

    mathfu::vec3 pos(spawn.Pos[0], spawn.Pos[1], spawn.Pos[2]);
    mathfu::vec3 scaleVec(spawn.Scale, spawn.Scale, spawn.Scale);
    mathfu::mat4 rotationMat = mathfu::quat(spawn.Rotation[3], spawn.Rotation[0], spawn.Rotation[1], spawn.Rotation[2]).ToMatrix4();
    mathfu::vec4 minVec(geoBoxMin[0], geoBoxMin[1], geoBoxMin[2], 1);
    mathfu::vec4 maxVec(geoBoxMax[0], geoBoxMax[1], geoBoxMax[2], 1);

    auto wmoObject = wmoFactory->createObject(api);
    wmoObject->setModelFileId(spawn.ModelFileDataID);
    wmoObject->setLoadingParam(pos, scaleVec, &rotationMat, minVec, maxVec);
    return wmoObject;
}

void WorldObjectManager::homeCreature(const std::shared_ptr<PlacedCreature> &creature, const std::array<float, 3> &spawnPosition,
                                      const std::vector<std::shared_ptr<WmoObject>> &wmos, bool onlyWhereBounded) {
    const mathfu::vec3 position(spawnPosition[0], spawnPosition[1], spawnPosition[2]);

    std::vector<WmoObject *> asked;
    for (auto const &wmo : wmos) {
        if (wmo == nullptr) continue;
        if (std::find(asked.begin(), asked.end(), wmo.get()) != asked.end()) continue;

        if (onlyWhereBounded) {
            const CAaBox bounds = wmo->getAABB();
            if (position.x < bounds.min.x || position.x > bounds.max.x ||
                position.y < bounds.min.y || position.y > bounds.max.y ||
                position.z < bounds.min.z || position.z > bounds.max.z) continue;
        }
        asked.push_back(wmo.get());
    }

    if (asked.empty()) {
        creature->home = CreatureHome::Exterior;
        return;
    }

    //The count is set before the first offer, so an answer cannot see it reach zero while the rest are still to ask
    creature->wmosToAnswer = (int) asked.size();
    for (auto *wmo : asked) {
        wmo->offerCreature(creature, position);
    }
}

void WorldObjectManager::spawnCustomM2(CustomM2Object &object) {
    object.m2Object = m2Factory->createObject(m_api);
    object.m2Object->setLoadParams(0, {}, {});
    if (!object.modelFileName.empty()) {
        object.m2Object->setModelFileName(object.modelFileName);
    } else {
        object.m2Object->setModelFileId(object.fileDataId);
    }

    applyCustomM2Placement(object, false);
}

void WorldObjectManager::applyCustomM2Placement(CustomM2Object &object, bool updateAABB) {
    if (!object.m2Object) return;

    // Same euler convention as ImGuizmo::RecomposeMatrixFromComponents (Rx * Ry * Rz),
    // so values edited through the gizmo and through numeric inputs stay in agreement.
    mathfu::mat4 rotationMat =
        MathHelper::RotationX(toRadian(object.rotationEulerDeg.x)) *
        MathHelper::RotationY(toRadian(object.rotationEulerDeg.y)) *
        MathHelper::RotationZ(toRadian(object.rotationEulerDeg.z));

    object.m2Object->createPlacementMatrix(object.position, 0.0f, object.scale, &rotationMat);
    object.m2Object->calcWorldPosition();

    // The world culling AABB lives in the pool's component storage and is only computed
    // once, at load time (M2Object::createAABB). Re-placement doesn't recompute it, so
    // shift the existing box by the placement delta here (exact for translation,
    // conservative for rotation/scale). Unloaded objects have no box yet; theirs is
    // computed when the model finishes loading.
    if (updateAABB && object.m2Object->getHasBoundingBox()) {
        mathfu::mat4 delta = object.m2Object->getModelMatrix() * object.lastPlacementMatrix.Inverse();
        const CAaBox &prevBox = object.m2Object->getAABB();
        object.m2Object->setAABB(MathHelper::transformAABBWithMat4(
            delta,
            mathfu::vec4(prevBox.min.x, prevBox.min.y, prevBox.min.z, 1.0f),
            mathfu::vec4(prevBox.max.x, prevBox.max.y, prevBox.max.z, 1.0f)));
    }

    object.lastPlacementMatrix = object.m2Object->getModelMatrix();
}

int WorldObjectManager::addCustomM2(const std::string &name, const std::string &modelFileName, int fileDataId,
                                    const mathfu::vec3 &position, const mathfu::vec3 &rotationEulerDeg,
                                    const mathfu::vec3 &scale) {
    if (m_api == nullptr) return -1;
    if (modelFileName.empty() && fileDataId <= 0) return -1;

    int id = m_nextCustomId++;
    auto &object = m_customM2s[id];
    object.id = id;
    object.name = name.empty() ? ("Custom M2 " + std::to_string(id)) : name;
    object.modelFileName = modelFileName;
    object.fileDataId = fileDataId;
    object.position = position;
    object.rotationEulerDeg = rotationEulerDeg;
    object.scale = scale;

    spawnCustomM2(object);
    m_customIdByM2Id[object.m2Object->getObjectId()] = id;

    return id;
}

bool WorldObjectManager::removeCustomM2(int id) {
    auto it = m_customM2s.find(id);
    if (it == m_customM2s.end()) return false;

    if (it->second.m2Object) {
        m_customIdByM2Id.erase(it->second.m2Object->getObjectId());
    }
    m_customM2s.erase(it); // drops the strong ref; pool slot release is deferred by the factory
    return true;
}

void WorldObjectManager::clearCustomM2s() {
    m_customM2s.clear();
    m_customIdByM2Id.clear();
}

bool WorldObjectManager::setCustomM2Transform(int id, const mathfu::vec3 &position,
                                              const mathfu::vec3 &rotationEulerDeg, const mathfu::vec3 &scale) {
    auto it = m_customM2s.find(id);
    if (it == m_customM2s.end()) return false;

    auto &object = it->second;
    object.position = position;
    object.rotationEulerDeg = rotationEulerDeg;
    object.scale = scale;

    applyCustomM2Placement(object, true);
    return true;
}

const CustomM2Object *WorldObjectManager::getCustomM2(int id) const {
    auto it = m_customM2s.find(id);
    return it != m_customM2s.end() ? &it->second : nullptr;
}

int WorldObjectManager::getCustomM2IdByM2ObjId(M2ObjId id) const {
    auto it = m_customIdByM2Id.find(id);
    return it != m_customIdByM2Id.end() ? it->second : -1;
}

void WorldObjectManager::cullAndCollect(M2ObjectListContainer &m2CandidatesOut) {
    // Submit every custom M2 and let Map's exact per-object culling (and load-state
    // routing) sort them out.
    for (auto &[id, customObject] : m_customM2s) {
        if (customObject.m2Object) {
            m2CandidatesOut.addCandidate(customObject.m2Object->getObjectId());
        }
    }
}
