// WindowShake - shakes the focused window when workspace switch is blocked
#pragma once

#include "globals.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// Animates a window shake effect on the focused window
// Runs on a separate thread to avoid blocking Hyprland
class WindowShake {
public:
    WindowShake();
    ~WindowShake();

    WindowShake(const WindowShake&) = delete;
    WindowShake& operator=(const WindowShake&) = delete;

    // Set shake params: intensity (pixels), duration (ms), frequency (ms)
    void configure(int intensityPx, int durationMs, int frequencyMs);

    // Shake the focused window (async, returns immediately)
    void shake();

    // Shake a specific window
    void shakeWindow(PHLWINDOW pWindow);

    bool isShaking() const { return m_isShaking.load(); }
    void stopShake();

private:
    void shakeLoop();
    void performShake(PHLWINDOW pWindow);

    // Configuration
    int m_intensity{15};       // Pixels
    int m_duration{300};       // Milliseconds
    int m_frequency{50};       // Milliseconds per oscillation

    // State
    std::atomic<bool> m_isShaking{false};
    std::atomic<bool> m_shouldStop{false};
    PHLWINDOW m_targetWindow;
    
    // Thread management
    std::thread m_shakeThread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};
