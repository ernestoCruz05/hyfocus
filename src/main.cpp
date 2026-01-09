#include "globals.hpp"
#include "dispatchers.hpp"
#include "eventhooks.hpp"
#include "FocusTimer.hpp"
#include "WorkspaceEnforcer.hpp"
#include "WindowShake.hpp"
#include "ExitChallenge.hpp"

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static void validateConfig() {
    std::vector<std::string> warnings;
    
    if (g_fe_total_duration < 1) {
        warnings.push_back("total_duration should be >= 1 minute");
        g_fe_total_duration = 120;
    }
    
    if (g_fe_work_interval < 1) {
        warnings.push_back("work_interval should be >= 1 minute");
        g_fe_work_interval = 25;
    }
    
    if (g_fe_break_interval < 0) {
        warnings.push_back("break_interval should be >= 0");
        g_fe_break_interval = 5;
    }
    
    if (g_fe_shake_intensity < 1 || g_fe_shake_intensity > 100) {
        warnings.push_back("shake_intensity should be 1-100 pixels");
        g_fe_shake_intensity = std::clamp(g_fe_shake_intensity, 1, 100);
    }
    
    for (const auto& warning : warnings) {
        FE_WARN("Config warning: {}", warning);
        showWarning(warning);
    }
}

/**
 * @brief Plugin initialization entry point.
 */
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;
    
    FE_INFO("HyFocus plugin initializing...");
    
    // Version compatibility check
    // HYPRLAND_API_VERSION is used by the plugin system to ensure API compatibility.
    // The plugin loader will refuse to load plugins built against incompatible API versions.
    // We log the version here for debugging purposes.
    FE_INFO("Built against Hyprland API version: {}", HYPRLAND_API_VERSION);
    
    // Note: Runtime version checking is handled by Hyprland's plugin system.
    // If this plugin loads successfully, the API version is compatible.
    // HyFocus requires Hyprland v0.53.0+ due to API changes in hook registration.
    
    // Register configuration options
    #define CONF(NAME, VALUE) \
        HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyfocus:" NAME, {VALUE})
    
    // Timer settings (in minutes)
    CONF("total_duration", 120L);     // 2 hours default
    CONF("work_interval", 25L);       // Pomodoro standard
    CONF("break_interval", 5L);       // Short break
    
    // Enforcement settings
    CONF("enforce_during_break", 0L); // Allow all workspaces during breaks
    
    // Animation settings
    CONF("shake_intensity", 15L);     // Pixels
    CONF("shake_duration", 300L);     // Milliseconds
    CONF("shake_frequency", 50L);     // Oscillation period in ms
    
    // EWW integration settings
    CONF("use_eww_notifications", 1L);   // Use EWW widgets instead of Hyprland notifications
    CONF("eww_config_path", "NONE");     // Path to EWW config directory (set to actual path to enable)
    
    // Exception classes (comma-separated string)
    CONF("exception_classes", "eww,rofi,wofi,dmenu,ulauncher");
    
    // Spawn blocking settings
    CONF("block_spawn", 1L);          // Block app launching by default
    CONF("spawn_whitelist", "NONE");  // Apps allowed to launch (comma-separated)
    
    // Exit challenge settings (makes stopping annoying to discourage quitting)
    // 0 = disabled, 1 = type phrase, 2 = math problem, 3 = countdown confirmations
    CONF("exit_challenge_type", 0L);
    CONF("exit_challenge_phrase", "I want to stop focusing");
    
    #undef CONF
    
    // Register a callback that fires after config is reloaded
    static auto configReloadedCallback = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "configReloaded", [](void* self, SCallbackInfo& info, std::any data) {
            (void)self; (void)info; (void)data;
            
            // Re-read config values
            static const auto* pExitChallengeType = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:exit_challenge_type")->getDataStaticPtr());
            static const auto* pBlockSpawn = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:block_spawn")->getDataStaticPtr());
            static const auto* pEwwConfigPath = (Hyprlang::STRING const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:eww_config_path")->getDataStaticPtr());
            static const auto* pUseEwwNotifications = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:use_eww_notifications")->getDataStaticPtr());
            
            g_fe_exit_challenge_type = **pExitChallengeType;
            g_fe_block_spawn = **pBlockSpawn != 0;
            g_fe_use_eww_notifications = **pUseEwwNotifications != 0;
            
            // Handle "NONE" as empty string
            std::string ewwPath = *pEwwConfigPath;
            g_fe_eww_config_path = (ewwPath == "NONE") ? "" : ewwPath;
            
            // Reconfigure exit challenge
            if (g_fe_exitChallenge) {
                ChallengeType challengeType = static_cast<ChallengeType>(g_fe_exit_challenge_type);
                g_fe_exitChallenge->configure(challengeType, g_fe_exit_challenge_phrase);
            }
        }
    );
    
    // Force a config reload to populate our values
    HyprlandAPI::reloadConfig();
    
    // Read config values DIRECTLY here (matching hycov's working pattern)
    static const auto* pTotalDuration = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:total_duration")->getDataStaticPtr());
    static const auto* pWorkInterval = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:work_interval")->getDataStaticPtr());
    static const auto* pBreakInterval = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:break_interval")->getDataStaticPtr());
    static const auto* pEnforceDuringBreak = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:enforce_during_break")->getDataStaticPtr());
    static const auto* pShakeIntensity = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:shake_intensity")->getDataStaticPtr());
    static const auto* pShakeDuration = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:shake_duration")->getDataStaticPtr());
    static const auto* pShakeFrequency = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:shake_frequency")->getDataStaticPtr());
    static const auto* pBlockSpawn = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:block_spawn")->getDataStaticPtr());
    static const auto* pExitChallengeType = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:exit_challenge_type")->getDataStaticPtr());
    static const auto* pExceptionClasses = (Hyprlang::STRING const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:exception_classes")->getDataStaticPtr());
    static const auto* pSpawnWhitelist = (Hyprlang::STRING const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:spawn_whitelist")->getDataStaticPtr());
    static const auto* pExitChallengePhrase = (Hyprlang::STRING const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:exit_challenge_phrase")->getDataStaticPtr());
    static const auto* pUseEwwNotifications = (Hyprlang::INT* const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:use_eww_notifications")->getDataStaticPtr());
    static const auto* pEwwConfigPath = (Hyprlang::STRING const*)(HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyfocus:eww_config_path")->getDataStaticPtr());
    
    // Apply values to globals
    g_fe_total_duration = **pTotalDuration;
    g_fe_work_interval = **pWorkInterval;
    g_fe_break_interval = **pBreakInterval;
    g_fe_enforce_during_break = **pEnforceDuringBreak != 0;
    g_fe_shake_intensity = **pShakeIntensity;
    g_fe_shake_duration = **pShakeDuration;
    g_fe_shake_frequency = **pShakeFrequency;
    g_fe_block_spawn = **pBlockSpawn != 0;
    g_fe_exit_challenge_type = **pExitChallengeType;
    g_fe_exit_challenge_phrase = *pExitChallengePhrase;
    g_fe_use_eww_notifications = **pUseEwwNotifications != 0;
    
    // Handle "NONE" as empty string for eww_config_path
    std::string ewwPath = *pEwwConfigPath;
    g_fe_eww_config_path = (ewwPath == "NONE") ? "" : ewwPath;
    
    FE_INFO("Config loaded: exit_challenge={}, use_eww={}, eww_path={}", 
            g_fe_exit_challenge_type, g_fe_use_eww_notifications, g_fe_eww_config_path);
    
    // Parse exception classes
    std::string exceptionStr = *pExceptionClasses;
    if (!exceptionStr.empty()) {
        std::stringstream ss(exceptionStr);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t start = token.find_first_not_of(" \t");
            size_t end = token.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                g_fe_exception_classes.insert(token.substr(start, end - start + 1));
            }
        }
    }
    
    // Parse spawn whitelist
    std::string spawnWhitelistStr = *pSpawnWhitelist;
    if (!spawnWhitelistStr.empty()) {
        std::stringstream ss(spawnWhitelistStr);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t start = token.find_first_not_of(" \t");
            size_t end = token.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                g_fe_spawn_whitelist.insert(token.substr(start, end - start + 1));
            }
        }
    }
    
    FE_INFO("Config loaded: exit_challenge_type={}, block_spawn={}", 
            g_fe_exit_challenge_type, g_fe_block_spawn);
    
    // Validate configuration
    validateConfig();
    
    // Initialize core components
    g_fe_timer = new FocusTimer();
    g_fe_enforcer = new WorkspaceEnforcer();
    g_fe_shaker = new WindowShake();;
    g_fe_exitChallenge = new ExitChallenge();
    
    // Configure components
    g_fe_timer->configure(g_fe_total_duration, g_fe_work_interval, g_fe_break_interval);
    g_fe_shaker->configure(g_fe_shake_intensity, g_fe_shake_duration, g_fe_shake_frequency);
    g_fe_enforcer->setEnforceDuringBreak(g_fe_enforce_during_break);
    
    // Initialize IPC pipe for EWW
    initPipe();
    
    // Configure exit challenge
    ChallengeType challengeType = static_cast<ChallengeType>(g_fe_exit_challenge_type);
    g_fe_exitChallenge->configure(challengeType, g_fe_exit_challenge_phrase);
    
    // Add exception classes from config
    for (const auto& cls : g_fe_exception_classes) {
        g_fe_enforcer->addExceptionClass(cls);
    }
    
    // Register dispatchers (user commands)
    registerDispatchers();
    
    // Register event hooks (workspace interception)
    std::vector<std::string> hookErrors;
    try {
        registerEventHooks(hookErrors);
    } catch (const std::exception& e) {
        hookErrors.push_back(std::string("Exception during hook registration: ") + e.what());
    }
    
    // Report any hook registration issues
    if (!hookErrors.empty()) {
        for (const auto& err : hookErrors) {
            FE_WARN("Hook warning: {}", err);
        }
        showWarning("Some features may not work. Check logs.");
    }
    
    // Reload config to ensure everything is properly set
    HyprlandAPI::reloadConfig();
    
    FE_INFO("HyFocus plugin initialized successfully!");
    
    return {
        "hyfocus",
        "Pomodoro focus timer with workspace enforcement",
        "faky (github.com/ernestoCruz05)",
        "0.1.0"
    };
}

