#include "res/ResourceManager.h"
#include "res/ResourceCatalog.h"
#include "gameplay/ItemCatalog.h"

#include "sprite/Sprite.h"
#include "sprite/BatSprite.h"
#include "sprite/CollectableSprite.h"

#include "scene/stage/RainSystem.h"
#include "render/ShaderLibrary.h"
#include "core/Config.h"
#include "core/Math.h"

#include <array>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <algorithm>

ResourceManager::ResourceManager() { }

ResourceManager::~ResourceManager() {
    Shutdown();
}

void ResourceManager::Init(const Config& cfg) {
    cfg_ = &cfg;
    InitUVCordsResources();
    std::cout << "Building async resource load pipeline... \n";
    StartLoading();
}

bool ResourceManager::UpdateLoading(int maxUploadsPerFrame) {
    // 解码失败的唯一处理出口在主线程，保持与旧版 LoadFromFile 一致的失败行为
    if (loadError.load()) {
        AbortOnLoadError();
    }

    // 阶段 1：上传后台线程已解码好的纹理（glTexImage2D + mipmap 通常远小于 1ms/张）
    for (int i = 0; i < maxUploadsPerFrame; ++i) {
        std::pair<std::string, DecodedImage> item;
        {
            std::lock_guard<std::mutex> lock(decodedMutex);
            if (decodedQueue.empty())
                break;
            item = std::move(decodedQueue.front());
            decodedQueue.pop();
        }

        auto tex = std::make_shared<Texture2D>();
        tex->UploadDecoded(item.second);
        textureResources[item.first] = tex;
        ++uploadedTextures;
    }

    // 阶段 2：全部纹理上传完成后，逐帧执行收尾任务（每帧一个，shader 编译等尖峰被摊到多帧）
    if (decodeFinished.load() && uploadedTextures >= totalAsyncTextures &&
        postTaskCursor < postTextureTasks.size()) {
        postTextureTasks[postTaskCursor++]();
    }

    return IsLoadingComplete();
}

bool ResourceManager::IsLoadingComplete() const {
    return decodeFinished.load()
        && uploadedTextures >= totalAsyncTextures
        && postTaskCursor >= postTextureTasks.size();
}

void ResourceManager::Shutdown() {
    if (loaderThread.joinable()) {
        loaderThread.join();
    }
}

void ResourceManager::LoaderThreadFunc() {
    for (const auto& pending : pendingTextures) {
        if (loadError.load())
            break; // 已有失败，尽快收尾（主线程会统一处理错误）

        auto decoded = Texture2D::DecodeFromFile(pending.path, true);
        if (!decoded) {
            {
                std::lock_guard<std::mutex> lock(decodedMutex);
                loadErrorMessage = "Failed to decode texture: " + pending.path;
            }
            loadError.store(true);
            break;
        }

        {
            std::lock_guard<std::mutex> lock(decodedMutex);
            decodedQueue.emplace(pending.key, std::move(*decoded));
        }
    }
    decodeFinished.store(true);
    std::cout << "[Loader] decode thread finished. \n";
}

void ResourceManager::AbortOnLoadError() {
    Shutdown(); // 出错时后台线程已 break，join 立即返回
    std::string msg;
    {
        std::lock_guard<std::mutex> lock(decodedMutex);
        msg = loadErrorMessage;
    }
    std::cerr << "[ResourceManager] " << msg << std::endl;
    system("pause"); // 暂停以便查看错误信息
    exit(EXIT_FAILURE);
}

ResourceManager& ResourceManager::Get() {
    static ResourceManager instance;
    return instance;
}

std::vector<std::shared_ptr<Sprite>> ResourceManager::GetSpriteList(const std::string& key) {
    auto ret = spriteListResources.find(key);
    if (ret == spriteListResources.end()) {
        std::cout << "[ERROR] No sprite list with such a key exists! \n";
        return {};
    }
    return spriteListResources[key];
}

