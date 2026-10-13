//
// Ported from WoWee's rendering/weather.cpp and rendering/lightning.cpp. Copyright (c) 2026 Wowee Contributors, under the
// licence in shaders/slang/common/LICENSE-WoWee.txt. WeatherSim.h has the mechanism.
//

#include "WeatherSim.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
    // A snow flake falls SpawnHeight + SinkLimit yards at 5 yards a second. WoWee's 10 s lifetime ended it 30 yards above the
    // camera, so the flakes never came down to it.
    constexpr float SnowLifetime = 20;
    constexpr float RainLifetime = 5;
    constexpr float StormLifetime = 3.5f;
    constexpr float Turn = 6.2831853f;
    constexpr float StreakSeconds = 0.02f;
}

void WeatherSim::update(const mathfu::vec3 &camera, float seconds, int type, float intensity, bool indoors) {
    // A long frame must not carry particles across the whole volume.
    seconds = std::min(seconds, 0.1f);
    intensity = std::clamp(intensity, 0.0f, 1.0f);

    if (type < Rain || type > Storm || indoors) {
        m_type = Clear;
        m_particles.clear();
        stopLightning();
        return;
    }

    if (type != m_type) {
        m_type = type;
        m_particles.clear();
    }

    const int target = (int) (MaxParticles * intensity);
    while ((int) m_particles.size() < target) spawn(camera, random(0, 1));
    if ((int) m_particles.size() > target) m_particles.resize(target);

    for (auto &particle : m_particles) move(particle, camera, seconds);

    if (m_type == Storm) updateLightning(camera, seconds, intensity);
    else stopLightning();
}

WeatherSim::Look WeatherSim::look() const {
    if (m_type == Rain) return {mathfu::vec4(0.7f, 0.8f, 0.9f, 0.6f), 3, StreakSeconds};
    if (m_type == Storm) return {mathfu::vec4(0.6f, 0.65f, 0.75f, 0.7f), 3.5f, StreakSeconds};
    return {mathfu::vec4(1.0f, 1.0f, 1.0f, 0.9f), 8, 0};
}

void WeatherSim::respawn(Particle &particle, const mathfu::vec3 &camera, float heightShare) {
    particle.position = mathfu::vec3(camera.x + random(-1, 1) * SpawnVolume,
                                     camera.y + random(-1, 1) * SpawnVolume,
                                     camera.z + SpawnHeight * heightShare);
    particle.lifetime = 0;
}

void WeatherSim::spawn(const mathfu::vec3 &camera, float heightShare) {
    Particle particle;
    if (m_type == Rain) {
        particle.velocity = mathfu::vec3(0, 0, -50);
        particle.maxLifetime = RainLifetime;
    } else if (m_type == Storm) {
        particle.velocity = mathfu::vec3(15, 8, -70);
        particle.maxLifetime = StormLifetime;
    } else {
        particle.velocity = mathfu::vec3(0, 0, -5);
        particle.maxLifetime = SnowLifetime;
    }
    respawn(particle, camera, heightShare);
    // Flakes that start together would sway together.
    particle.lifetime = random(0, particle.maxLifetime * 0.5f);
    m_particles.push_back(particle);
}

void WeatherSim::move(Particle &particle, const mathfu::vec3 &camera, float seconds) {
    particle.lifetime += seconds;

    const mathfu::vec3 toCamera = particle.position - camera;
    const float distanceSquared = mathfu::vec3::DotProduct(toCamera, toCamera);
    if (particle.lifetime >= particle.maxLifetime || distanceSquared > SpawnVolume * SpawnVolume ||
        particle.position.z < camera.z - SinkLimit) {
        respawn(particle, camera, 1);
    }

    if (m_type == Snow) {
        particle.velocity.x = std::sin(particle.lifetime * 0.5f) * 2;
        particle.velocity.y = std::cos(particle.lifetime * 0.3f) * 2;
    } else if (m_type == Storm) {
        const float gust = std::sin(particle.lifetime * 1.5f + particle.position.x * 0.1f) * 5;
        particle.velocity.x = 15 + gust;
        particle.velocity.y = 8 + std::cos(particle.lifetime * 2) * 3;
    }

    particle.position += particle.velocity * seconds;
}

