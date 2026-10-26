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
// - They follow the camera. A particle's position is an offset from the camera, and each update first subtracts the camera's
//   move since the last one, so a drop stays where it is in the world. The vertex shader adds the camera of the frame it draws,
//   since the camera the plan was made with and the one drawing differ by a frame, and an absolute position would swim by that
//   much. They spawn SpawnHeight above the camera, SpawnVolume yards to each side, and respawn there once they leave that
//   radius, outlive their lifetime or sink SinkLimit yards below it. While the camera moves, a respawn lands in the half of the
//   volume the camera is heading into, so the drops it flies into are not thinner than the ones behind it, and one the camera
//   outran comes back at a random height, since a camera faster than the fall would otherwise fly under a column refilled
//   from the top alone. The first update after a change of weather or an interior takes no move.
// - The intensity sets the share of MaxParticles that fall. A particle added while the count grows starts at a random
//   height, where WoWee started it at the top, so a rise of the intensity does not drop a burst.
// - Snow drifts on a sine in x and a cosine in y, and a storm falls slanted with a gust over x.
//
// Lightning, in a storm only:
// - Each tick a strike falls with probability seconds * intensity * kStrikesPerSecond, so the gaps are exponential and a full
//   storm averages one strike in twenty seconds. A strike is followed, with probability kFollowUpChance, by another 0.2 to 1 s
//   later with no roll, since a real storm clusters. A strike lands at a random bearing 50 to 200 yards off, starts 80 to 150 yards above
//   the camera and runs 100 to 200 yards down.
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
    // 0 for the round dot WoWee draws. Rain and storm draw streaks, which are the dot stretched along the fall. A particle
    // drawn with a picture takes the picture's colours, so its colour is white at WoWee's alpha.
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
    Look look(bool textured) const;

private:
    static constexpr float SpawnVolume = 100;
    static constexpr float SpawnHeight = 80;
    static constexpr float SinkLimit = 20;
    static constexpr float kStrikesPerSecond = 0.05f;
    static constexpr float kFollowUpChance = 1.0f / 3.0f;
    static constexpr float BoltLifetime = 0.15f;
    static constexpr float FlashLifetime = 0.3f;
    static constexpr float BranchChance = 0.3f;

    float random(float low, float high) { return low + std::uniform_real_distribution<float>(0, 1)(m_rng) * (high - low); }
    void spawn(float heightShare);
    void respawn(Particle &particle, float heightShare, bool ahead);
    void move(Particle &particle, float seconds);
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
    // The camera of the last update, and the direction it moved in on the ground, zero while it stood still.
    mathfu::vec3 m_lastCamera = mathfu::vec3(0, 0, 0);
    bool m_haveCamera = false;
    mathfu::vec2 m_heading = mathfu::vec2(0, 0);
    // Seconds until the follow-up strike of the last one, 0 when none is due.
    float m_followUpIn = 0;
};

#endif //AWEBWOWVIEWERCPP_WEATHERSIM_H
