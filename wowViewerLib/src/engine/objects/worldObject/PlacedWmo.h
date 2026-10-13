#ifndef AWEBWOWVIEWERCPP_PLACEDWMO_H
#define AWEBWOWVIEWERCPP_PLACEDWMO_H

#include <memory>
#include <vector>
#include "../wmo/wmoObject.h"
#include "PlacedCreature.h"

// A game object of a GameObjectSpawn row whose model is a WMO. The tile or the map that built it holds the only strong ref, so it goes
// with its owner. It is a WMO candidate of every frame its owner is collected in, shown or hidden by Config::renderGameObjectSpawns and
// its events as a game object that is an M2 is. No creature is homed in it.
struct PlacedWmo {
    std::shared_ptr<WmoObject> wmo;
    std::vector<int> events;

    bool shown(const Config &config) const {
        return config.renderGameObjectSpawns && spawnEventsStand(events, config);
    }
};

#endif //AWEBWOWVIEWERCPP_PLACEDWMO_H
