// FocusTimer - Pomodoro timer that loops work/break cycles
#pragma once

#include "globals.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

enum class TimerState {
    Idle,
    Working,
    Break,
    Paused,
    Completed
};

class FocusTimer {
public:
    using Callback = std::function<void()>;
    using TickCallback = std::function<void(int minutesRemaining, TimerState state)>;

    FocusTimer();
    ~FocusTimer();

    FocusTimer(const FocusTimer&) = delete;
    FocusTimer& operator=(const FocusTimer&) = delete;

    void configure(int totalMinutes, int workMinutes, int breakMinutes);
    bool start();
    void stop();
    void pause();
    void resume();

    TimerState getState() const { return m_state.load(); }
    int getRemainingSeconds() const;
    int getElapsedSeconds() const;
    bool isBreakTime() const { return m_state.load() == TimerState::Break; }
    bool isRunning() const {
        auto state = m_state.load();
        return state == TimerState::Working || state == TimerState::Break;
    }

    void setOnWorkStart(Callback cb) { m_onWorkStart = std::move(cb); }
    void setOnBreakStart(Callback cb) { m_onBreakStart = std::move(cb); }
    void setOnSessionComplete(Callback cb) { m_onSessionComplete = std::move(cb); }
    void setOnTick(TickCallback cb) { m_onTick = std::move(cb); }

private:
    void timerLoop();
    void transitionToWork();
    void transitionToBreak();
    void invokeCallback(const Callback& cb);

    std::chrono::minutes m_totalDuration{120};
    std::chrono::minutes m_workInterval{25};
    std::chrono::minutes m_breakInterval{5};

    std::atomic<TimerState> m_state{TimerState::Idle};
    std::chrono::steady_clock::time_point m_sessionStart;
    std::chrono::steady_clock::time_point m_intervalStart;
    std::chrono::seconds m_pausedRemaining{0};
    int m_completedWorkIntervals{0};

    std::thread m_timerThread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_shouldStop{false};

    Callback m_onWorkStart;
    Callback m_onBreakStart;
    Callback m_onSessionComplete;
    TickCallback m_onTick;
};