void WeatherSim::updateLightning(const mathfu::vec3 &camera, float seconds, float intensity) {
    if (m_nextStrike <= 0) m_nextStrike = random(MinStrikeInterval, MaxStrikeInterval);
    m_strikeTimer += seconds;
    if (m_strikeTimer >= m_nextStrike) {
        strike(camera);
        m_strikeTimer = 0;
        const float interval = MinStrikeInterval + (MaxStrikeInterval - MinStrikeInterval) * (1 - intensity);
        m_nextStrike = random(interval * 0.8f, interval * 1.2f);
    }

    for (auto &bolt : m_bolts) {
        if (!bolt.active) continue;
        bolt.lifetime += seconds;
        if (bolt.lifetime >= BoltLifetime) {
            bolt.active = false;
            bolt.links.clear();
        } else {
            bolt.brightness = 1 - bolt.lifetime / BoltLifetime;
        }
    }

    if (m_flash > 0) {
        m_flashLifetime += seconds;
        const float t = m_flashLifetime / FlashLifetime;
        m_flash = t >= 1 ? 0 : 1 - t * t;
    }
}

void WeatherSim::strike(const mathfu::vec3 &camera) {
    auto bolt = std::find_if(m_bolts.begin(), m_bolts.end(), [](const Bolt &b) { return !b.active; });
    if (bolt == m_bolts.end()) return;

    const float bearing = random(0, Turn);
    const float distance = random(50, 200);
    const mathfu::vec3 start(camera.x + std::cos(bearing) * distance,
                             camera.y + std::sin(bearing) * distance,
                             camera.z + random(80, 150));
    const mathfu::vec3 end(start.x, start.y, start.z - random(100, 200));

    bolt->active = true;
    bolt->lifetime = 0;
    bolt->brightness = 1;
    bolt->links.clear();
    chain(start, end, 0, bolt->links);

    m_flash = 1;
    m_flashLifetime = 0;
}

void WeatherSim::chain(const mathfu::vec3 &from, const mathfu::vec3 &to, int depth, std::vector<Link> &links) {
    mathfu::vec3 direction = to - from;
    const float length = direction.Length();
    if (length < 1e-3f) return;
    direction = direction / length;

    mathfu::vec3 first = mathfu::vec3::CrossProduct(direction, mathfu::vec3(0, 0, 1));
    if (first.Length() < 1e-3f) first = mathfu::vec3::CrossProduct(direction, mathfu::vec3(1, 0, 0));
    first = first.Normalized();
    const mathfu::vec3 second = mathfu::vec3::CrossProduct(direction, first).Normalized();

    // The main chain is laid whole before its branches, so the cap never cuts into it.
    std::vector<std::pair<mathfu::vec3, mathfu::vec3>> branches;
    const int count = 8 + (int) random(0, 8);
    mathfu::vec3 current = from;
    for (int i = 1; i < count && (int) links.size() < MaxBoltLinks; i++) {
        const float t = (float) i / (float) count;
        const float spread = (1 - t) * 8;
        const mathfu::vec3 next = from + direction * (length * t) +
                                  first * random(-spread, spread) + second * random(-spread, spread);
        links.push_back({current, next});
        current = next;

        if (depth < 3 && random(0, 1) < BranchChance) {
            branches.push_back({current, current + mathfu::vec3(random(-20, 20), random(-20, 20), random(-30, -10))});
        }
    }
    if ((int) links.size() < MaxBoltLinks) links.push_back({current, to});

    for (const auto &[branchFrom, branchTo] : branches) chain(branchFrom, branchTo, depth + 1, links);
}

void WeatherSim::stopLightning() {
    for (auto &bolt : m_bolts) {
        bolt.active = false;
        bolt.links.clear();
    }
    m_flash = 0;
    m_strikeTimer = 0;
    m_nextStrike = 0;
}
