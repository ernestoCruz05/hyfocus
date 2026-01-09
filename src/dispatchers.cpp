#include "dispatchers.hpp"
#include "eventhooks.hpp"
#include "FocusTimer.hpp"
#include <sstream>

static std::vector<WORKSPACEID> parseWorkspaceList(const std::string& input) {
    std::vector<WORKSPACEID> result;
    std::stringstream ss(input);
    std::string token;
    
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        
        if (start != std::string::npos && end != std::string::npos) {
            token = token.substr(start, end - start + 1);
            
            try {
                WORKSPACEID id = std::stoi(token);
                if (id < 1) {
                    FE_WARN("Invalid workspace ID '{}': must be >= 1", token);
                    continue;
                }
                result.push_back(id);
            } catch (const std::exception& e) {
                FE_WARN("Failed to parse workspace ID '{}': {}", token, e.what());
            }
        }
    }
    
    return result;
}

/**
 * @brief Format remaining seconds as MM:SS string.
 */
static std::string formatTime(int seconds) {
    int mins = seconds / 60;
    int secs = seconds % 60;
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << mins
        << ":" << std::setfill('0') << std::setw(2) << secs;
    return oss.str();
}

void dispatch_startSession(std::string args) {
    FE_INFO("Starting focus session with args: '{}'", args);
    
    // Check if already running
    if (g_fe_is_session_active.load()) {
        showWarning("Focus session already running! Stop it first.");
        return;
    }
    
    // Parse args: "workspaces@duration" or just "workspaces"
    std::string workspaceStr = args;
    int sessionDuration = g_fe_work_interval; // Default from config
    
    size_t atPos = args.find('@');
    if (atPos != std::string::npos) {
        workspaceStr = args.substr(0, atPos);
        std::string durationStr = args.substr(atPos + 1);
        try {
            sessionDuration = std::stoi(durationStr);
            FE_INFO("Using duration from args: {} minutes", sessionDuration);
        } catch (...) {
            FE_WARN("Failed to parse duration '{}', using default", durationStr);
        }
    }
    
    // Parse allowed workspaces
    std::vector<WORKSPACEID> allowedWorkspaces;
    
    if (workspaceStr.empty()) {
        // Default to current workspace only
        auto focusState = Desktop::focusState();
        if (!focusState) {
            FE_WARN("focusState is null, cannot determine current workspace");
        } else {
            auto pMonitor = focusState->monitor();
            if (pMonitor && pMonitor->m_activeWorkspace) {
                allowedWorkspaces.push_back(pMonitor->m_activeWorkspace->m_id);
                FE_INFO("No workspaces specified, using current: {}", 
                        pMonitor->m_activeWorkspace->m_id);
            }
        }
    } else {
        allowedWorkspaces = parseWorkspaceList(workspaceStr);
    }
    
    if (allowedWorkspaces.empty()) {
        showError("No valid workspaces specified!");
        return;
    }
    
    // Configure the enforcer
    if (!g_fe_enforcer) {
        FE_ERR("g_fe_enforcer is null, cannot start session");
        showError("Internal error: enforcer not initialized");
        return;
    }
    g_fe_enforcer->setAllowedWorkspaces(allowedWorkspaces);
    
    // Store current workspace as last valid
    auto focusState = Desktop::focusState();
    if (!focusState) {
        FE_WARN("focusState is null, cannot determine current workspace for last valid");
    } else {
        auto pMonitor = focusState->monitor();
        if (pMonitor && pMonitor->m_activeWorkspace) {
            g_fe_enforcer->setLastValidWorkspace(pMonitor->m_activeWorkspace->m_id);
        }
    }
    
    // Configure and start the timer
    // Pomodoro: work duration from user, break auto-calculated (1:5 ratio)
    // e.g., 25 min work -> 5 min break, 50 min -> 10 min break
    int breakDuration = std::max(1, sessionDuration / 5);
    int totalDuration = sessionDuration + breakDuration; // One full Pomodoro cycle
    g_fe_timer->configure(totalDuration, sessionDuration, breakDuration);
    
    FE_INFO("Pomodoro: {} min work, {} min break, {} min total", 
            sessionDuration, breakDuration, totalDuration);
    
    // Store allowed workspaces for state file (need a copy for lambda)
    static std::vector<WORKSPACEID> s_allowedWorkspaces;
    s_allowedWorkspaces = allowedWorkspaces;
    
    // Set up callbacks
    g_fe_timer->setOnWorkStart([]() {
        g_fe_is_break_time = false;
        writeStateFile(true, "working", g_fe_timer->getRemainingSeconds(), s_allowedWorkspaces);
        // Only show flash if this is resuming from break (not initial start)
        if (g_fe_timer->getElapsedSeconds() > 5) {
            showFlash("Back to work!", 2000);
        }
        showNotification("Focus time! Stay on task.", {0.2, 0.6, 1.0, 1.0});
    });
    
    g_fe_timer->setOnBreakStart([]() {
        g_fe_is_break_time = true;
        writeStateFile(true, "break", g_fe_timer->getRemainingSeconds(), s_allowedWorkspaces);
        showFlash("Take a break!", 2500);
        showNotification("Break time! Relax for a moment.", {0.2, 0.8, 0.2, 1.0});
    });
    
    g_fe_timer->setOnSessionComplete([]() {
        g_fe_is_session_active = false;
        g_fe_is_break_time = false;
        disableEnforcementHooks();
        removeStateFile();
        showNotification("Focus session complete! Great work!", {1.0, 0.8, 0.0, 1.0}, 10000);
        
        // Close status widgets on all monitors if using EWW
        if (g_fe_use_eww_notifications && !g_fe_eww_config_path.empty()) {
            execAsync("eww -c " + g_fe_eww_config_path + " close hyfocus-status");
            execAsync("eww -c " + g_fe_eww_config_path + " close hyfocus-status-2");
        }
    });
    
    // Set tick callback to update state file every second
    g_fe_timer->setOnTick([](int remainingMins, TimerState state) {
        std::string stateStr = (state == TimerState::Working) ? "working" :
                               (state == TimerState::Break) ? "break" :
                               (state == TimerState::Paused) ? "paused" : "inactive";
        int remainingSecs = g_fe_timer->getRemainingSeconds();
        writeStateFile(true, stateStr, remainingSecs, s_allowedWorkspaces);
    });
    
    // Start!
    if (g_fe_timer->start()) {
        g_fe_is_session_active = true;
        
        // Enable enforcement hooks now that session is active
        enableEnforcementHooks();
        
        // Build workspace list for notification
        std::string wsStr;
        for (auto id : allowedWorkspaces) {
            if (!wsStr.empty()) wsStr += ", ";
            wsStr += std::to_string(id);
        }
        
        // Write initial state file
        writeStateFile(true, "working", sessionDuration * 60, allowedWorkspaces);
        
        showNotification("Focus session started! Allowed workspaces: " + wsStr);
        
        // Open status widgets on ALL monitors if using EWW (each window separately)
        if (g_fe_use_eww_notifications && !g_fe_eww_config_path.empty()) {
            execAsync("eww -c " + g_fe_eww_config_path + " open hyfocus-status");
            execAsync("eww -c " + g_fe_eww_config_path + " open hyfocus-status-2");
        }
    } else {
        showError("Failed to start focus session!");
    }
}

