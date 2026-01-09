// globals.hpp - shared state and helpers for hyfocus
#pragma once

#include <hyprland/src/includes.hpp>
#include <any>

// std headers before private->public hack
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define private public
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/managers/HookSystemManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/devices/Keyboard.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprutils/string/String.hpp>
#undef private

#include "log.hpp"

class FocusTimer;
class WorkspaceEnforcer;
class WindowShake;
class ExitChallenge;

inline HANDLE PHANDLE = nullptr;

// Timer config (minutes)
inline int g_fe_total_duration = 120;
inline int g_fe_work_interval = 25;
inline int g_fe_break_interval = 5;

// Workspace enforcement
inline std::vector<WORKSPACEID> g_fe_allowed_workspaces;
inline std::set<std::string> g_fe_exception_classes;
inline bool g_fe_enforce_during_break = false;

// App spawn blocking (experimental - whitelist doesn't work yet)
inline bool g_fe_block_spawn = true;
inline std::set<std::string> g_fe_spawn_whitelist;

// Exit challenge
inline int g_fe_exit_challenge_type = 0;  // 0=none, 1=phrase, 2=math, 3=countdown
inline std::string g_fe_exit_challenge_phrase = "I want to stop focusing";

// Animation
inline int g_fe_shake_intensity = 15;
inline int g_fe_shake_duration = 300;
inline int g_fe_shake_frequency = 50;
inline bool g_fe_use_eww_notifications = true;
inline std::string g_fe_eww_config_path = "";

// State
inline std::atomic<bool> g_fe_is_session_active{false};
inline std::atomic<bool> g_fe_is_break_time{false};
inline std::atomic<bool> g_fe_is_shaking{false};
inline WORKSPACEID g_fe_last_valid_workspace = 1;

// Shared resources
inline FocusTimer* g_fe_timer = nullptr;
inline WorkspaceEnforcer* g_fe_enforcer = nullptr;
inline WindowShake* g_fe_shaker = nullptr;
inline ExitChallenge* g_fe_exitChallenge = nullptr;
inline std::mutex g_fe_mutex;

// Hooks
inline CFunctionHook* g_fe_pSpawnHook = nullptr;

// Helpers
inline void execAsync(const std::string& cmd) {
    std::thread([cmd]() {
        system(cmd.c_str());
    }).detach();
}

inline void triggerEww(const std::string& action, const std::string& args = "") {
    if (g_fe_eww_config_path.empty()) return;
    std::string cmd = "eww -c " + g_fe_eww_config_path + " " + action;
    if (!args.empty()) cmd += " " + args;
    execAsync(cmd);
}

inline void showFlash(const std::string& msg = "Stay focused", int durationMs = 1200) {
    if (g_fe_use_eww_notifications && !g_fe_eww_config_path.empty()) {
        std::string cmd = g_fe_eww_config_path + "/scripts/show-flash \"" + msg + "\" " + std::to_string(durationMs);
        execAsync(cmd);
    }
}

inline void showNotification(const std::string& msg, CHyprColor color = {0.2, 0.8, 0.2, 1.0}, uint64_t timeMs = 3000) {
    if (!g_fe_use_eww_notifications) {
        HyprlandAPI::addNotification(PHANDLE, "[hyfocus] " + msg, color, timeMs);
    }
}

inline void showError(const std::string& msg) {
    showNotification(msg, {1.0, 0.2, 0.2, 1.0}, 5000);
}

inline void showWarning(const std::string& msg) {
    showNotification(msg, {1.0, 0.7, 0.0, 1.0}, 4000);
}

// Named pipe for EWW IPC
inline std::string g_fe_pipe_path = "";

inline void initPipe() {
    const char* runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir) runtimeDir = "/tmp";
    g_fe_pipe_path = std::string(runtimeDir) + "/hyfocus.pipe";
    
    // Remove old pipe if exists, create new one
    unlink(g_fe_pipe_path.c_str());
    mkfifo(g_fe_pipe_path.c_str(), 0666);
}

inline void writeToPipe(const std::string& json) {
    if (g_fe_pipe_path.empty()) return;
    
    // Open in non-blocking mode, write, close immediately
    int fd = open(g_fe_pipe_path.c_str(), O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        std::string msg = json + "\n";
        write(fd, msg.c_str(), msg.size());
        close(fd);
    }
}

inline void cleanupPipe() {
    if (!g_fe_pipe_path.empty()) {
        unlink(g_fe_pipe_path.c_str());
    }
}

inline void writeStateFile(bool active, const std::string& state, int remainingSecs, const std::vector<WORKSPACEID>& workspaces = {}) {
    int mins = remainingSecs / 60;
    int secs = remainingSecs % 60;
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", mins, secs);
    
    std::string wsArr = "[";
    for (size_t i = 0; i < workspaces.size(); ++i) {
        if (i > 0) wsArr += ",";
        wsArr += std::to_string(workspaces[i]);
    }
    wsArr += "]";
    
    std::ostringstream json;
    json << "{\"active\": " << (active ? "true" : "false")
         << ", \"state\": \"" << state << "\""
         << ", \"remaining\": \"" << timeStr << "\""
         << ", \"workspaces\": " << wsArr << "}";
    
    // Write to pipe (for deflisten)
    writeToPipe(json.str());
    
    // Also write to file (fallback for polling)
    const char* runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir) runtimeDir = "/tmp";
    std::string path = std::string(runtimeDir) + "/hyfocus-state.json";
    std::ofstream f(path);
    if (f.is_open()) {
        f << json.str();
        f.close();
    }
}

inline void removeStateFile() {
    const char* runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir) runtimeDir = "/tmp";
    std::string path = std::string(runtimeDir) + "/hyfocus-state.json";
    std::remove(path.c_str());
}
