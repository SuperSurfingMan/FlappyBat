#include "app/Services.h"
#include "sprite/FadeSprite.h"
#include "render/SpriteRenderer.h"
#include "scene/BootScene.h"
#include "scene/SceneManager.h"
#include "core/Config.h"
#include "res/ResourceManager.h"

BootScene::BootScene(Services& services) : svc_(services) {
    // 直接复用 ResourceManager 预载的纹理（Init 阶段已同步加载），避免重复解码
    auto bootBackground = svc_.Res().GetTexture2D("boot_background");
    background = std::make_shared<Sprite>(bootBackground);

    float width = (float)svc_.Cfg().GetScreenWidth();
    float height = (float)svc_.Cfg().GetScreenHeight();
    background->SetScale(width, height);
    background->SetPosition(width * 0.5f, height * 0.5f);

    // 启动 LOGO（蝙蝠与月亮，黑底素材经 luma key 透明叠加）
    auto logoTex = svc_.Res().GetTexture2D("v2_ui_logo");
    text = std::make_shared<FadeSprite>(logoTex, 2.f, 1.0f, 0.05f, Vec2{340, 340}, Vec2{0.9f, 1.0f}, Vec2{1.0f, 1.02f});
    text->SetLumaKey(true, 0.06f, 0.12f);
    text->SetPosition(width * 0.5f, height * 0.5f);
}

void BootScene::OnEnter() {}

void BootScene::OnExit() {}

void BootScene::Update(float dt) {
    text->Update(dt);

    // 动画结束且所有资源加载完成后，才跳转到 ReadyScene
    if (IsFinished() && svc_.Res().IsLoadingComplete()) {
        // 跳转到 ReadyScene
        svc_.Scenes().RequestScene(kSceneReady);
    }
}

void BootScene::Render(SpriteRenderer& renderer) {
    renderer.Draw_UV(background);
    renderer.Draw_UV(text);
}

void BootScene::Reset() {
    text->Reset();
}

bool BootScene::IsFinished() const {
    auto bootText = std::static_pointer_cast<FadeSprite>(text);
    return bootText->IsFinished();
}
