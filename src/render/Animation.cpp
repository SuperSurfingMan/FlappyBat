#include "render/Animation.h"
#include "core/Util.h"

#include <stb_image.h>
#include <iostream>

void Animation::AddFrame(std::shared_ptr<Texture2D> tex, FrameUV frame_uv) {
    frames.push_back(tex);
    frames_uv.push_back(frame_uv);
}

void Animation::SetFrameTime(float secondsPerFrame) {
    if (secondsPerFrame > 0.f)
        frameTime = secondsPerFrame;
}

void Animation::SetLoop(bool l) {
    loop = l;
}

void Animation::SetEmptyWhenDone(bool empty) {
    emptyWhenDone = empty;
}

void Animation::AddFrameBatch(const std::vector<std::string>& paths, bool flipY) {
    for (const auto& path : paths) {
        auto tex = std::make_shared<Texture2D>();
        if (tex->LoadFromFile(path, flipY)) {
            frames.push_back(tex);
        }
    }
}

void Animation::AddFrameFromFolder(const std::string& folderPath, bool flipY) {
    // 这里假设有一个函数 ListFilesInFolder 可以列出文件夹中的所有文件
    std::vector<std::string> filePaths = ListFilesInFolder(folderPath);
    AddFrameBatch(filePaths, flipY);
}


void Animation::Update(float dt) {
    if (frames_uv.empty()) return;
    if (finished) return; // 非循环动画已完成

    timer += dt;
    while (timer >= frameTime) {
        timer -= frameTime;
        currentFrame++;

        if (currentFrame >= frames_uv.size()) {
            if (loop)
                currentFrame = 0;
            else {
                currentFrame = frames_uv.size() - 1; // 保持在最后一帧
                if (!finished && onAnimationFinished) onAnimationFinished();
                finished = true;
                break;
            }
        }
    }

    // if (finished && onAnimationFinished) {
    //     onAnimationFinished();
    //     onAnimationFinished = nullptr; // 确保回调只调用一次
    // }
}

void Animation::setOnAnimationFinished(std::function<void()> callback) {
    onAnimationFinished = callback;
}

void Animation::SetAnimationProperties (bool _loop, bool _emptyWhenDone) 
{
    loop = _loop;
    emptyWhenDone = _emptyWhenDone;
}

void Animation::Reset() {
    timer = 0.0f;
    currentFrame = 0;
    finished = false;
}

std::shared_ptr<Texture2D> Animation::GetSpriteSheet() const {
    return this->spriteSheet;
}

std::shared_ptr<Texture2D> Animation::GetCurrentFrame() const {
    if (currentFrame < frames.size()) {
        return frames[currentFrame];
    }
    return nullptr;
}

bool Animation::IsFinished() const {
    return finished;
}

FrameUV Animation::GetCurrentFrameUV() const {
    if (frames_uv.empty()) return FrameUV::Zero();
    if (finished && emptyWhenDone) return FrameUV::Zero(); // 动画结束后显示空白帧
    return this->frames_uv[currentFrame];
}

void Animation::InitAnimationUV(std::shared_ptr<Texture2D> spriteSheet, size_t frameCount) {
    this->spriteSheet = spriteSheet;
    frames_uv = Texture2D::ClipFramesFromSpriteSheet(frameCount);
}

void Animation::InitAnimationUV(std::shared_ptr<Texture2D> spriteSheet, size_t rows, size_t columns) {
    this->spriteSheet = spriteSheet;
    frames_uv = Texture2D::ClipFramesFromSpriteSheet(rows, columns);
}

void Animation::InitAnimationUV(std::shared_ptr<Texture2D> spriteSheet, std::vector<FrameUV> framesUV) {
    this->spriteSheet = spriteSheet;
    frames_uv = framesUV;
}

bool Animation::IsFrameListEmpty() {
    return frames.empty();
}

void Animation::MakeFrameUVSyncWithFrameList01() {
    size_t frame_list_size = frames.size();
    frames_uv.clear();
    frames_uv.resize(frame_list_size);
    for (size_t i = 0;i < frame_list_size; ++i) {
        frames_uv[i] = FrameUV{Vec2{0.f, 0.f}, Vec2{1.f, 1.f}};
    }
}
