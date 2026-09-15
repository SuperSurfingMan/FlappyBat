#include "Config.h"
#include "Math.h"

void Config::LoadParamsFromFile(const std::string& filename) {
    // 从文件加载参数的实现, 要求文件格式为 key=value，每行一个参数
    // 从中读取属性时操作 propertiesList

}

void Config::InitAllProperties() {
    propertiesList["collectableItemScale"] = Vec2{30.f, 30.f};
}