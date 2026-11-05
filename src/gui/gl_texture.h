#pragma once

#include <SDL3/SDL_opengl.h>
#include <vector>

namespace gfx {
// UI actions can replace a preview after its texture was added to a draw list.
// Keep those names alive until all draw lists for the frame have been rendered.
inline std::vector<GLuint> retiredTextures;
inline void RetireTexture(GLuint texture) {
    if (texture) retiredTextures.push_back(texture);
}
inline void FlushRetiredTextures() {
    if (retiredTextures.empty()) return;
    glDeleteTextures(static_cast<GLsizei>(retiredTextures.size()), retiredTextures.data());
    retiredTextures.clear();
}
}
