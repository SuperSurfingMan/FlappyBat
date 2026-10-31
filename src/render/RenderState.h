#pragma once

#include <glad/glad.h>

// 混合模式描述
struct BlendMode
{
    GLenum src = GL_SRC_ALPHA;
    GLenum dst = GL_ONE_MINUS_SRC_ALPHA;

    static BlendMode Alpha()    { return {GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA}; }
    static BlendMode Additive() { return {GL_SRC_ALPHA, GL_ONE}; }
};

// 混合状态作用域守卫：构造时应用指定混合模式，析构时恢复原状态。
// 用于封装"谁修改 GL 状态，谁负责恢复"，避免状态跨渲染模块泄漏。
class BlendScope
{
public:
    explicit BlendScope(BlendMode mode);
    ~BlendScope();

    BlendScope(const BlendScope&) = delete;
    BlendScope& operator=(const BlendScope&) = delete;

private:
    GLint prevSrc_ = GL_SRC_ALPHA;
    GLint prevDst_ = GL_ONE_MINUS_SRC_ALPHA;
};