void ResourceManager::AddTexture2D(std::string key, std::shared_ptr<Texture2D> value) {
    auto it = textureResources.find(key);
    if (it != textureResources.end()) {
        std::cout << "[WARNING] Texture with the same key already exists!  \n";
        std::cout << "[WARNING] This operation will cover the old texture!  \n";
    }
    textureResources[key] = value;
}

std::shared_ptr<Texture2D> ResourceManager::GetTexture2D(const std::string& key) {
    auto it = textureResources.find(key);
    if (it == textureResources.end()) {
        std::cout << "[ERROR] No texture with such a key " << key << " exists! \n";
        return nullptr;
    }
    return textureResources[key];
}

void ResourceManager::StartLoading() {
    // ===== 资源清单统一由 res/ResourceCatalog.h 登记 =====
    // 加载器只负责遍历：新增贴图 = 往表里加一行，本函数零改动。
    // 预载先于异步入队、表内顺序、v2_ui_logo 的 key 去重，均与改造前一致。
    const auto& catalog = GetResourceCatalog(); // 运行时清单（缺失时回退内置表）
    for (const auto& e : catalog.preload) PreloadTexture(e.key, e.path);
    for (const auto& e : catalog.async)   EnqueueTexture(e.key, e.path);
    // 收集品贴图由道具定义表驱动：新增道具自动登记常态 / 消散两张贴图
    for (const auto& def : kItemDefinitions) {
        EnqueueTexture(def.key, def.texturePath);
        EnqueueTexture(std::string(def.key) + "Disappear", def.disappearPath);
    }
    for (const auto& iconName : catalog.icons) {
        EnqueueTexture(std::string("v2_ui_icon_") + iconName,
                       std::string("assets/Sprites/UI Elements/v2/ui_icon_") + iconName + ".png");
    }

    // ===== 收尾任务：依赖所有纹理上传完成，主线程逐帧执行 =====
    // batBlink07 复用 batBlink01 的纹理（保持原逻辑，避免重复解码）
    postTextureTasks.push_back([this]() {
        textureResources["batBlink07"] = textureResources["batBlink01"];
    });
    postTextureTasks.push_back([this]() { InitSprites(); });
    postTextureTasks.push_back([this]() { InitSpriteLists(); });
    InitRainSystem(); // 登记 RainSystem 数据初始化与 shader 编译任务

    // ===== 启动后台解码线程 =====
    totalAsyncTextures = pendingTextures.size();
    loaderThread = std::thread(&ResourceManager::LoaderThreadFunc, this);
    std::cout << "Async loader started (" << totalAsyncTextures << " textures to decode). \n";
}

void ResourceManager::PreloadTexture(const std::string& key, const std::string& path) {
    auto tex = std::make_shared<Texture2D>();
    tex->LoadFromFile(path);
    textureResources[key] = tex;
}

void ResourceManager::EnqueueTexture(const std::string& key, const std::string& path) {
    if (textureResources.count(key)) {
        std::cout << "[Loader] texture '" << key << "' already loaded, skip decoding. \n";
        return;
    }
    pendingTextures.push_back({ key, path });
}


void ResourceManager::InitRainSystem() {
    // RainSystem 只负责逻辑数据初始化；渲染所需的 shader 在收尾阶段预编译（分帧）。
    postTextureTasks.push_back([this]() {
        rainSystem = std::make_shared<RainSystem>();
    });
    postTextureTasks.push_back([this]() { rainSystem->InitStepRains(*cfg_); });
    postTextureTasks.push_back([this]() { rainSystem->InitStepSplashSets(*cfg_, *this); });

    // shader 预编译：RainRenderer / LightningRenderer 构造时通过 ShaderLibrary 获取
    postTextureTasks.push_back([]() {
        ShaderLibrary::Instance().Load("rain", "assets/shaders/rain.vert", "assets/shaders/rain.frag");
    });
    postTextureTasks.push_back([]() {
        ShaderLibrary::Instance().Load("lightning", "assets/shaders/lightning.vert", "assets/shaders/lightning.frag");
    });
    postTextureTasks.push_back([]() {
        ShaderLibrary::Instance().Load("flash", "assets/shaders/lightning_flash.vert", "assets/shaders/lightning_flash.frag");
    });
    postTextureTasks.push_back([]() {
        ShaderLibrary::Instance().Load("wind", "assets/shaders/wind.vert", "assets/shaders/wind.frag");
    });
    std::cout << "rain system & shaders tasks queued... \n";
}

