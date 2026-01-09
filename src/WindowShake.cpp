#include "WindowShake.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

WindowShake::WindowShake() = default;

WindowShake::~WindowShake() {
    stopShake();  // Now handles thread join internally
}

void WindowShake::configure(int intensityPx, int durationMs, int frequencyMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_intensity = std::max(1, intensityPx);
    m_duration = std::max(50, durationMs);
    m_frequency = std::max(10, frequencyMs);
    
    FE_DEBUG("Shake configured: intensity={}px, duration={}ms, frequency={}ms",
             m_intensity, m_duration, m_frequency);
}

void WindowShake::shake() {
    // Get the currently focused window
    auto focusState = Desktop::focusState();
    if (!focusState) {
        FE_WARN("focusState is null, cannot shake window");
        return;
    }
    auto pWindow = focusState->window();
    if (!pWindow) {
        FE_DEBUG("No focused window to shake");
        return;
    }
    
    shakeWindow(pWindow);
}

void WindowShake::shakeWindow(PHLWINDOW pWindow) {
    if (!pWindow) {
        return;
    }
    
    // Don't start a new shake if one is already in progress
    if (m_isShaking.exchange(true)) {
        FE_DEBUG("Shake already in progress, ignoring");
        return;
    }
    
    m_targetWindow = pWindow;
    m_shouldStop = false;
    
    // Clean up any previous thread
    if (m_shakeThread.joinable()) {
        m_shakeThread.join();
    }
    
    // Start shake animation on a new thread
    m_shakeThread = std::thread(&WindowShake::shakeLoop, this);
    
    FE_DEBUG("Started shake animation");
}

void WindowShake::stopShake() {
    m_shouldStop = true;
    m_cv.notify_all();

    // Wait for the shake thread to actually finish
    if (m_shakeThread.joinable()) {
        m_shakeThread.join();
    }
}

void WindowShake::shakeLoop() {
    if (!m_targetWindow) {
        m_isShaking = false;
        return;
    }
    
    performShake(m_targetWindow);
    
    m_isShaking = false;
    m_targetWindow = nullptr;
    
    FE_DEBUG("Shake animation completed");
}

/**
 * @brief Execute the shake animation on a window.
 * 
 * The animation works by manipulating the window's position goal.
 * We store the original position, then apply oscillating offsets
 * over the duration of the animation.
 * 
 * The offset formula creates a decaying sinusoidal motion:
 *   offset(t) = intensity * sin(2π * t / period) * (1 - t/duration)
 * 
 * The decay factor (1 - t/duration) makes the shake gradually diminish,
 * creating a more natural "settling" effect.
 * 
 * @param pWindow The window to animate
 */
void WindowShake::performShake(PHLWINDOW pWindow) {
    if (!pWindow || !pWindow->m_realPosition) {
        return;
    }

    // Store original position
    Vector2D originalPos = pWindow->m_realPosition->goal();

    auto startTime = std::chrono::steady_clock::now();
    auto endTime = startTime + std::chrono::milliseconds(m_duration);

    const double period = static_cast<double>(m_frequency);
    const double totalDuration = static_cast<double>(m_duration);

    while (!m_shouldStop.load()) {
        auto now = std::chrono::steady_clock::now();

        if (now >= endTime) {
            break;
        }

        // Calculate elapsed time in milliseconds
        double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime).count();

        // Calculate sinusoidal offset with decay
        // The decay makes the shake gradually diminish
        double decay = 1.0 - (elapsed / totalDuration);
        double phase = (2.0 * M_PI * elapsed) / period;
        double offset = m_intensity * std::sin(phase) * decay;

        // Apply offset to window position
        Vector2D newPos = originalPos;
        newPos.x += offset;

        // Use the animated variable's warp to instantly set position
        // This bypasses normal animation for immediate effect
        pWindow->m_realPosition->setValueAndWarp(newPos);

        // Interruptible sleep: wait on condition variable instead of sleep_for
        // This allows stopShake() to wake us up immediately
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_cv.wait_for(lock, std::chrono::milliseconds(16), [this]() {
                return m_shouldStop.load();
            })) {
                // Woke up because m_shouldStop is true - exit loop
                break;
            }
        }
    }

    // Restore original position (always, even if interrupted)
    if (pWindow && pWindow->m_realPosition) {
        pWindow->m_realPosition->setValueAndWarp(originalPos);

        // Schedule a render to ensure the window is properly restored
        if (g_pHyprRenderer) {
            g_pHyprRenderer->damageWindow(pWindow);
        } else {
            FE_WARN("g_pHyprRenderer is null, cannot damage window");
        }
    }
}
