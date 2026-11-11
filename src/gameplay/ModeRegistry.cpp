#include "gameplay/ModeRegistry.h"

ModeRegistry& ModeRegistry::Instance() {
    static ModeRegistry instance;
    return instance;
}

void ModeRegistry::Register(std::unique_ptr<IGameMode> mode) {
    if (!mode) throw std::invalid_argument("ModeRegistry::Register: 空模式");
    const std::string key(mode->Key());
    if (key.empty()) throw std::invalid_argument("ModeRegistry::Register: 空 ModeKey");
    if (index_.count(key))
        throw std::invalid_argument("ModeRegistry::Register: 重复注册模式 " + key);

    index_.emplace(key, modes_.size());
    order_.push_back(mode.get());
    modes_.push_back(std::move(mode));
}

const IGameMode& ModeRegistry::Get(ModeKey key) const {
    return At(IndexOf(key));
}

const IGameMode& ModeRegistry::At(std::size_t index) const {
    if (index >= modes_.size())
        throw std::out_of_range("ModeRegistry::At: 索引越界");
    return *modes_[index];
}

std::size_t ModeRegistry::IndexOf(ModeKey key) const {
    auto it = index_.find(std::string(key));
    if (it == index_.end())
        throw std::out_of_range("ModeRegistry::IndexOf: 模式未登记 '" + std::string(key) + "'");
    return it->second;
}

bool ModeRegistry::Contains(ModeKey key) const {
    return index_.find(std::string(key)) != index_.end();
}

void ModeRegistry::Clear() {
    modes_.clear();
    order_.clear();
    index_.clear();
}

const ModeParams& ParamsOf(ModeKey key) {
    return ModeRegistry::Instance().Get(key).Params();
}
