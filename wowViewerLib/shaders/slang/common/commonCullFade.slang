#ifndef COMMON_CULL_FADE_SLANG
#define COMMON_CULL_FADE_SLANG

import glsl;

// The screen-door of Config::m2CullFade, shared by every fade that dissolves a thing in the scene: keeps the share `fade` of the
// fragments, in a pattern fixed to the screen (interleaved gradient noise). The M2 shader drops a fading model's fragments by
// it, and the far terrain's pass takes the fragments that this drops when the full scene fades into the far land, so both
// leave the same pixels to each other.
bool dropsForCullFade(float fade, vec2 fragCoord) {
    return fade < 1.0 && fract(52.9829189 * fract(dot(fragCoord, vec2(0.06711056, 0.00583715)))) >= fade;
}

#endif
