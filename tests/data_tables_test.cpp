// =============================================================================
// 数据表自测（第 1 期止血 + 第 2 期扩展验收）。
//
// 只链接玩法域注册表 / 参数表 / 排行榜 / 成就 / 音效目录，不引入 OpenGL、
// ImGui、miniaudio，因此可以在任何构建环境里快速跑完。
//
// 第 2 期的核心验收：往注册表追加一个「测试用假模式 ZenMode」（带自定义
// IGameMode 行为），验证模式数量、菜单名、页签名、分榜段数、存档兼容、
// 成就事件全部自动派生，下游代码零改动。
// =============================================================================
#include "app/Achievements.h"
#include "app/PlayerProfile.h"
#include "app/ScoreRank.h"
#include "audio/SFXCatalog.h"
#include "enemy/IEnemy.h"
#include "enemy/IStageServices.h"
#include "gameplay/ItemCatalog.h"
#include "gameplay/ModeRegistry.h"
#include "gameplay/StageParams.h"
#include "res/ManifestReader.h"
#include "res/ResourceCatalog.h"
#include "scene/stage/ParallaxField.h"
#include "scene/stage/RunFlow.h"
#include "scene/stage/RunSettlement.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// 第 3 期 ISP 证明：只实现 IFeedbackFx 的 6 个方法即可被消费，
// 不必实现 IStageFeedback 组合接口里其余 10 个无关方法。
class MinimalFx : public IFeedbackFx {
public:
    int shakes = 0;
    void FloatTextAt(const std::string&, Vec2, float, float, float,
                     float, float) override {}
    void PlaySFX(int, float) override {}
    void Shake(float) override { ++shakes; }
    void PostWeatherMsg(int, const std::string&, float, bool) override {}
    void RemoveWeatherMsg(int) override {}
    void RequestLightningStrike(float) override {}
};

int g_failures = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::cerr << "FAIL: " << msg << "  (line " << __LINE__ << ")\n";    \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

// 与 ScoreRank.cpp 的历史协议魔数保持一致（仅测试用途，不是第二份事实源）
constexpr size_t kFileMagic = 0x46424154334D4F44ULL; // "FBAT3MOD"

// ===== 验收用假模式：自定义 IGameMode 策略，不改动任何下游代码 =====
class ZenMode : public IGameMode {
public:
    ModeKey Key() const override { return "zen"; }
    const char* DisplayName() const override { return "禅境"; }
    const char* MenuDesc() const override { return "没有风暴，只有节奏"; }
    const ModeParams& Params() const override { return params_; }

    // 自定义计分成就：除通用 run_score 外另上报一个专属事件
    std::vector<ScoreAchievement> ScoreAchievements(long long total) const override {
        return { {"run_score", total}, {"zen_score", total} };
    }

private:
    ModeParams params_{ 0.8f, 1, 9.0f, 15.0f, 0.8f, false,
                        0.0f, 0.0f, 0.0f, 0.0f, false,
                        kTimeRushDuration, 60.0f, 1.8f };
};

void WriteSizeTs(const std::string& path, const std::vector<size_t>& values) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (size_t v : values)
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

std::vector<size_t> MakeSegmentedBlob(size_t segments,
                                      const std::vector<std::vector<size_t>>& lists) {
    static const std::vector<size_t> empty;
    std::vector<size_t> blob{kFileMagic, segments};
    for (size_t m = 0; m < segments; ++m) {
        const auto& list = (m < lists.size()) ? lists[m] : empty;
        blob.push_back(list.size());
        for (size_t s : list) blob.push_back(s);
    }
    return blob;
}

template <typename F>
bool ThrowsOutOfRange(F&& f) {
    try { f(); } catch (const std::out_of_range&) { return true; }
    catch (...) { return false; }
    return false;
}

std::vector<std::vector<size_t>> FourModeLists() {
    std::vector<std::vector<size_t>> lists;
    for (size_t m = 0; m < 4; ++m)
        lists.push_back({size_t(100 + m * 10), size_t(200 + m * 10)});
    return lists;
}

