#include "Timer.h"

Timer::Timer()
    : duration(0.0f),
      elapsed(0.0f),
      running(false),
      finished(false)
{
}

Timer::Timer(float duration, Callback cb)
{
    Start(duration, cb);
}

void Timer::Start(float d, Callback cb)
{
    duration = d;
    callback = cb;

    elapsed = 0.0f;
    running = true;
    finished = false;
}

void Timer::Stop()
{
    running = false;
}

void Timer::Resume()
{
    if (!finished)
        running = true;
}

void Timer::Reset()
{
    elapsed = 0.0f;
    running = false;
    finished = false;
}

void Timer::Update(float dt)
{
    // 外部可先用 IsRunning() 判断再调用
    if (!running || finished)
        return;

    elapsed += dt;

    if (elapsed >= duration)
    {
        elapsed = duration;
        running = false;
        finished = true;

        if (callback)
            callback();
    }
}

bool Timer::IsRunning() const
{
    return running;
}

bool Timer::IsFinished() const
{
    return finished;
}

float Timer::GetProgress() const
{
    if (duration <= 0.0f)
        return 1.0f;

    return elapsed / duration;
}

float Timer::GetRemaining() const
{
    return duration - elapsed;
}

float Timer::GetElapsed() const
{
    return elapsed;
}