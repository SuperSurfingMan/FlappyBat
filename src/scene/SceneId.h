#pragma once
// =============================================================================
// 场景标识：切场请求的唯一地址。
// 场景不再持有其它场景的具体类型、也不再拼接 Application::Get().GetScene(...) 样板，
// 统一经 SceneManager 的注册表（SceneId -> 实例）解析。
// =============================================================================
enum class SceneId {
    Boot,
    Ready,
    Main,
    ScoreRank,
    Achievements,
    Settings,
};