void dispatch_stopSession(std::string args) {
    FE_INFO("dispatch_stopSession called with args: '{}'", args);
    
    if (!g_fe_is_session_active.load()) {
        showWarning("No focus session is running.");
        return;
    }
    
    // Check for force stop (bypasses challenge)
    bool forceStop = (args.find("force") != std::string::npos);
    
    // Check if exit challenge is enabled and not already active
    if (!forceStop && g_fe_exitChallenge) {
        FE_INFO("ExitChallenge exists, isEnabled={}, isActive={}, type={}", 
                g_fe_exitChallenge->isEnabled(), 
                g_fe_exitChallenge->isChallengeActive(),
                static_cast<int>(g_fe_exitChallenge->getChallengeType()));
        
        if (g_fe_exitChallenge->isEnabled()) {
            if (!g_fe_exitChallenge->isChallengeActive()) {
                // Start the challenge
                std::string prompt = g_fe_exitChallenge->initiateChallenge();
                FE_INFO("Exit challenge initiated: {}", prompt);
                
                // Trigger EWW challenge widget if configured
                if (g_fe_use_eww_notifications && !g_fe_eww_config_path.empty()) {
                    triggerEww("open hyfocus-challenge");
                    // Also run the show-challenge script to generate the problem
                    execAsync(g_fe_eww_config_path + "/scripts/show-challenge");
                } else {
                    showWarning(prompt);
                }
                return;  // Don't stop yet, wait for confirmation
            } else {
                // Challenge already active, remind user
                if (g_fe_use_eww_notifications && !g_fe_eww_config_path.empty()) {
                    triggerEww("open hyfocus-challenge");
                } else {
                    showWarning("Complete the challenge first! Use: hyfocus:confirm <answer>");
                }
                return;
            }
        }
    }
    
    // No challenge, challenge disabled, or force stop - stop immediately
    g_fe_timer->stop();
    g_fe_is_session_active = false;
    g_fe_is_break_time = false;
    
    // Disable enforcement hooks
    disableEnforcementHooks();
    
    // Remove state file and close status widgets
    removeStateFile();
    if (g_fe_use_eww_notifications && !g_fe_eww_config_path.empty()) {
        execAsync("eww -c " + g_fe_eww_config_path + " close hyfocus-status");
        execAsync("eww -c " + g_fe_eww_config_path + " close hyfocus-status-2");
    }
    
    int elapsed = g_fe_timer->getElapsedSeconds();
    showNotification("Focus session stopped. Total time: " + formatTime(elapsed));
}

