#include "render/RenderState.h"

BlendScope::BlendScope(BlendMode mode)
{
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrc_);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDst_);

    glEnable(GL_BLEND);
    glBlendFunc(mode.src, mode.dst);
}

BlendScope::~BlendScope()
{
    glBlendFunc(prevSrc_, prevDst_);
}
