#pragma once
#include<string>
#include<unordered_map>
#include<memory>
#include<array>
#include<vector>
#include<functional>
#include<queue>
#include<thread>
#include<mutex>
#include<atomic>
#include "core/Math.h"
#include "render/Texture2D.h"

class RainSystem;
class Sprite;

class ResourceManager
{
public:
    static ResourceManager& Get();

    void AddTexture2D(std::string key, std::shared_ptr<Texture2D> value);

    std::shared_ptr<Texture2D> GetTexture2D(const std::string& key);

    std::shared_ptr<Sprite> GetSprite(const std::string& key);

    std::vector<std::shared_ptr<Sprite>> GetSpriteList(const std::string& key);

    std::shared_ptr<RainSystem> GetRainSystem();

    const std::array<Vec2, 3> GetUVcords3(const std::string& key);

    const std::array<Vec2, 2> GetUVcords2(const std::string& key);

    void Init();

    // ===== 异步加载（与 BootScene 完全并行） =====
    // 后台线程：磁盘 IO + PNG 解码（不碰 GL）
    // 主线程：每帧调用 UpdateLoading，上传少量已解码纹理 + 至多一个收尾任务
    // 返回 true 表示全部加载完成
    bool UpdateLoading(int maxUploadsPerFrame = 4);
    // 是否已全部加载完成
    bool IsLoadingComplete() const;
    // 等待并回收后台加载线程（程序退出前调用）
    void Shutdown();

private:
    ResourceManager();
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    void InitUVCordsResources();
    void StartLoading();

    // 主线程同步预载（第一帧就要用的资源，如 BootScene 界面；异步队列会跳过已有 key，避免重复解码）
    void PreloadTexture(const std::string& key, const std::string& path);
    // 登记一张待异步解码的纹理
    void EnqueueTexture(const std::string& key, const std::string& path);
    // 后台线程入口
    void LoaderThreadFunc();

    void InitRainSystem();
    void InitSprites();
    void InitSpriteLists();

    // 解码失败的统一出口（主线程调用，保持与旧版 LoadFromFile 一致的失败行为）
    [[noreturn]] void AbortOnLoadError();

private:
    std::unordered_map<std::string, std::shared_ptr<Texture2D>> textureResources;

    std::unordered_map<std::string, std::shared_ptr<Sprite>> spriteResources;

    std::unordered_map<std::string, std::vector<std::shared_ptr<Sprite>>> spriteListResources;

    std::shared_ptr<RainSystem> rainSystem;

    // ===== 异步加载状态 =====
    struct PendingTexture {
        std::string key;
        std::string path;
    };
    // Init 阶段写好、线程启动后只读，无需加锁
    std::vector<PendingTexture> pendingTextures;
    size_t totalAsyncTextures = 0;

    std::thread loaderThread;
    std::mutex decodedMutex;
    std::queue<std::pair<std::string, DecodedImage>> decodedQueue; // decodedMutex 保护
    std::atomic<bool> decodeFinished{false};
    std::atomic<bool> loadError{false};
    std::string loadErrorMessage; // decodedMutex 保护

    size_t uploadedTextures = 0; // 仅主线程访问

    // 依赖全部纹理就绪的主线程收尾任务（Sprite 构建、shader 编译等），逐帧执行
    std::vector<std::function<void()>> postTextureTasks;
    size_t postTaskCursor = 0;

private:
    const std::array<Vec2, 3> obstacleUpCollisionUVcords = {
        Vec2{0.522f, 0.0f}, 
        Vec2{0.281f, 1.0f},
        Vec2{0.62f, 1.0f}
    };

    const std::array<Vec2, 3> obstacleDownCollisionUVcords = {
        Vec2{0.520f, 1.0f}, 
        Vec2{0.281f, 0.0f},
        Vec2{0.62f, 0.0f}
    };

    const std::array<Vec2, 2>  batCollisionUVcords = {
        Vec2{0.16f, 0.68f}, // 左上
        Vec2{0.75f, 0.35f}  // 右下
    };

    std::unordered_map<std::string, std::array<Vec2, 3>> UVcords3Resources;
    std::unordered_map<std::string, std::array<Vec2, 2>> UVcords2Resources;

};