void dispatch_pauseSession(std::string args) {
    (void)args;
    
    if (!g_fe_is_session_active.load()) {
        showWarning("No focus session is running.");
        return;
    }
    
    g_fe_timer->pause();
    showNotification("Focus session paused.", {1.0, 0.7, 0.0, 1.0});
}

void dispatch_resumeSession(std::string args) {
    (void)args;
    
    if (g_fe_timer->getState() != TimerState::Paused) {
        showWarning("Session is not paused.");
        return;
    }
    
    g_fe_timer->resume();
    showNotification("Focus session resumed!");
}

void dispatch_toggleSession(std::string args) {
    if (g_fe_is_session_active.load()) {
        dispatch_stopSession("");
    } else {
        dispatch_startSession(args);
    }
}

void dispatch_allowWorkspace(std::string args) {
    if (args.empty()) {
        showError("Please specify a workspace ID.");
        return;
    }
    
    try {
        WORKSPACEID id = std::stoi(args);
        if (id < 1) {
            showError("Invalid workspace ID: must be >= 1");
            FE_WARN("Invalid workspace ID '{}': must be >= 1", args);
            return;
        }
        g_fe_enforcer->addAllowedWorkspace(id);
        showNotification("Workspace " + std::to_string(id) + " added to allowed list.");
    } catch (const std::exception& e) {
        showError("Invalid workspace ID: " + args);
        FE_WARN("Failed to parse workspace ID '{}': {}", args, e.what());
    }
}

void dispatch_disallowWorkspace(std::string args) {
    if (args.empty()) {
        showError("Please specify a workspace ID.");
        return;
    }
    
    try {
        WORKSPACEID id = std::stoi(args);
        if (id < 1) {
            showError("Invalid workspace ID: must be >= 1");
            FE_WARN("Invalid workspace ID '{}': must be >= 1", args);
            return;
        }
        g_fe_enforcer->removeAllowedWorkspace(id);
        showNotification("Workspace " + std::to_string(id) + " removed from allowed list.");
    } catch (const std::exception& e) {
        showError("Invalid workspace ID: " + args);
        FE_WARN("Failed to parse workspace ID '{}': {}", args, e.what());
    }
}

void dispatch_addException(std::string args) {
    if (args.empty()) {
        showError("Please specify a window class.");
        return;
    }
    
    g_fe_enforcer->addExceptionClass(args);
    showNotification("Window class '" + args + "' added to exceptions.");
}