// ===== 0) manifest 可解析且与内置回退表一致 =====
void TestManifestFile() {
    std::vector<ManifestLine> lines;
    std::string error;
    const std::string root = FLAPPYBAT_SOURCE_DIR;
    CHECK(ReadManifest(root + "/assets/manifests/textures.txt", lines, &error),
          "纹理 manifest 可解析: " + error);

    int preload = 0, async = 0, icons = 0;
    for (const auto& l : lines) {
        if (l.section == "preload")     ++preload;
        else if (l.section == "async")  ++async;
        else if (l.section == "icons")  ++icons;
    }
    CHECK(preload == (int)kPreloadTextures.size(), "manifest preload 段与内置表一致");
    CHECK(async == (int)kAsyncTextures.size(),     "manifest async 段与内置表一致");
    CHECK(icons == (int)kV2IconNames.size(),       "manifest icons 段与内置表一致");

    std::vector<ManifestLine> sfxLines;
    CHECK(ReadManifest(root + "/assets/manifests/sfx.txt", sfxLines, &error),
          "音效 manifest 可解析: " + error);
    CHECK(sfxLines.size() == SFXCount, "音效清单条目覆盖所有 SFX 枚举值");
}

// ===== 3) 接口隔离：新消费者只需窄接口 =====
void TestInterfaceSegregation() {
    MinimalFx fx;
    EnemyContext ctx;
    ctx.fxFx = &fx; // 只注入纯表现角色
    CHECK(ctx.fxEconomy == nullptr, "新消费者无需提供结算接口");
    CHECK(ctx.fxPlayer == nullptr,  "新消费者无需提供玩家操作接口");
    ctx.fxFx->Shake(2.0f);
    CHECK(fx.shakes == 1, "只实现 IFeedbackFx 即可被怪物使用");
}

// ===== 4) 局末结算规则（第 4 期抽出后可脱离场景验证） =====
// 榜单是全局落盘状态（前面的兼容性用例已写入经典榜），因此这里不复用「空榜」假设，
// 而是以当前榜首为基准构造破纪录 / 未破纪录 / 再破纪录三种情形。
void TestSettlementRules() {
    ScoreRank& rank = ScoreRank::Get();
    PlayerProfile& profile = PlayerProfile::Get();

    const auto prior = rank.GetScores(kDefaultModeKey);
    const long long priorBest = prior.empty() ? 0 : (long long)prior.front();
    const size_t low  = (size_t)(priorBest + 100); // 必然破纪录
    const size_t high = (size_t)(priorBest + 200); // 再破一次
    const size_t weak = low - 55;                  // 必然不破纪录

    auto runOf = [](size_t score) {
        RunFlow f;
        f.SetMode(kDefaultModeKey);
        f.Reset(ParamsOf(kDefaultModeKey));
        f.AddScore(score, 1); // scoreFactor = 1，落榜分数即入参
        return f;
    };

    const long long coinsBefore = profile.GetCoins();

    RunFlow first = runOf(low);
    SettlementResult r1 = RunSettlement::Apply(first, rank, profile);
    CHECK(r1.score == low, "结算回传本局分数");
    CHECK(r1.newRecord, "超过历史最高分判为破纪录");
    CHECK(r1.coinBonus == (long long)(low / RunSettlement::ScorePerCoin),
          "金币 = 分数 / 5（比例来自 RunSettlement::ScorePerCoin）");
    CHECK(r1.best == (int)low, "结算面板显示写榜后的榜首");
    CHECK(profile.GetCoins() == coinsBefore + r1.coinBonus, "金币实际入账");
    CHECK(first.ScoreSaved(), "结算后标记已保存（退出场景不再重复写榜）");

    RunFlow second = runOf(weak);
    SettlementResult r2 = RunSettlement::Apply(second, rank, profile);
    CHECK(!r2.newRecord, "低于历史最高分不判破纪录");
    CHECK(r2.coinBonus == (long long)(weak / RunSettlement::ScorePerCoin),
          "低分局金币仍按分数折算");
    CHECK(r2.best == (int)low, "未破纪录时榜首保持不变");

    RunFlow third = runOf(high);
    SettlementResult r3 = RunSettlement::Apply(third, rank, profile);
    CHECK(r3.newRecord, "再次超过榜首判为破纪录");
    CHECK(r3.best == (int)high, "榜首更新为最新高分");
    CHECK(profile.GetCoins() ==
              coinsBefore + r1.coinBonus + r2.coinBonus + r3.coinBonus,
          "三局金币累计正确");
}

