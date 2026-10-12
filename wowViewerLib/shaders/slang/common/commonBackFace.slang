#ifndef COMMON_BACK_FACE_SLANG
#define COMMON_BACK_FACE_SLANG

import glsl;

// The normal of the side being drawn. A batch that Config::drawBackFaces opened up lights its back faces as their own
// side, not as the front they belong to.
vec3 backFaceNormal(in vec3 normal, in bool backFacesDrawn) {
    return (backFacesDrawn && !gl_FrontFacing) ? -normal : normal;
}

#endif //COMMON_BACK_FACE_SLANG
