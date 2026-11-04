#include "res/ResourceManager.h"

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

void ResourceManager::Init() {
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
    // ===== Boot 场景资源：主线程同步预载（第一帧就要用） =====
    // v2_ui_logo 之后 BootScene 直接从 ResourceManager 取用；异步队列按 key 去重跳过，保证只解码一次
    PreloadTexture("boot_background", "assets/Sprites/UI Elements/Boot_background.png");
    PreloadTexture("v2_ui_logo", "assets/Sprites/UI Elements/v2/ui_logo.png");

    // ===== 异步纹理清单（后台线程只做 IO + 解码，不碰 GL） =====
    EnqueueTexture("background", "assets/Sprites/Game Objects/Background.png");
    EnqueueTexture("foreground", "assets/Sprites/Game Objects/Foreground.png");
    EnqueueTexture("terrain", "assets/Sprites/Game Objects/Terrain.png");
    EnqueueTexture("obstaclesUp", "assets/Sprites/Game Objects/Obstacle_Up.png");
    EnqueueTexture("obstaclesDown", "assets/Sprites/Game Objects/Obstacle_Down.png");
    EnqueueTexture("bat", "assets/Sprites/Game Objects/Bat.png");
    EnqueueTexture("batFlySheet", "assets/Animations/Spritesheets/Fly.png");
    EnqueueTexture("batDeadSheet", "assets/Animations/Spritesheets/Death.png");
    EnqueueTexture("rainSplash", "assets/Animations/Spritesheets/rainSplash.png");
    EnqueueTexture("buttonRectangle", "assets/Sprites/UI Elements/Rectangle.png");
    EnqueueTexture("buttonShadow", "assets/Sprites/UI Elements/button_shadow.png");
    EnqueueTexture("candleLight", "assets/Animations/Spritesheets/candlelight.png");
    EnqueueTexture("centerFrame", "assets/Sprites/UI Elements/center_frame.png");
    EnqueueTexture("borderFrame", "assets/Sprites/UI Elements/frame.png");
    EnqueueTexture("bigBat", "assets/Animations/Spritesheets/bigBat.png");
    EnqueueTexture("bigBatShort", "assets/Animations/Spritesheets/bigBatShort.png");
    // BOSS（D14 血月枭王）：本体拍翼/死亡序列帧、音爆网格动画、血月/羽刃/水晶（均为黑底，luma key 抠像）
    EnqueueTexture("bossOwlFlapSheet", "assets/Animations/Spritesheets/BossOwlFlap.png");
    EnqueueTexture("bossDeathSheet", "assets/Animations/Spritesheets/BossDeath.png");
    EnqueueTexture("bossOwlFlapSheet10", "assets/Animations/Spritesheets/BossOwlFlap10.png");
    EnqueueTexture("bossDeathSheet10", "assets/Animations/Spritesheets/BossOwlDeath.png");
    EnqueueTexture("sonicWaveSheet", "assets/Animations/Spritesheets/SonicWave.png");
    EnqueueTexture("bloodMoon", "assets/Sprites/Game Objects/blood_moon.png");
    EnqueueTexture("featherBlade", "assets/Sprites/Game Objects/feather_blade.png");
    EnqueueTexture("echoCrystal", "assets/Sprites/Game Objects/echo_crystal.png");
    // BOSS 表演层素材（docs/BOSS美术素材生成方案 §四）：施法/力竭/狂暴/受击/瞳孔/技能姿势序列帧（黑底转透明 + repack 对齐）
    EnqueueTexture("bossOwlCastSheet", "assets/Animations/Spritesheets/BossOwlCast.png");
    EnqueueTexture("bossOwlFatigueSheet", "assets/Animations/Spritesheets/BossOwlFatigue.png");
    EnqueueTexture("bossOwlRageSheet", "assets/Animations/Spritesheets/BossOwlRage.png");
    EnqueueTexture("bossOwlHitSheet", "assets/Animations/Spritesheets/BossOwlHit.png");
    EnqueueTexture("bossOwlScreechSheet", "assets/Animations/Spritesheets/BossOwlScreech.png");
    EnqueueTexture("bossOwlStormCallSheet", "assets/Animations/Spritesheets/BossOwlStormCall.png");
    EnqueueTexture("bossOwlDashSheet", "assets/Animations/Spritesheets/BossOwlDash.png");
    EnqueueTexture("bossSwoopSheet", "assets/Animations/Spritesheets/BossOwlSwoop.png");
    // 枭王矢量批次（assets/Animations/Vector/OwlKing/render，tools/render_owlking_vector_sheets.py
    // 光栅化产物）：与 legacy 归一化表同规格（640 方格、eye_anchor=格正中），OwlKing 运行时按
    // ArtBatch 开关在两批同构纹理间切换；键 = legacy 键 + "_vec" 后缀，网格参数完全复用
    EnqueueTexture("bossOwlFlapSheet10_vec", "assets/Animations/Vector/OwlKing/render/BossOwlFlap10.png");
    EnqueueTexture("bossDeathSheet10_vec",   "assets/Animations/Vector/OwlKing/render/BossOwlDeath.png");
    EnqueueTexture("bossOwlCastSheet_vec",   "assets/Animations/Vector/OwlKing/render/BossOwlCast.png");
    EnqueueTexture("bossOwlFatigueSheet_vec","assets/Animations/Vector/OwlKing/render/BossOwlFatigue.png");
    EnqueueTexture("bossOwlRageSheet_vec",   "assets/Animations/Vector/OwlKing/render/BossOwlRage.png");
    EnqueueTexture("bossOwlHitSheet_vec",    "assets/Animations/Vector/OwlKing/render/BossOwlHit.png");
    EnqueueTexture("bossOwlScreechSheet_vec","assets/Animations/Vector/OwlKing/render/BossOwlScreech.png");
    EnqueueTexture("bossOwlStormCallSheet_vec","assets/Animations/Vector/OwlKing/render/BossOwlStormCall.png");
    EnqueueTexture("bossOwlDashSheet_vec",   "assets/Animations/Vector/OwlKing/render/BossOwlDash.png");
    EnqueueTexture("bossSwoopSheet_vec",     "assets/Animations/Vector/OwlKing/render/BossOwlSwoop.png");
    EnqueueTexture("mosquitoe", "assets/Animations/Spritesheets/mosquitoe.png");
    EnqueueTexture("kiwi", "assets/Animations/Spritesheets/kiwiFruit.png");
    EnqueueTexture("firefly", "assets/Animations/Spritesheets/firefly.png");
    EnqueueTexture("mosquitoeDisappear", "assets/Animations/Spritesheets/mosquitoe_disappear.png");
    EnqueueTexture("kiwiDisappear", "assets/Animations/Spritesheets/kiwi_disappear.png");
    EnqueueTexture("fireflyDisappear", "assets/Animations/Spritesheets/firefly_disappear.png");
    EnqueueTexture("pauseSpritesheet", "assets/Animations/Spritesheets/pause_spritesheet.png");
    EnqueueTexture("pause", "assets/Sprites/UI Elements/pause.png");
    EnqueueTexture("continue", "assets/Sprites/UI Elements/continue.png");
    EnqueueTexture("return", "assets/Sprites/UI Elements/return.png");
    EnqueueTexture("quit", "assets/Sprites/UI Elements/quit.png");
    EnqueueTexture("pausing", "assets/Sprites/UI Elements/pause_img.png");
    EnqueueTexture("pauseText", "assets/Sprites/UI Elements/pause_text.png");
    EnqueueTexture("white", "assets/Sprites/UI Elements/white.png");
    EnqueueTexture("restart", "assets/Sprites/UI Elements/restart.png");
    EnqueueTexture("batBlink01", "assets/Animations/Spritesheets/GameOver/01.png");
    EnqueueTexture("batBlink02", "assets/Animations/Spritesheets/GameOver/02.png");
    EnqueueTexture("batBlink03", "assets/Animations/Spritesheets/GameOver/03.png");
    EnqueueTexture("batBlink04", "assets/Animations/Spritesheets/GameOver/04.png");
    EnqueueTexture("batBlink05", "assets/Animations/Spritesheets/GameOver/05.png");
    EnqueueTexture("batBlink06", "assets/Animations/Spritesheets/GameOver/06.png");
    EnqueueTexture("scoreRankBackground", "assets/Sprites/UI Elements/ScoreRankBackground.jpg");

    // ===== V2 UI 资源（暗夜哥特风格） =====
    // v2_ui_logo 已预载，EnqueueTexture 检测到同 key 会自动跳过（不重复解码）
    EnqueueTexture("v2_ui_logo", "assets/Sprites/UI Elements/v2/ui_logo.png");
    EnqueueTexture("v2_ui_panel", "assets/Sprites/UI Elements/v2/ui_panel.png");
    EnqueueTexture("v2_ui_scoreboard_bg", "assets/Sprites/UI Elements/v2/ui_scoreboard_bg.png");
    EnqueueTexture("v2_ui_achievements_bg", "assets/Sprites/UI Elements/v2/ui_achievements_bg.png");
    EnqueueTexture("v2_ui_text_paused", "assets/Sprites/UI Elements/v2/ui_text_paused.png");
    EnqueueTexture("v2_ui_text_gameover", "assets/Sprites/UI Elements/v2/ui_text_gameover.png");
    EnqueueTexture("v2_ui_text_rank", "assets/Sprites/UI Elements/v2/ui_text_rank.png");
    EnqueueTexture("v2_ui_text_achievements", "assets/Sprites/UI Elements/v2/ui_text_achievements.png");
    EnqueueTexture("v2_ui_achievement_toast", "assets/Sprites/UI Elements/v2/ui_achievement_toast.png");
    EnqueueTexture("v2_ui_score_badge", "assets/Sprites/UI Elements/v2/ui_score_badge.png");
    EnqueueTexture("v2_ui_button", "assets/Sprites/UI Elements/v2/ui_button.png");
    EnqueueTexture("v2_ui_button_secondary", "assets/Sprites/UI Elements/v2/ui_button_secondary.png");

    const std::vector<std::string> v2IconNames = {
        "pause", "play", "home", "quit", "restart", "rank"
    };
    for (const auto& iconName : v2IconNames) {
        EnqueueTexture("v2_ui_icon_" + iconName,
            "assets/Sprites/UI Elements/v2/ui_icon_" + iconName + ".png");
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
    postTextureTasks.push_back([this]() { rainSystem->InitStepRains(); });
    postTextureTasks.push_back([this]() { rainSystem->InitStepSplashSets(); });

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
    auto batSprite = std::make_shared<BatSprite>(batTex);
    batSprite->SetScale(Config::GetInstance().GetBatSize(), Config::GetInstance().GetBatSize());
    spriteResources["bat"] = batSprite;
}

void ResourceManager::InitSpriteLists() {
    float terrainRenderHeight = Config::GetInstance().GetTerrainRenderHeight();
    float terrainHeight = Config::GetInstance().GetTerrainHeight();

    const float WIN_HEIGHT = Config::GetInstance().GetScreenHeight();
    const float WIN_WIDTH = Config::GetInstance().GetScreenWidth();

    
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

    // 初始化可收集物品
    Vec2 collectableItemScale = Config::GetInstance().GetProperty<Vec2>("collectableItemScale");

    auto& kiwiPool = spriteListResources["kiwiPool"];
    auto& mosquitoePool = spriteListResources["mosquitoePool"];
    auto& fireflyPool = spriteListResources["fireflyPool"];

    for (size_t i = 0;i < 3; ++i) {
        auto kiwiSprite = std::make_shared<CollectableSprite>();
        kiwiSprite->InitAnimations("kiwi", 2, 4);
        kiwiSprite->SetScale(collectableItemScale.x, collectableItemScale.y);

        auto mosquitoeSprite = std::make_shared<CollectableSprite>();
        mosquitoeSprite->InitAnimations("mosquitoe", 1, 8);
        mosquitoeSprite->SetScale(collectableItemScale.x, collectableItemScale.y);

        auto fireflySprite = std::make_shared<CollectableSprite>();
        fireflySprite->InitAnimations("firefly", 1, 8);
        fireflySprite->SetScale(collectableItemScale.x, collectableItemScale.y);

        kiwiPool.push_back(kiwiSprite);
        mosquitoePool.push_back(mosquitoeSprite);
        fireflyPool.push_back(fireflySprite);
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