/**
 * @brief Plugin cleanup entry point.
 */
APICALL EXPORT void PLUGIN_EXIT() {
    FE_INFO("HyFocus plugin shutting down...");
    
    // First, mark session as inactive to stop all processing
    g_fe_is_session_active = false;
    
    // Cleanup IPC pipe
    cleanupPipe();
    removeStateFile();
    
    // Stop any running timer (do this BEFORE deleting)
    if (g_fe_timer) {
        g_fe_timer->stop();
    }
    
    // Stop any ongoing shake animation and wait for thread
    if (g_fe_shaker) {
        g_fe_shaker->stopShake();
        // Give thread time to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Cancel any pending exit challenge
    if (g_fe_exitChallenge) {
        g_fe_exitChallenge->cancelChallenge();
    }
    
    // Unregister hooks BEFORE deleting objects they might reference
    unregisterEventHooks();
    
    // Small delay to ensure hooks are fully unregistered
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    // Now safe to delete
    delete g_fe_timer;
    delete g_fe_enforcer;
    delete g_fe_shaker;
    delete g_fe_exitChallenge;
    g_fe_timer = nullptr;
    g_fe_enforcer = nullptr;
    g_fe_shaker = nullptr;
    g_fe_exitChallenge = nullptr;
    
    FE_INFO("HyFocus plugin shutdown complete");
}