void ResourceManager::InitSprites() {
    auto batTex = GetTexture2D("bat");
    auto batSprite = std::make_shared<BatSprite>(batTex, *this, *cfg_);
    batSprite->SetScale(cfg_->GetBatSize(), cfg_->GetBatSize());
    spriteResources["bat"] = batSprite;
}

void ResourceManager::InitSpriteLists() {
    float terrainRenderHeight = cfg_->GetTerrainRenderHeight();
    float terrainHeight = cfg_->GetTerrainHeight();

    const float WIN_HEIGHT = cfg_->GetScreenHeight();
    const float WIN_WIDTH = cfg_->GetScreenWidth();

    
    // 初始化背景
    auto& backgroundSprites = spriteListResources["background"];

    auto bgTexture = GetTexture2D("background");
    float backgroundSpriteHeight = WIN_HEIGHT - terrainHeight; // 背景图的实际渲染高度，扣除地面部分
    for (int i = 0; i < 3; i++)
    {
        auto bg = std::make_shared<Sprite>(bgTexture);
        bg->SetScale(270.0f, backgroundSpriteHeight);
        backgroundSprites.push_back(bg);
    }

    // 初始化前景
    auto& foregroundSprites = spriteListResources["foreground"];
    auto fgTexture = GetTexture2D("foreground");

    for (int i = 0; i < 3; i++)
    {
        auto fg = std::make_shared<Sprite>(fgTexture);
        fg->SetScale(270.0f, backgroundSpriteHeight);
        foregroundSprites.push_back(fg);
    }

    // 初始化地面
    auto& terrainSprites = spriteListResources["terrain"];
    auto terrainTexture = GetTexture2D("terrain");
    for (int i = 0; i < 3; i++)
    {
        auto terrain = std::make_shared<Sprite>(terrainTexture);
        terrain->SetScale(270.0f, terrainRenderHeight);
        terrainSprites.push_back(terrain);
    }

    // 初始化障碍物
    auto& obstaclesUpPool = spriteListResources["obstaclesUpPool"];
    auto& obstaclesDownPool = spriteListResources["obstaclesDownPool"];

    auto obsTextureUp = GetTexture2D("obstaclesUp");
    auto obsTextureDown = GetTexture2D("obstaclesDown");

    for (int i = 0; i < 5; i++) {
        auto obsUp = std::make_shared<Sprite>(obsTextureUp);
        obstaclesUpPool.push_back(obsUp);

        auto obsDown = std::make_shared<Sprite>(obsTextureDown);
        obstaclesDownPool.push_back(obsDown);
    }

    // 初始化可收集物品：按 ItemCatalog 定义表登记精灵池与动画网格。
    // 新增道具不需要改这里（也不再有 type == "kiwi" 的帧数特例）。
    Vec2 collectableItemScale = cfg_->GetProperty<Vec2>("collectableItemScale");

    for (const auto& def : kItemDefinitions) {
        auto& pool = spriteListResources[def.poolKey];
        for (size_t i = 0; i < 3; ++i) { // 每类池容量 = ItemField::ItemPoolCap
            auto item = std::make_shared<CollectableSprite>();
            item->InitAnimations(def.key, (size_t)def.rows, (size_t)def.cols,
                                 def.disappearFrames, *this);
            item->SetScale(collectableItemScale.x, collectableItemScale.y);
            pool.push_back(item);
        }
    }

    // ===== 巨化星（B6-b 最稀有道具）：程序化生成五角星纹理 =====
    // 灰度白星：RGB 同值，颜色完全交给精灵逐帧 HSV 炫彩着色（MainScene::UpdateOrb），
    // 白底乘任意色相都鲜艳（原金色底乘蓝/紫会发暗，无法覆盖全谱段）
    {
        const int S = 48;                       // 纹理边长
        const float c = (S - 1) * 0.5f;         // 中心
        const float PI2_5 = 6.2831853f / 5.0f;  // 五角星周期（72°）
        const float rOuter = 0.95f, rInner = 0.42f;

        std::vector<unsigned char> px((size_t)S * S * 4, 0);
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                float dx = (x - c) / c;
                float dy = (y - c) / c;
                float r = std::sqrt(dx * dx + dy * dy);
                if (r > 1.35f) continue;

                // 五角星极径：顶点处 rOuter、谷处 rInner，线性过渡
                float a = std::atan2(dy, dx);
                a = std::fmod(a, PI2_5);
                if (a < 0.0f) a += PI2_5;
                float t = a / PI2_5;                       // 0=顶点, 0.5=谷
                float f = (t < 0.5f) ? t * 2.0f : (1.0f - t) * 2.0f;
                float rStar = rOuter + (rInner - rOuter) * f;

                float starMask = std::clamp((rStar - r) * 8.0f, 0.0f, 1.0f); // 边缘软化
                float glow = std::max(0.0f, 1.0f - r);
                glow *= glow;                                               // 外圈光晕
                float m = std::clamp(starMask + glow * 0.5f, 0.0f, 1.0f);
                if (m <= 0.01f) continue;

                size_t i = ((size_t)y * S + x) * 4;
                unsigned char lum = (unsigned char)std::clamp(
                    255.0f * std::clamp(starMask + glow * 0.55f, 0.0f, 1.0f), 0.0f, 255.0f);
                px[i]     = lum;
                px[i + 1] = lum;
                px[i + 2] = lum;
                px[i + 3] = (unsigned char)(255.0f * m);
            }
        }

        auto starTex = std::make_shared<Texture2D>();
        if (starTex->LoadFromMemory(px.data(), S, S, 4)) {
            AddTexture2D("giantStar", starTex);
            auto orb = std::make_shared<Sprite>(starTex);
            orb->SetScale(44.0f, 44.0f);
            orb->setActive(false); // 未生成时不可见
            spriteResources["giantOrb"] = orb;
            std::cout << "giant star orb generated (procedural 48x48).\n";
        } else {
            std::cout << "[WARN] giant star texture generation failed.\n";
        }
    }
}

