//
// Flyover: the render Config exposed by field name, for the stdin `config` command, the route file and `--config-defaults`.
//

#ifndef AWEBWOWVIEWERCPP_FLYOVERCONFIG_H
#define AWEBWOWVIEWERCPP_FLYOVERCONFIG_H

#include <string>
#include "../../wowViewerLib/src/include/config.h"

namespace FlyoverConfig {
    // Sets one field from its text form. Returns an empty string on success, otherwise the reason, with nothing applied.
    std::string set(Config &config, const std::string &name, const std::string &value);

    // Every settable field with its value as one JSON object on one line; main.cpp prints a fresh Config's as `--config-defaults`.
    std::string dump(const Config &config);
}

#endif //AWEBWOWVIEWERCPP_FLYOVERCONFIG_H