// ===== 5) 视差回绕距离由块数推导（不再硬编码 270 * 3） =====
void TestParallaxWrap() {
    CHECK(ParallaxField::WrapDistance(3) == 810.0f, "三块条带回绕 810px（与原硬编码等价）");
    CHECK(ParallaxField::WrapDistance(4) == 1080.0f, "四块条带自动推导回绕距离");
    CHECK(ParallaxField::WrapDistance(0) == 0.0f, "零块条带不做回绕（不空转）");
}

// ===== 1) 注册表契约 + 自定义策略 =====
void TestRegistryContracts() {
    auto& registry = ModeRegistry::Instance();
    CHECK(registry.Size() == 4, "3 个内置模式 + 1 个假模式 = 4");
    CHECK(registry.Contains("zen"), "假模式已登记");
    CHECK(registry.IndexOf("zen") == 3, "假模式追加在注册序末尾（旧档段索引不变）");

    std::string err;
    CHECK(ValidateStageParams(&err), "遍历注册表的参数校验通过: " + err);

    for (std::size_t i = 0; i < registry.Size(); ++i) {
        const IGameMode& mode = registry.At(i);
        CHECK(mode.DisplayName() && *mode.DisplayName(),
              "模式 " + std::to_string(i) + " 有显示名");
        CHECK(mode.MenuDesc() && *mode.MenuDesc(),
              "模式 " + std::to_string(i) + " 有菜单说明");
        CHECK(&ParamsOf(mode.Key()) == &mode.Params(),
              "ParamsOf 与注册表指向同一份参数");
        CHECK(std::string(ScoreRank::ModeName(mode.Key())) == mode.DisplayName(),
              "排行榜页签名与注册表同源");
    }

    CHECK(ThrowsOutOfRange([] { (void)ModeRegistry::Instance().Get("not_registered"); }),
          "未登记模式明确抛错，不静默兜底");

    // 自定义策略无需改动 MainScene：ScoreAchievements 直接声明专属事件
    const auto zenAch = registry.Get("zen").ScoreAchievements(123);
    CHECK(zenAch.size() == 2, "假模式上报通用 + 专属两条成就事件");
    CHECK(std::string(zenAch[1].eventKey) == "zen_score" && zenAch[1].value == 123,
          "专属成就事件内容正确");

    // 音效目录仍是按枚举值索引的完整表
    for (std::size_t i = 0; i < SFXCount; ++i)
        CHECK(!SFXToFile(static_cast<SFX>(i)).empty(),
              "SFX[" + std::to_string(i) + "] 有非空路径");
    CHECK(ThrowsOutOfRange([] { (void)SFXToFile(SFX::Count); }),
          "SFXToFile(Count) 明确抛错");

    // 清单外置：当前工作目录（临时目录）没有 manifest，应回退到内置表
    const auto& catalog = GetResourceCatalog();
    CHECK(catalog.source == "built-in fallback", "manifest 缺失时回退内置清单");
    CHECK(catalog.preload.size() == kPreloadTextures.size(), "回退 preload 段完整");
    CHECK(catalog.async.size() == kAsyncTextures.size(),     "回退 async 段完整");
    CHECK(catalog.icons.size() == kV2IconNames.size(),       "回退 icons 段完整");

    // 道具定义表：新增道具只加一行定义，道具场 / 资源管理器 / 拾取结算零改动
    {
        float sum = 0.0f;
        std::set<std::string> keys;
        for (std::size_t i = 0; i < ItemCount(); ++i) {
            const ItemDefinition& d = ItemAt(i);
            CHECK(keys.insert(d.key).second, "道具键唯一");
            CHECK(d.texturePath && *d.texturePath &&
                  d.disappearPath && *d.disappearPath,
                  "道具 " + std::string(d.key) + " 贴图路径完整");
            CHECK(d.rows > 0 && d.cols > 0 && d.disappearFrames > 0,
                  "道具 " + std::string(d.key) + " 动画参数合法");
            CHECK(d.effect.floatText && *d.effect.floatText,
                  "道具 " + std::string(d.key) + " 有飘字");
            sum += d.weightNormal;
        }
        CHECK(std::fabs(sum - 1.0f) < 0.001f, "普通难度权重和约等于 1");
    }

    // 成就规则表驱动：未知事件是 no-op，通用事件仍能推进成就等级
    CHECK(Achievements::Rules().size() >= 13, "成就规则表已加载");
    Achievements::Get().Report("zen_score", 500); // 规则表中没有该事件：不应崩溃
    Achievements::Get().Report("run_score", 100);
    CHECK(Achievements::Get().Level(0) >= 1, "run_score 经规则表推进成就 0");
}