void dispatch_showStatus(std::string args) {
    (void)args;
    
    std::ostringstream status;
    
    if (!g_fe_is_session_active.load()) {
        status << "No active focus session.";
    } else {
        auto state = g_fe_timer->getState();
        int remaining = g_fe_timer->getRemainingSeconds();
        int elapsed = g_fe_timer->getElapsedSeconds();
        
        status << "Session: ";
        
        switch (state) {
            case TimerState::Working:
                status << "WORKING";
                break;
            case TimerState::Break:
                status << "BREAK";
                break;
            case TimerState::Paused:
                status << "PAUSED";
                break;
            default:
                status << "UNKNOWN";
        }
        
        status << " | Remaining: " << formatTime(remaining);
        status << " | Elapsed: " << formatTime(elapsed);
        
        // Add allowed workspaces
        auto allowed = g_fe_enforcer->getAllowedWorkspaces();
        status << " | Workspaces: ";
        for (size_t i = 0; i < allowed.size(); i++) {
            if (i > 0) status << ", ";
            status << allowed[i];
        }
    }
    
    showNotification(status.str(), {0.5, 0.7, 1.0, 1.0}, 5000);
    FE_INFO("Status: {}", status.str());
}

void dispatch_confirmStop(std::string args) {
    if (!g_fe_exitChallenge) {
        showError("Exit challenge system not initialized.");
        return;
    }
    
    if (!g_fe_exitChallenge->isChallengeActive()) {
        showWarning("No active challenge. Use hyfocus:stop first.");
        return;
    }
    
    if (args.empty()) {
        showWarning("Please provide an answer: hyfocus:confirm <answer>");
        return;
    }
    
    bool passed = g_fe_exitChallenge->validateAnswer(args);
    
    if (passed) {
        // Challenge passed! Actually stop the session now
        g_fe_timer->stop();
        g_fe_is_session_active = false;
        g_fe_is_break_time = false;
        
        // Disable enforcement hooks
        disableEnforcementHooks();
        
        int elapsed = g_fe_timer->getElapsedSeconds();
        showNotification("Challenge passed! Session stopped. Total time: " + formatTime(elapsed));
    } else {
        // Check if it's a countdown that needs more confirmations
        if (g_fe_exitChallenge->getChallengeType() == ChallengeType::Countdown) {
            int remaining = g_fe_exitChallenge->getRemainingAttempts();
            if (remaining > 0) {
                showWarning("Keep going! " + std::to_string(remaining) + " more confirmations needed.");
                return;
            }
        }
        
        // Wrong answer
        showError("Wrong answer! " + g_fe_exitChallenge->getHint());
    }
}

void dispatch_allowApp(std::string args) {
    if (args.empty()) {
        showError("Please specify an app name/command.");
        return;
    }
    
    g_fe_spawn_whitelist.insert(args);
    showNotification("App '" + args + "' added to spawn whitelist.");
    FE_INFO("Added app to spawn whitelist: {}", args);
}

void dispatch_disallowApp(std::string args) {
    if (args.empty()) {
        showError("Please specify an app name/command.");
        return;
    }
    
    g_fe_spawn_whitelist.erase(args);
    showNotification("App '" + args + "' removed from spawn whitelist.");
    FE_INFO("Removed app from spawn whitelist: {}", args);
}

void registerDispatchers() {
    FE_INFO("Registering dispatchers...");
    
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:start", dispatch_startSession);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:stop", dispatch_stopSession);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:pause", dispatch_pauseSession);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:resume", dispatch_resumeSession);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:toggle", dispatch_toggleSession);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:allow", dispatch_allowWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:disallow", dispatch_disallowWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:except", dispatch_addException);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:status", dispatch_showStatus);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:confirm", dispatch_confirmStop);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:allowapp", dispatch_allowApp);
    HyprlandAPI::addDispatcher(PHANDLE, "hyfocus:disallowapp", dispatch_disallowApp);
    
    FE_INFO("Dispatchers registered successfully");
}
