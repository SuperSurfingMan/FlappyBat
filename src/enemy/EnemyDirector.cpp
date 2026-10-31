#include "enemy/EnemyDirector.h"

void EnemyDirector::Register(std::shared_ptr<IEnemy> enemy) {
    enemies_.push_back(std::move(enemy));
}

void EnemyDirector::InitAll() {
    for (auto& e : enemies_) e->Init();
}

void EnemyDirector::OnRunStart() {
    for (auto& e : enemies_) e->OnRunStart();
}

void EnemyDirector::OnBatDead() {
    for (auto& e : enemies_) e->OnBatDead();
}

void EnemyDirector::Update(float dt, const EnemyContext& ctx) {
    rules_ = EncounterRules{};
    for (auto& e : enemies_) {
        e->Update(dt, ctx);
        // 单怪不变量（BOSS设计文档 §3 硬性规则）：段落接管取注册序第一个活动怪物，
        // 注册序即优先级；将来放开多怪同屏时改为逐位 OR 即可，接口不变
        EncounterRules r = e->ActiveRules();
        if (!rules_.Active() && r.Active()) rules_ = r;
    }
}

void EnemyDirector::RenderWorld(SpriteRenderer& renderer) {
    for (auto& e : enemies_) e->RenderWorld(renderer);
}

void EnemyDirector::RenderUI() {
    for (auto& e : enemies_) e->RenderUI();
}
