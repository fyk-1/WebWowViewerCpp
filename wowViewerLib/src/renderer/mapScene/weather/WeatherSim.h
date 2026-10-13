//
// Rain, snow and storm around the camera: the particles and the lightning of WoWee, simulated on the CPU.
//
// Ported from WoWee's rendering/weather.cpp and rendering/lightning.cpp. Copyright (c) 2026 Wowee Contributors, under the
// licence in shaders/slang/common/LICENSE-WoWee.txt.
//

#ifndef AWEBWOWVIEWERCPP_WEATHERSIM_H
#define AWEBWOWVIEWERCPP_WEATHERSIM_H

#include <random>
#include <vector>
#include "mathfu/glsl_mappings.h"

// robots@Impl: WoWee's numbers are kept and its axes are turned to the viewer's, z up and in yards.
//
// Particles:
// - They follow the camera. They spawn SpawnHeight above it, SpawnVolume yards to each side, and respawn there once they
//   leave that radius, outlive their lifetime or sink SinkLimit yards below it.
// - The intensity sets the share of MaxParticles that fall. A particle added while the count grows starts at a random
//   height, where WoWee started it at the top, so a rise of the intensity does not drop a burst.
// - Snow drifts on a sine in x and a cosine in y, and a storm falls slanted with a gust over x.
//
// Lightning, in a storm only:
// - A strike comes every MinStrikeInterval seconds at full intensity and every MaxStrikeInterval at none, each interval
//   varied by a fifth either way. It lands at a random bearing 50 to 200 yards off, starts 80 to 150 yards above the camera
//   and runs 100 to 200 yards down.
// - A bolt is a chain of 8 to 15 jagged links whose offsets taper to nothing at the foot. Each link may fork a branch, a
//   chain of its own, to three deep, and a bolt keeps at most MaxBoltLinks.
// - WoWee took the offset axes from the cross product of the bolt and the up axis, which is the zero vector for a bolt
//   straight down. Here the axes fall back to x for a near-vertical bolt.
// - A strike also starts a flash that fades quadratically over FlashLifetime.
//
// The camera in a WMO interior stops all of it: the particles and bolts are dropped, and a returning camera starts afresh.
class WeatherSim {
public:
    enum Type { Clear = 0, Rain = 1, Snow = 2, Storm = 3 };

    static constexpr int MaxParticles = 2000;
    static constexpr int MaxBolts = 3;
    static constexpr int MaxBoltLinks = 256;

    struct Particle {
        mathfu::vec3 position;
        mathfu::vec3 velocity;
        float lifetime;
        float maxLifetime;
    };

    // What a particle of the current weather looks like: WoWee's colour and size in pixels, and the seconds of fall a streak spans,
    // 0 for the round dot WoWee draws. Rain and storm draw streaks, which are the dot stretched along the fall.
    struct Look {
        mathfu::vec4 color;
        float size;
        float streakSeconds;
    };

    struct Link {
        mathfu::vec3 from;
        mathfu::vec3 to;
    };

    struct Bolt {
        bool active = false;
        float lifetime = 0;
        float brightness = 1;
        std::vector<Link> links;
    };

    // Moves the weather on by `seconds`. `type` is a Type, any other value counting as Clear, and `intensity` runs 0 to 1.
    void update(const mathfu::vec3 &camera, float seconds, int type, float intensity, bool indoors);

    const std::vector<Particle> &particles() const { return m_particles; }
    const std::vector<Bolt> &bolts() const { return m_bolts; }
    float flash() const { return m_flash; }
    int type() const { return m_type; }
    Look look() const;

private:
    static constexpr float SpawnVolume = 100;
    static constexpr float SpawnHeight = 80;
    static constexpr float SinkLimit = 20;
    static constexpr float MinStrikeInterval = 2;
    static constexpr float MaxStrikeInterval = 8;
    static constexpr float BoltLifetime = 0.15f;
    static constexpr float FlashLifetime = 0.3f;
    static constexpr float BranchChance = 0.3f;

    float random(float low, float high) { return low + std::uniform_real_distribution<float>(0, 1)(m_rng) * (high - low); }
    void spawn(const mathfu::vec3 &camera, float heightShare);
    void respawn(Particle &particle, const mathfu::vec3 &camera, float heightShare);
    void move(Particle &particle, const mathfu::vec3 &camera, float seconds);
    void updateLightning(const mathfu::vec3 &camera, float seconds, float intensity);
    void strike(const mathfu::vec3 &camera);
    void chain(const mathfu::vec3 &from, const mathfu::vec3 &to, int depth, std::vector<Link> &links);
    void stopLightning();

    std::mt19937 m_rng{std::random_device{}()};
    int m_type = Clear;
    std::vector<Particle> m_particles;
    std::vector<Bolt> m_bolts = std::vector<Bolt>(MaxBolts);
    float m_flash = 0;
    float m_flashLifetime = 0;
    float m_strikeTimer = 0;
    float m_nextStrike = 0;
};

#endif //AWEBWOWVIEWERCPP_WEATHERSIM_H