const std::array<Vec2, 3> ResourceManager::GetUVcords3(const std::string& key) {
    auto it = UVcords3Resources.find(key);
    if (it == UVcords3Resources.end()) {
        std::cout << "[ERROR] No uv coordinates with such a key exists! \n";
        return {};
    }
    return UVcords3Resources[key];
}

const std::array<Vec2, 2> ResourceManager::GetUVcords2(const std::string& key) {
    auto it = UVcords2Resources.find(key);
    if (it == UVcords2Resources.end()) {
        std::cout << "[ERROR] No uv coordinates with such a key exists! \n";
        return {};
    }
    return UVcords2Resources[key];
}

std::shared_ptr<Sprite> ResourceManager::GetSprite(const std::string& key) {
    auto it = spriteResources.find(key);
    if (it == spriteResources.end()) {
        std::cout << "[ERROR] No sprite with such a key exists! \n";
        return nullptr;
    }
    return spriteResources[key];
}

void ResourceManager::InitUVCordsResources() {
    UVcords3Resources["obstacleUp"] = obstacleUpCollisionUVcords;
    UVcords3Resources["obstacleDown"] = obstacleDownCollisionUVcords;

    UVcords2Resources["bat"] = batCollisionUVcords;
}

std::shared_ptr<RainSystem> ResourceManager::GetRainSystem() {
    if (rainSystem != nullptr)
        return rainSystem;
    std::cout << "RainSystem instance not initialized... \n";
    return rainSystem;
}

