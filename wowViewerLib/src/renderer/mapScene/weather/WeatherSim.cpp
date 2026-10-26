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
    // The raindrop picture is a 16-texel-wide strip with the drop over 5 of them: on WoWee's 3-pixel streak it came out a pixel wide.
    constexpr float TexturedStreakWidth = 10;
}

void WeatherSim::update(const mathfu::vec3 &camera, float seconds, int type, float intensity, bool indoors) {
    // A long frame must not carry particles across the whole volume.
    seconds = std::min(seconds, 0.1f);
    intensity = std::clamp(intensity, 0.0f, 1.0f);

    if (type < Rain || type > Storm || indoors) {
        m_type = Clear;
        m_particles.clear();
        m_haveCamera = false;
        stopLightning();
        return;
    }

    if (type != m_type) {
        m_type = type;
        m_particles.clear();
        m_haveCamera = false;
    }

    const mathfu::vec3 delta = m_haveCamera ? camera - m_lastCamera : mathfu::vec3(0, 0, 0);
    m_lastCamera = camera;
    m_haveCamera = true;
    const float ground = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    m_heading = ground > 0.001f ? mathfu::vec2(delta.x / ground, delta.y / ground) : mathfu::vec2(0, 0);
    for (auto &particle : m_particles) particle.position -= delta;

    const int target = (int) (MaxParticles * intensity);
    while ((int) m_particles.size() < target) spawn(random(0, 1));
    if ((int) m_particles.size() > target) m_particles.resize(target);

    for (auto &particle : m_particles) move(particle, seconds);

    if (m_type == Storm) updateLightning(camera, seconds, intensity);
    else stopLightning();
}

WeatherSim::Look WeatherSim::look(bool textured) const {
    if (m_type == Rain) return {textured ? mathfu::vec4(1.0f, 1.0f, 1.0f, 0.6f) : mathfu::vec4(0.7f, 0.8f, 0.9f, 0.6f), textured ? TexturedStreakWidth : 3, StreakSeconds};
    if (m_type == Storm) return {textured ? mathfu::vec4(1.0f, 1.0f, 1.0f, 0.7f) : mathfu::vec4(0.6f, 0.65f, 0.75f, 0.7f), textured ? TexturedStreakWidth : 3.5f, StreakSeconds};
    return {mathfu::vec4(1.0f, 1.0f, 1.0f, 0.9f), 8, 0};
}

void WeatherSim::respawn(Particle &particle, float heightShare, bool ahead) {
    float x = random(-1, 1) * SpawnVolume;
    float y = random(-1, 1) * SpawnVolume;
    if (ahead && x * m_heading.x + y * m_heading.y < 0) {
        x = -x;
        y = -y;
    }
    particle.position = mathfu::vec3(x, y, SpawnHeight * heightShare);
    particle.lifetime = 0;
}

void WeatherSim::spawn(float heightShare) {
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
    respawn(particle, heightShare, false);
    // Flakes that start together would sway together.
    particle.lifetime = random(0, particle.maxLifetime * 0.5f);
    m_particles.push_back(particle);
}

void WeatherSim::move(Particle &particle, float seconds) {
    particle.lifetime += seconds;

    const float distanceSquared = mathfu::vec3::DotProduct(particle.position, particle.position);
    if (distanceSquared > SpawnVolume * SpawnVolume) {
        // Outrun by the camera: back at any height, or a moving camera's column refills from the top only.
        respawn(particle, random(0, 1), true);
    } else if (particle.lifetime >= particle.maxLifetime || particle.position.z < -SinkLimit) {
        respawn(particle, 1, true);
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
    bool fire;
    if (m_followUpIn > 0) {
        m_followUpIn -= seconds;
        fire = m_followUpIn <= 0;
    } else {
        fire = random(0, 1) < seconds * intensity * kStrikesPerSecond;
    }
    if (fire) {
        m_followUpIn = 0;
        strike(camera);
        if (random(0, 1) < kFollowUpChance) m_followUpIn = random(0.2f, 1.0f);
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
    m_followUpIn = 0;
}
