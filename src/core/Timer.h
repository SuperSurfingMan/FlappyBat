#pragma once

#include <functional>

class Timer
{
public:
    using Callback = std::function<void()>;

public:
    Timer();
    explicit Timer(float duration, Callback cb = nullptr);

    // ======================
    // 控制
    // ======================
    void Start(float duration, Callback cb);
    void Stop();       // 停止并冻结
    void Resume();     // 从当前位置继续
    void Reset();      // 清零并停止

    // ======================
    // 更新（外部驱动）
    // ======================
    void Update(float dt);

    // ======================
    // 状态查询
    // ======================
    bool IsRunning() const;   // 是否正在计时
    bool IsFinished() const;  // 是否完成

    float GetProgress() const;   // 0~1
    float GetRemaining() const;  // 剩余时间
    float GetElapsed() const;    // 已经过时间

private:
    float duration;     // 总时长
    float elapsed;      // 已经过时间

    bool running;
    bool finished;

    Callback callback;
};