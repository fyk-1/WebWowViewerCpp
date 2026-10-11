//
// Created by Deamon on 7/15/2026.
//

#ifndef AWEBWOWVIEWERCPP_PLACEDCREATURE_H
#define AWEBWOWVIEWERCPP_PLACEDCREATURE_H

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <vector>
#include "../m2/m2Object.h"
#include "../../../include/config.h"

// Where a creature draws. Pending hides it until every WMO that may hold it has answered; Exterior is the map's
// exterior candidates, as for any M2 standing in the open; Interior is the candidates of the WMO group it stands in.
enum class CreatureHome : uint8_t {
    Pending,
    Exterior,
    Interior,
};

// A creature of a CreatureSpawn row, or a game object of a GameObjectSpawn row, which is placed, homed and drawn the same way. The
// tile or the map that built it holds the only strong refs, and the WMO groups it stands in hold weak ones, so it goes with its
// owner. WorldObjectManager::homeCreature sets the WMOs asking about it, and each WmoObject answers once its groups are loaded
// (WmoObject::homeCreatures).
struct PlacedCreature {
    std::shared_ptr<M2Object> m2;
    M2ObjId id;
    // Dressed by a race and sex model, its row carrying a body texture. Config::renderHumanoids hides these alone.
    bool humanoid = false;
    // From a GameObjectSpawn row. Config::renderGameObjectSpawns shows these alone, where renderCreatures shows the creatures.
    bool gameObject = false;
    // The signed world event ids of the spawn's row, empty for a spawn no event ties. Config::activeSpawnEvents decides which count.
    std::vector<int> events;
    std::atomic<CreatureHome> home{CreatureHome::Pending};
    std::atomic<int> wmosToAnswer{0};

    bool shown(const Config &config) const {
        if (!inActiveEvents(config)) return false;
        if (gameObject) return config.renderGameObjectSpawns;
        return config.renderCreatures && (config.renderHumanoids || !humanoid);
    }

    // A positive event shows the spawn while it runs, and any one of several does. A negative event hides it while that event runs.
    bool inActiveEvents(const Config &config) const {
        if (events.empty()) return true;

        std::shared_lock lock(config.activeSpawnEventsLock);
        bool tied = false, running = false;
        for (int event : events) {
            bool active = config.activeSpawnEvents.count(event < 0 ? -event : event) > 0;
            if (event < 0 && active) return false;
            if (event > 0) {
                tied = true;
                running = running || active;
            }
        }
        return !tied || running;
    }

    // A WMO that holds the creature in no interior group of its own. The last to answer sends a creature no WMO took outside.
    void declinedByWmo() {
        if (--wmosToAnswer > 0) return;

        CreatureHome pending = CreatureHome::Pending;
        home.compare_exchange_strong(pending, CreatureHome::Exterior);
    }
};

#endif //AWEBWOWVIEWERCPP_PLACEDCREATURE_H
