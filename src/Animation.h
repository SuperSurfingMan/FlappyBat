#pragma once
#include <vector>
#include <memory>
#include "Texture2D.h"
#include <functional>
#include "Math.h"

class Animation {
public:
    enum class ClipDirection {
        Row,    // 先横向（行优先）
        Column  // 先纵向（列优先）
    };

    Animation(bool _loop = true, bool _emptyWhenDone = false) : loop(_loop), emptyWhenDone(_emptyWhenDone) {}

    void SetAnimationProperties(bool _loop = true, bool _emptyWhenDone = false);

    void AddFrame(std::shared_ptr<Texture2D> tex, FrameUV frame_uv = FrameUV::Zero());
    void AddFrameBatch(const std::vector<std::string>& paths, bool flipY = false);
    void AddFrameFromFolder(const std::string& folderPath, bool flipY = false);
    
    void SetFrameTime(float secondsPerFrame);
    void SetLoop(bool loop);
    void SetEmptyWhenDone(bool empty);

    // dt：每帧时间（秒）
    void Update(float dt);

    std::shared_ptr<Texture2D> GetCurrentFrame() const;

    void Reset();

    void setOnAnimationFinished(std::function<void()> callback);

    std::shared_ptr<Texture2D> GetSpriteSheet() const;
    FrameUV GetCurrentFrameUV() const;

    void InitAnimationUV(std::shared_ptr<Texture2D> spriteSheet, size_t frameCount);
    void InitAnimationUV(std::shared_ptr<Texture2D> spriteSheet, std::vector<FrameUV> framesUV);
    void InitAnimationUV(std::shared_ptr<Texture2D> spriteSheet, size_t rows, size_t columns);

    bool IsFinished() const;

    bool IsFrameListEmpty();
    void MakeFrameUVSyncWithFrameList01();

private:
    std::vector<std::shared_ptr<Texture2D>> frames;
    float frameTime = 0.1f;   // 每帧 0.1 秒
    float timer = 0.0f;
    size_t currentFrame = 0;

    bool loop = true;
    bool emptyWhenDone = false; // 可选：动画结束后是否显示空白帧
    bool finished = false;      // 可选：动画是否已完成（非循环时）

    // 动画结束的回调函数
    std::function<void()> onAnimationFinished = nullptr;

    // 公共SpriteSheet
    std::shared_ptr<Texture2D> spriteSheet;

    // frames数组存储uv坐标点
    std::vector<FrameUV> frames_uv;
};
