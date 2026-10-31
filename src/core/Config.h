// 应用程序基本配置信息
#pragma once
#include <utility>
#include <string>
#include <unordered_map>
#include <any>
#include <stdexcept>

// 单例模式的配置类，包含游戏的基本参数
class Config
{
public:
    // 获取单例实例
    static Config& GetInstance()
    {
        static Config instance; // 局部静态变量，保证线程安全的单例实现
        return instance;
    }

    // 删除拷贝构造函数和赋值运算符，防止复制实例
    Config(const Config&) = delete;
    void operator=(const Config&) = delete;

    void LoadParamsFromFile(const std::string& filename); // 从文件加载参数的函数声明

    template<typename T>
    T GetProperty(const std::string key) {
        auto it = propertiesList.find(key);
        if (it == propertiesList.end()) {
            throw std::out_of_range("key not exists: " + key);
        }
        if (it->second.type() != typeid(T)) {
            throw std::bad_cast();
        }
        return std::any_cast<T>(it->second);
    }

    // 所有Getter
    int GetScreenWidth() const { return SCREEN_WIDTH; }
    int GetScreenHeight() const { return SCREEN_HEIGHT; }
    float GetGravity() const { return GRAVITY; }
    float GetFlapStrength() const { return FLAP_STRENGTH; }
    float GetObstacleSpeed() const { return OBSTACLE_SPEED; }
    float GetGapSize() const { return GAP_SIZE; }
    float GetObstacleWidth() const { return OBSTACLE_WIDTH; }
    float GetBatSize() const { return BAT_SIZE; }
    float GetSafeMargin() const { return SAFE_MARGIN; }
    float GetBackgroundSpeed() const { return BACKGROUND_SPEED; }
    float GetForegroundSpeed() const { return FOREGROUND_SPEED; }   

    float GetTerrainRenderHeight() const { return terrain_render_height; }
    float GetTerrainHeight() const { return terrain_height; }


    std::pair<float, float> GetGapCenterDeltaRange() const { return gapCenterDeltaRange; }
    std::pair<float, float> GetSpacingRange() const { return spacingRange; }

    std::pair<size_t, size_t> GetRainSystemConfigs() const { return rain_system_configs; }

private:
    // 私有构造函数，防止外部实例化
    Config() { InitAllProperties(); }

    void InitAllProperties();

    const int SCREEN_WIDTH = 540; // 窗口宽度
    const int SCREEN_HEIGHT = 960; // 窗口高度

    const float BACKGROUND_SPEED = 50.0f;
    const float FOREGROUND_SPEED = 150.0f;
    const float OBSTACLE_SPEED   = 200.0f;

    const float GRAVITY = 500.0f; // 重力加速度
    const float FLAP_STRENGTH = 250.0f; // 蝙蝠向上飞行的初始速度

    const float GAP_SIZE = 100.0f; // 障碍物之间的gap大小
    const float OBSTACLE_WIDTH = 128.0f; // 障碍物的宽度

    const float BAT_SIZE = 64.0f; // 蝙蝠的尺寸

    const float SAFE_MARGIN = 100.0f; // 障碍物生成时与边界的安全距离

    const std::pair<float, float> gapCenterDeltaRange = { -20.0f, 20.0f }; // gap中心位置变化的随机范围
    const std::pair<float, float> spacingRange = { 300.0f, 700.0f }; // 障碍物间距的随机范围（拉大以保证障碍间走廊有空间放置道具）

    const float terrain_render_height = 480.0f;    // 地面贴图的实际大小
    const float terrain_height = 480.0f - 381.0f;  // 地面高度
    const std::pair<size_t, size_t> rain_system_configs = {200, 100};

    std::unordered_map<std::string, std::any> propertiesList;
};
