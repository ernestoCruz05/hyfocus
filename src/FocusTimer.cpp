#include "FocusTimer.hpp"

FocusTimer::FocusTimer() = default;

FocusTimer::~FocusTimer() {
    stop();
}

void FocusTimer::configure(int totalMinutes, int workMinutes, int breakMinutes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Validate inputs
    if (totalMinutes < 1) totalMinutes = 1;
    if (workMinutes < 1) workMinutes = 1;
    if (breakMinutes < 0) breakMinutes = 0;
    
    m_totalDuration = std::chrono::minutes(totalMinutes);
    m_workInterval = std::chrono::minutes(workMinutes);
    m_breakInterval = std::chrono::minutes(breakMinutes);
    
    FE_INFO("Timer configured: {}min total, {}min work, {}min break",
            totalMinutes, workMinutes, breakMinutes);
}

bool FocusTimer::start() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_state.load() != TimerState::Idle && m_state.load() != TimerState::Completed) {
            FE_WARN("Cannot start timer: already running or paused");
            return false;
        }
        
        m_shouldStop = false;
        m_completedWorkIntervals = 0;
        m_sessionStart = std::chrono::steady_clock::now();
        m_intervalStart = m_sessionStart;
        m_state = TimerState::Working;
    }
    
    // Start the timer thread
    if (m_timerThread.joinable()) {
        m_timerThread.join();
    }
    m_timerThread = std::thread(&FocusTimer::timerLoop, this);
    
    FE_INFO("Focus session started");
    invokeCallback(m_onWorkStart);
    
    return true;
}

void FocusTimer::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shouldStop = true;
        m_state = TimerState::Idle;
    }
    m_cv.notify_all();
    
    if (m_timerThread.joinable()) {
        m_timerThread.join();
    }
    
    FE_INFO("Focus session stopped");
}

void FocusTimer::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state.load() != TimerState::Working && m_state.load() != TimerState::Break) {
        return;
    }
    
    // Calculate remaining time in current interval
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_intervalStart);
    auto intervalDuration = (m_state.load() == TimerState::Working) 
                             ? m_workInterval 
                             : m_breakInterval;
    m_pausedRemaining = std::chrono::duration_cast<std::chrono::seconds>(intervalDuration) - elapsed;
    
    m_state = TimerState::Paused;
    m_cv.notify_all();
    
    FE_INFO("Timer paused with {} seconds remaining in interval", m_pausedRemaining.count());
}

void FocusTimer::resume() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state.load() != TimerState::Paused) {
        return;
    }
    
    // Adjust interval start to account for paused time
    auto now = std::chrono::steady_clock::now();
    auto intervalDuration = (m_completedWorkIntervals % 2 == 0) 
                             ? m_workInterval 
                             : m_breakInterval;
    m_intervalStart = now - (std::chrono::duration_cast<std::chrono::seconds>(intervalDuration) - m_pausedRemaining);
    
    // Resume to previous state (working if even intervals completed, break if odd)
    m_state = (m_completedWorkIntervals % 2 == 0) ? TimerState::Working : TimerState::Break;
    m_cv.notify_all();
    
    FE_INFO("Timer resumed");
}

int FocusTimer::getRemainingSeconds() const {
    if (m_state.load() == TimerState::Paused) {
        return static_cast<int>(m_pausedRemaining.count());
    }
    
    if (!isRunning()) {
        return 0;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_intervalStart);
    auto intervalDuration = (m_state.load() == TimerState::Working) 
                             ? m_workInterval 
                             : m_breakInterval;
    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(intervalDuration) - elapsed;
    
    return std::max(0, static_cast<int>(remaining.count()));
}

int FocusTimer::getElapsedSeconds() const {
    if (m_state.load() == TimerState::Idle) {
        return 0;
    }
    
    auto now = std::chrono::steady_clock::now();
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - m_sessionStart).count()
    );
}

void FocusTimer::timerLoop() {
    FE_DEBUG("Timer thread started");
    
    while (!m_shouldStop.load()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        // Wait for 1 second or until notified (for pause/stop)
        m_cv.wait_for(lock, std::chrono::seconds(1), [this]() {
            return m_shouldStop.load() || m_state.load() == TimerState::Paused;
        });
        
        if (m_shouldStop.load()) {
            break;
        }
        
        if (m_state.load() == TimerState::Paused) {
            continue;  // Stay in loop but don't progress time
        }
        
        auto now = std::chrono::steady_clock::now();
        
        // Check if current interval has elapsed FIRST (before total check)
        // This ensures we transition to break before session ends
        auto intervalElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_intervalStart);
        auto currentInterval = (m_state.load() == TimerState::Working) 
                                ? std::chrono::duration_cast<std::chrono::seconds>(m_workInterval)
                                : std::chrono::duration_cast<std::chrono::seconds>(m_breakInterval);
        
        if (intervalElapsed >= currentInterval) {
            if (m_state.load() == TimerState::Working) {
                m_completedWorkIntervals++;
                transitionToBreak();
            } else {
                // Break ended - start next work cycle
                transitionToWork();
            }
            m_intervalStart = now;
        }
        
        // Invoke tick callback (for UI updates)
        if (m_onTick) {
            int remaining = getRemainingSeconds();
            lock.unlock();
            m_onTick(remaining / 60, m_state.load());
        }
    }
    
    FE_DEBUG("Timer thread exiting");
}

void FocusTimer::transitionToWork() {
    m_state = TimerState::Working;
    FE_INFO("Work interval started");
    invokeCallback(m_onWorkStart);
}

void FocusTimer::transitionToBreak() {
    m_state = TimerState::Break;
    FE_INFO("Break interval started");
    invokeCallback(m_onBreakStart);
}

void FocusTimer::invokeCallback(const Callback& cb) {
    if (cb) {
        try {
            cb();
        } catch (const std::exception& e) {
            FE_ERR("Callback threw exception: {}", e.what());
        }
    }
}
