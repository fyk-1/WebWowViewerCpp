//
// Created by Deamon on 7/15/2026.
//

#ifndef AWEBWOWVIEWERCPP_WORLDOBJECTMANAGER_H
#define AWEBWOWVIEWERCPP_WORLDOBJECTMANAGER_H

#include <map>
#include <unordered_map>
#include <vector>
#include "../m2/m2Object.h"
#include "../wmo/wmoObject.h"
#include "../../../include/database/dbStructs.h"
#include "PlacedCreature.h"

class WmoObject;

#include "../../ApiContainer.h"

// A user-placed custom M2 (not DB-driven). The manager owns the strong ref to the
// pooled M2Object; dropping it (removal / manager destruction) releases the pool slot
// through the factory's deferred deallocation, so no explicit deregistration is needed.
struct CustomM2Object {
    int id = -1;                    // stable manager-local id
    std::string name;               // user-facing label
    std::string modelFileName;      // CASC path; when empty, fileDataId is used instead
    int fileDataId = 0;
    mathfu::vec3 position = {0, 0, 0};
    mathfu::vec3 rotationEulerDeg = {0, 0, 0}; // degrees, applied as RotX * RotY * RotZ
    mathfu::vec3 scale = {1, 1, 1};

    std::shared_ptr<M2Object> m2Object = nullptr;
    // Placement matrix from the last applied transform; used to shift the (accurate,
    // load-time-computed) world AABB by the placement delta on subsequent moves.
    mathfu::mat4 lastPlacementMatrix = mathfu::mat4::Identity();
};

// Holds client-side world objects for a single map: the user-placed custom M2s, and the
// static factories for the creatures and game objects the spawn rows place. Created once
// per opened map and handed to Map, which asks it to contribute its candidates into the
// frame's exterior culling pass every frame (Map's existing M2/WMO candidate->drawn
// machinery does the exact per-object test and load-state routing from there, same as
// ADT-placed doodads).
class WorldObjectManager {
public:
    WorldObjectManager(HApiContainer api, int mapId);

    // Adds the custom M2s as candidates into m2CandidatesOut (e.g. exteriorView->m2List),
    // mirroring how AdtObject registers ADT-placed doodads as candidates for the same container.
    void cullAndCollect(M2ObjectListContainer &m2CandidatesOut);

    // The creature a CreatureSpawn row places: its model at the row's world position, turned about z by its facing and at its scale,
    // with the row's textures in the model's creature-skin slots 11 to 13. A batch whose first texture slot stays empty, or is a type
    // the row cannot fill, is left out of the draw. The models the row says it wears hang on the creature's attachment points
    // (M2Object::attach) and are drawn with it. The row's Anim is the animation the model starts in. A row with a mount places the mount
    // there instead, on its default animation, and hangs the creature on its attachment point MountAttachment; the creature's own
    // placement is then the mount's, so the pair is the one PlacedCreature, and its id and its box are the mount's. Null for a row with no model.
    // The caller owns it, homes it (homeCreature) and takes it into the exterior candidates of the map or tile it belongs to once its home
    // is Exterior.
    static std::shared_ptr<PlacedCreature> createCreature(HApiContainer api, const CreatureSpawnRecord &spawn);
    // The game object a GameObjectSpawn row places: its model at the row's world position, turned by its quaternion and at its scale,
    // as it stands with nothing replaced and nothing hidden but the batches whose first texture nothing fills, in the animation the row's
    // Anim names. Null for a row with no model. The caller owns it and homes it as a creature (homeCreature).
    static std::shared_ptr<PlacedCreature> createGameObjectSpawn(HApiContainer api, const GameObjectSpawnRecord &spawn);
    // The WMO a game object places: its model at the row's world position, turned by its quaternion and at its scale. The box is the
    // model's geometry box in model space, which the WMO keeps until its own load is done and then replaces with its header's. The
    // callers pass an empty one: a WMO that is not loaded is loaded whenever it is a candidate, whatever its box. Null for a row with no
    // model. The caller owns it. AdtObject and Map place the rows with IsWmo set through this.
    static std::shared_ptr<WmoObject> createGameObjectWmo(HApiContainer api, const GameObjectSpawnRecord &spawn,
                                                          const std::array<float, 3> &geoBoxMin, const std::array<float, 3> &geoBoxMax);
    // Hands the creature, standing at the position, to the WMOs it may stand in, which decide in which interior group, if any, it does
    // (WmoObject::offerCreature). With onlyWhereBounded the WMOs are those whose bounding box holds the position, and a creature in none
    // of them is Exterior at once.
    static void homeCreature(const std::shared_ptr<PlacedCreature> &creature, const std::array<float, 3> &position,
                             const std::vector<std::shared_ptr<WmoObject>> &wmos, bool onlyWhereBounded);
    // The M2 attachment point of a mount a rider hangs on.
    static constexpr int MountAttachment = 0;

    // ---------------- Custom user-placed M2s ----------------
    // Custom M2s are submitted as exterior M2 candidates every frame; Map's per-object
    // culling then does the exact frustum test. This keeps add/remove/move cheap.

    // Spawns a new custom M2 and returns its manager-local id, or -1 on invalid input
    // (no api, or neither a model file name nor a valid FileDataID given).
    int addCustomM2(const std::string &name, const std::string &modelFileName, int fileDataId,
                    const mathfu::vec3 &position, const mathfu::vec3 &rotationEulerDeg,
                    const mathfu::vec3 &scale);
    bool removeCustomM2(int id);
    void clearCustomM2s();

    // Re-applies position/rotation/scale of a custom M2 and updates its culling AABB.
    bool setCustomM2Transform(int id, const mathfu::vec3 &position,
                              const mathfu::vec3 &rotationEulerDeg, const mathfu::vec3 &scale);

    const CustomM2Object *getCustomM2(int id) const;
    const std::map<int, CustomM2Object> &getCustomM2s() const { return m_customM2s; }

    // Reverse lookup from a pooled M2ObjId (e.g. GPU pick result) to the custom object
    // id, or -1 if the picked M2 is not a custom-placed one.
    int getCustomM2IdByM2ObjId(M2ObjId id) const;

private:
    void spawnCustomM2(CustomM2Object &object);
    void applyCustomM2Placement(CustomM2Object &object, bool updateAABB);

    HApiContainer m_api;
    int m_mapId;

    std::map<int, CustomM2Object> m_customM2s;
    std::unordered_map<M2ObjId, int> m_customIdByM2Id;
    int m_nextCustomId = 1;
};
typedef std::shared_ptr<WorldObjectManager> HWorldObjectManager;

#endif //AWEBWOWVIEWERCPP_WORLDOBJECTMANAGER_H