// ===== 2) 动态分榜与 score.dat 兼容 =====
void TestScoreRankCompatibility() {
    auto& rank = ScoreRank::Get();
    const auto lists = FourModeLists();

    // 旧版单榜：整文件按 size_t 顺序读入，全部归入注册序第 0 段
    WriteSizeTs("score.dat", {30, 10, 20});
    CHECK(rank.GetScoresFromDisk(kDefaultModeKey) == std::vector<size_t>({30, 20, 10}),
          "旧版单榜迁移并降序排列");

    // 新版 4 段：注册表有几个模式就写几段
    WriteSizeTs("score.dat", MakeSegmentedBlob(4, lists));
    for (std::size_t m = 0; m < 4; ++m) {
        const ModeKey key = ModeRegistry::Instance().At(m).Key();
        const std::vector<size_t> expected{size_t(200 + m * 10), size_t(100 + m * 10)};
        CHECK(rank.GetScoresFromDisk(key) == expected,
              "分段榜单[" + std::to_string(m) + "] 读回正确");
    }

    // 关键验收：旧档段数(3) 少于当前模式数(4)，新段为空且旧段无损
    WriteSizeTs("score.dat", MakeSegmentedBlob(3, lists));
    for (std::size_t m = 0; m < 3; ++m) {
        const ModeKey key = ModeRegistry::Instance().At(m).Key();
        const std::vector<size_t> expected{size_t(200 + m * 10), size_t(100 + m * 10)};
        CHECK(rank.GetScoresFromDisk(key) == expected,
              "旧档段[" + std::to_string(m) + "] 保留");
    }
    CHECK(rank.GetScoresFromDisk("zen").empty(),
          "新增模式段在旧档中为空，不产生脏数据");

    // 假模式写分后自动扩成 4 段，且其它模式数据保留
    rank.GetScoresFromDisk("zen"); // 重新载入上面的 3 段旧档
    rank.UpdateScore(999, "zen");
    CHECK(rank.GetScoresFromDisk("zen") == std::vector<size_t>({999}),
          "假模式分数写入新段");
    CHECK(rank.GetScoresFromDisk("classic") == std::vector<size_t>({200, 100}),
          "写出新档时经典段保持不变");
}

} // namespace

int main() {
    TestManifestFile(); // 切目录前：按绝对路径校验工程内的 manifest 文件

    // ScoreRank / Achievements 都读固定文件名的存档：切到专用临时目录，
    // 避免碰到工程根目录里的真实排行榜与玩家存档。
    std::error_code ec;
    const auto saved = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "flappybat_datatable_test";
    std::filesystem::create_directories(tmp, ec);
    if (ec) {
        std::cerr << "FAIL: 无法创建测试临时目录: " << ec.message() << "\n";
        return 2;
    }
    std::filesystem::current_path(tmp, ec);
    if (ec) {
        std::cerr << "FAIL: 无法切换测试目录: " << ec.message() << "\n";
        return 2;
    }

    // 组合根：先登记内置模式，再追加一个「测试用假模式」。
    // 这一步就是第 2 期的扩展操作 —— 下面所有断言都不得因它而修改下游代码。
    ModeRegistry::Instance().Clear();
    RegisterBuiltinModes();
    ModeRegistry::Instance().Register(std::make_unique<ZenMode>());

    TestRegistryContracts();
    TestScoreRankCompatibility();
    TestInterfaceSegregation();
    TestSettlementRules();
    TestParallaxWrap();

    // 只清理由本次测试创建的文件与空目录，不递归删除任何东西
    std::filesystem::current_path(saved, ec);
    std::filesystem::remove(tmp / "score.dat", ec);
    std::filesystem::remove(tmp / "player_save.dat", ec);
    std::filesystem::remove(tmp, ec);

    if (g_failures != 0) {
        std::cerr << g_failures << " 项检查失败\n";
        return 1;
    }
    std::cout << "mode registry / dynamic ranks / achievement rules / score.dat: "
                 "all checks passed\n";
    return 0;
}
