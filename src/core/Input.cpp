#include "core/Input.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

float Input::mouseX = 0.0f;
float Input::mouseY = 0.0f;

bool Input::mousePressed[MouseButtonCount] = { false };
bool Input::mouseDown[MouseButtonCount] = { false };
bool Input::mouseReleased[MouseButtonCount] = { false };

bool Input::keyPressed[KeyCount] = { false };
bool Input::keyDown[KeyCount] = { false };
bool Input::keyReleased[KeyCount] = { false };

void Input::Update()
{
    GLFWwindow* window = glfwGetCurrentContext();
    if (!window) return;

    // Reset one-frame flags
    for (int i = 0; i < MouseButtonCount; i++) {
        mousePressed[i] = false;   // 修正：必须重置
        mouseReleased[i] = false;
    }
    for (int i = 0; i < KeyCount; i++) {
        keyPressed[i] = false;
        keyReleased[i] = false;
    }

    // Update mouse position
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    mouseX = static_cast<float>(x);
    mouseY = static_cast<float>(y);

    // Update mouse button states
    for (int i = 0; i < MouseButtonCount; i++) {
        int state = glfwGetMouseButton(window, i);

        if (state == GLFW_PRESS) {
            if (!mouseDown[i]) {
                mousePressed[i] = true;
            }
            mouseDown[i] = true;
        }
        else {
            if (mouseDown[i]) {
                mouseReleased[i] = true;
            }
            mouseDown[i] = false;
        }
    }

    // Update keyboard states（与鼠标同样的边沿检测逻辑）
    for (int i = 0; i < KeyCount; i++) {
        int state = glfwGetKey(window, i);

        if (state == GLFW_PRESS) {
            if (!keyDown[i]) {
                keyPressed[i] = true;
            }
            keyDown[i] = true;
        }
        else {
            if (keyDown[i]) {
                keyReleased[i] = true;
            }
            keyDown[i] = false;
        }
    }
}

float Input::GetMouseX()
{
    return mouseX;
}

float Input::GetMouseY()
{
    return mouseY;
}

bool Input::IsMousePressed(int button)
{
    if (button < 0 || button >= MouseButtonCount)
        return false;

    return mousePressed[button];
}

bool Input::IsMouseDown(int button)
{
    if (button < 0 || button >= MouseButtonCount)
        return false;

    return mouseDown[button];
}

bool Input::IsMouseReleased(int button)
{
    if (button < 0 || button >= MouseButtonCount)
        return false;

    return mouseReleased[button];
}

bool Input::IsKeyPressed(int key)
{
    if (key < 0 || key >= KeyCount)
        return false;

    return keyPressed[key];
}

bool Input::IsKeyDown(int key)
{
    if (key < 0 || key >= KeyCount)
        return false;

    return keyDown[key];
}

bool Input::IsKeyReleased(int key)
{
    if (key < 0 || key >= KeyCount)
        return false;

    return keyReleased[key];
}

Vec2 Input::GetCursorPosition()
{
    return Vec2(mouseX, mouseY);
}