#pragma once
#include <array>
#include <string>
#include <vector>

// =============================================================================
// 资源清单：所有贴图的唯一登记点（第 1 期：从散落的调用序列收敛为数据表）。
//
// 原先 StartLoading() 里是 80 多行逐条 EnqueueTexture 调用，新增贴图只能往
// 函数中间插代码。现在收敛成三张 constexpr 表，加载器只负责遍历：
//   - 新增贴图 = 往 kAsyncTextures 加一行；
//   - 顺序即入队顺序，与改造前完全一致，EnqueueTexture 的 key 去重行为不变；
//   - 表长度由类模板参数推导（C++17 CTAD），不需要手写数量魔数。
//
// 第 2 期可把这三张表直接换成从 manifest 文件解析出的 std::vector，
// 加载器循环本身无需改动。
// =============================================================================
struct TextureEntry {
    const char* key;
    const char* path;
};

// 首帧同步预载（BootScene 立刻要用；异步队列按 key 去重，不会重复解码）
inline constexpr std::array kPreloadTextures = {
    TextureEntry{"boot_background", "assets/Sprites/UI Elements/Boot_background.png"},
    TextureEntry{"v2_ui_logo",      "assets/Sprites/UI Elements/v2/ui_logo.png"},
};

// 后台线程异步解码的纹理清单
inline constexpr std::array kAsyncTextures = {
    TextureEntry{"background",      "assets/Sprites/Game Objects/Background.png"},
    TextureEntry{"foreground",      "assets/Sprites/Game Objects/Foreground.png"},
    TextureEntry{"terrain",         "assets/Sprites/Game Objects/Terrain.png"},
    TextureEntry{"obstaclesUp",     "assets/Sprites/Game Objects/Obstacle_Up.png"},
    TextureEntry{"obstaclesDown",   "assets/Sprites/Game Objects/Obstacle_Down.png"},
    TextureEntry{"bat",             "assets/Sprites/Game Objects/Bat.png"},
    TextureEntry{"batFlySheet",     "assets/Animations/Spritesheets/Fly.png"},
    TextureEntry{"batDeadSheet",    "assets/Animations/Spritesheets/Death.png"},
    TextureEntry{"rainSplash",      "assets/Animations/Spritesheets/rainSplash.png"},
    TextureEntry{"buttonRectangle", "assets/Sprites/UI Elements/Rectangle.png"},
    TextureEntry{"buttonShadow",    "assets/Sprites/UI Elements/button_shadow.png"},
    TextureEntry{"candleLight",     "assets/Animations/Spritesheets/candlelight.png"},
    TextureEntry{"centerFrame",     "assets/Sprites/UI Elements/center_frame.png"},
    TextureEntry{"borderFrame",     "assets/Sprites/UI Elements/frame.png"},
    TextureEntry{"bigBat",          "assets/Animations/Spritesheets/bigBat.png"},
    TextureEntry{"bigBatShort",     "assets/Animations/Spritesheets/bigBatShort.png"},

    // BOSS（D14 血月枭王）：序列帧 / 表演层素材（黑底，luma key 抠像）
    TextureEntry{"bossOwlFlapSheet",       "assets/Animations/Spritesheets/BossOwlFlap.png"},
    TextureEntry{"bossDeathSheet",         "assets/Animations/Spritesheets/BossDeath.png"},
    TextureEntry{"bossOwlFlapSheet10",     "assets/Animations/Spritesheets/BossOwlFlap10.png"},
    TextureEntry{"bossDeathSheet10",       "assets/Animations/Spritesheets/BossOwlDeath.png"},
    TextureEntry{"sonicWaveSheet",         "assets/Animations/Spritesheets/SonicWave.png"},
    TextureEntry{"bloodMoon",              "assets/Sprites/Game Objects/blood_moon.png"},
    TextureEntry{"featherBlade",           "assets/Sprites/Game Objects/feather_blade.png"},
    TextureEntry{"echoCrystal",            "assets/Sprites/Game Objects/echo_crystal.png"},
    TextureEntry{"bossOwlCastSheet",       "assets/Animations/Spritesheets/BossOwlCast.png"},
    TextureEntry{"bossOwlFatigueSheet",    "assets/Animations/Spritesheets/BossOwlFatigue.png"},
    TextureEntry{"bossOwlRageSheet",       "assets/Animations/Spritesheets/BossOwlRage.png"},
    TextureEntry{"bossOwlHitSheet",        "assets/Animations/Spritesheets/BossOwlHit.png"},
    TextureEntry{"bossOwlScreechSheet",    "assets/Animations/Spritesheets/BossOwlScreech.png"},
    TextureEntry{"bossOwlStormCallSheet",  "assets/Animations/Spritesheets/BossOwlStormCall.png"},
    TextureEntry{"bossOwlDashSheet",       "assets/Animations/Spritesheets/BossOwlDash.png"},
    TextureEntry{"bossSwoopSheet",         "assets/Animations/Spritesheets/BossOwlSwoop.png"},

    // 枭王矢量批次：与 legacy 同构，键 = legacy 键 + "_vec"，网格参数复用
    TextureEntry{"bossOwlFlapSheet10_vec",      "assets/Animations/Vector/OwlKing/render/BossOwlFlap10.png"},
    TextureEntry{"bossDeathSheet10_vec",        "assets/Animations/Vector/OwlKing/render/BossOwlDeath.png"},
    TextureEntry{"bossOwlCastSheet_vec",        "assets/Animations/Vector/OwlKing/render/BossOwlCast.png"},
    TextureEntry{"bossOwlFatigueSheet_vec",     "assets/Animations/Vector/OwlKing/render/BossOwlFatigue.png"},
    TextureEntry{"bossOwlRageSheet_vec",        "assets/Animations/Vector/OwlKing/render/BossOwlRage.png"},
    TextureEntry{"bossOwlHitSheet_vec",         "assets/Animations/Vector/OwlKing/render/BossOwlHit.png"},
    TextureEntry{"bossOwlScreechSheet_vec",     "assets/Animations/Vector/OwlKing/render/BossOwlScreech.png"},
    TextureEntry{"bossOwlStormCallSheet_vec",   "assets/Animations/Vector/OwlKing/render/BossOwlStormCall.png"},
    TextureEntry{"bossOwlDashSheet_vec",        "assets/Animations/Vector/OwlKing/render/BossOwlDash.png"},
    TextureEntry{"bossSwoopSheet_vec",          "assets/Animations/Vector/OwlKing/render/BossOwlSwoop.png"},

    // 收集品贴图不在这里登记：由 gameplay/ItemCatalog.h 的定义表驱动
    // （StartLoading 遍历定义表登记 <key> 与 <key>Disappear 两张）。

    TextureEntry{"pauseSpritesheet",  "assets/Animations/Spritesheets/pause_spritesheet.png"},
    TextureEntry{"pause",             "assets/Sprites/UI Elements/pause.png"},
    TextureEntry{"continue",          "assets/Sprites/UI Elements/continue.png"},
    TextureEntry{"return",            "assets/Sprites/UI Elements/return.png"},
    TextureEntry{"quit",              "assets/Sprites/UI Elements/quit.png"},
    TextureEntry{"pausing",           "assets/Sprites/UI Elements/pause_img.png"},
    TextureEntry{"pauseText",         "assets/Sprites/UI Elements/pause_text.png"},
    TextureEntry{"white",             "assets/Sprites/UI Elements/white.png"},
    TextureEntry{"restart",           "assets/Sprites/UI Elements/restart.png"},
    TextureEntry{"batBlink01",        "assets/Animations/Spritesheets/GameOver/01.png"},
    TextureEntry{"batBlink02",        "assets/Animations/Spritesheets/GameOver/02.png"},
    TextureEntry{"batBlink03",        "assets/Animations/Spritesheets/GameOver/03.png"},
    TextureEntry{"batBlink04",        "assets/Animations/Spritesheets/GameOver/04.png"},
    TextureEntry{"batBlink05",        "assets/Animations/Spritesheets/GameOver/05.png"},
    TextureEntry{"batBlink06",        "assets/Animations/Spritesheets/GameOver/06.png"},
    TextureEntry{"scoreRankBackground", "assets/Sprites/UI Elements/ScoreRankBackground.jpg"},

    // V2 UI（暗夜哥特风格）；v2_ui_logo 已预载，EnqueueTexture 会按 key 跳过
    TextureEntry{"v2_ui_logo",               "assets/Sprites/UI Elements/v2/ui_logo.png"},
    TextureEntry{"v2_ui_panel",              "assets/Sprites/UI Elements/v2/ui_panel.png"},
    TextureEntry{"v2_ui_scoreboard_bg",      "assets/Sprites/UI Elements/v2/ui_scoreboard_bg.png"},
    TextureEntry{"v2_ui_achievements_bg",    "assets/Sprites/UI Elements/v2/ui_achievements_bg.png"},
    TextureEntry{"v2_ui_text_paused",        "assets/Sprites/UI Elements/v2/ui_text_paused.png"},
    TextureEntry{"v2_ui_text_gameover",      "assets/Sprites/UI Elements/v2/ui_text_gameover.png"},
    TextureEntry{"v2_ui_text_rank",          "assets/Sprites/UI Elements/v2/ui_text_rank.png"},
    TextureEntry{"v2_ui_text_achievements",  "assets/Sprites/UI Elements/v2/ui_text_achievements.png"},
    TextureEntry{"v2_ui_achievement_toast",  "assets/Sprites/UI Elements/v2/ui_achievement_toast.png"},
    TextureEntry{"v2_ui_score_badge",        "assets/Sprites/UI Elements/v2/ui_score_badge.png"},
    TextureEntry{"v2_ui_button",             "assets/Sprites/UI Elements/v2/ui_button.png"},
    TextureEntry{"v2_ui_button_secondary",   "assets/Sprites/UI Elements/v2/ui_button_secondary.png"},
};

// V2 图标名：键 = "v2_ui_icon_" + name，路径 = ".../ui_icon_" + name + ".png"
inline constexpr std::array kV2IconNames = {
    "pause", "play", "home", "quit", "restart", "rank"
};

// ===== 运行时清单（第 3 期）=====
// 优先读取 assets/manifests/textures.txt；文件缺失或格式不完整时
// 回退到上面的内置 constexpr 表，保证脱离资源目录也能启动。
struct CatalogEntry {
    std::string key;
    std::string path;
};

struct ResourceCatalogData {
    std::vector<CatalogEntry> preload;
    std::vector<CatalogEntry> async;
    std::vector<std::string> icons;
    std::string source; // "manifest" 或 "built-in fallback"
};

const ResourceCatalogData& GetResourceCatalog();
