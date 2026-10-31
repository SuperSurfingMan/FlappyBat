#pragma once
#include "core/Point2D.h"

// 不让 glfw3.h 拉入系统 GL 头（GL 函数统一由 glad 提供），
// 从而 Input.h 可被任意顺序包含而不与 glad 冲突
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

class Input {
public:
    static void Update();

    static float GetMouseX();
    static float GetMouseY();

    static bool IsMousePressed(int button);
    static bool IsMouseDown(int button);
    static bool IsMouseReleased(int button);

    // ===== 键盘（key 为 GLFW_KEY_*）=====
    static bool IsKeyPressed(int key);    // 本帧按下
    static bool IsKeyDown(int key);       // 持续按住
    static bool IsKeyReleased(int key);   // 本帧释放

    static Point2D GetCursorPosition();
private:
    static constexpr int MouseButtonCount = 3;
    static constexpr int KeyCount = GLFW_KEY_LAST + 1;

    static float mouseX;
    static float mouseY;

    static bool mousePressed[MouseButtonCount];
    static bool mouseDown[MouseButtonCount];
    static bool mouseReleased[MouseButtonCount];

    static bool keyPressed[KeyCount];
    static bool keyDown[KeyCount];
    static bool keyReleased[KeyCount];
};