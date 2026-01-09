#include "eventhooks.hpp"
#include "dispatchers.hpp"
#include "FocusTimer.hpp"
#include "WorkspaceEnforcer.hpp"
#include "WindowShake.hpp"

#include <stdexcept>

static std::atomic<bool> g_isReverting{false};

static void onWorkspaceChange(void* self, SCallbackInfo& info, std::any data) {
    (void)self;
    (void)info;
    
    // Debug log
    std::ofstream dbg("/tmp/hyfocus_debug.log", std::ios::app);
    dbg << "onWorkspaceChange called, session_active=" << g_fe_is_session_active.load() << std::endl;
    
    try {
        auto pWorkspace = std::any_cast<PHLWORKSPACE>(data);
        if (!pWorkspace) {
            dbg << "pWorkspace is null" << std::endl;
            dbg.close();
            return;
        }
        
        WORKSPACEID newWsId = pWorkspace->m_id;
        dbg << "newWsId=" << newWsId << std::endl;
        
        // If we're currently reverting, just update tracking and exit
        if (g_isReverting.load()) {
            if (g_fe_enforcer && g_fe_enforcer->isWorkspaceAllowed(newWsId)) {
                g_fe_enforcer->setLastValidWorkspace(newWsId);
            }
            dbg << "reverting, skip" << std::endl;
            dbg.close();
            return;
        }
        
        // If no session active, just track the workspace
        if (!g_fe_is_session_active.load()) {
            if (g_fe_enforcer) {
                g_fe_enforcer->setLastValidWorkspace(newWsId);
            }
            dbg << "no session active" << std::endl;
            dbg.close();
            return;
        }
        dbg.close();
        
        // During breaks, check if enforcement is active
        if (g_fe_is_break_time.load() && !g_fe_enforce_during_break) {
            if (g_fe_enforcer) {
                g_fe_enforcer->setLastValidWorkspace(newWsId);
            }
            return;
        }
        
        // Check if this workspace is allowed
        std::ofstream dbg2("/tmp/hyfocus_debug.log", std::ios::app);
        dbg2 << "checking isAllowed, enforcer=" << (g_fe_enforcer != nullptr) << std::endl;
        
        if (g_fe_enforcer && g_fe_enforcer->isWorkspaceAllowed(newWsId)) {
            g_fe_enforcer->setLastValidWorkspace(newWsId);
            dbg2 << "allowed, returning" << std::endl;
            dbg2.close();
            FE_DEBUG("Allowed switch to workspace {}", newWsId);
            return;
        }
        
        // BLOCKED! Revert to last valid workspace
        WORKSPACEID lastValid = g_fe_enforcer ? g_fe_enforcer->getLastValidWorkspace() : 1;
        dbg2 << "BLOCKED! reverting to " << lastValid << std::endl;
        dbg2 << "use_eww=" << g_fe_use_eww_notifications << " path=" << g_fe_eww_config_path << std::endl;
        dbg2.close();
        FE_INFO("Blocked switch to workspace {}, reverting to {}", newWsId, lastValid);
        
        // Trigger shake animation
        if (g_fe_shaker) {
            g_fe_shaker->shake();
        }
        
        // Show flash warning (EWW) or notification (fallback)
        if (g_fe_use_eww_notifications && !g_fe_eww_config_path.empty()) {
            showFlash("Stay focused");
        } else {
            showWarning("Focus mode: Workspace " + std::to_string(newWsId) + " is restricted!");
        }
        
        // Set revert guard to prevent infinite loop
        g_isReverting.store(true);
        
        // Revert by dispatching a workspace change back to the allowed workspace
        HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + std::to_string(lastValid));
        
        // Clear revert guard
        g_isReverting.store(false);
        
    } catch (const std::bad_any_cast& e) {
        FE_WARN("Failed to cast workspace data: {}", e.what());
    } catch (const std::exception& e) {
        FE_WARN("Exception processing workspace change: {}", e.what());
    } catch (...) {
        FE_WARN("Unknown exception processing workspace change event");
    }
}

/**
 * @brief Hook function that intercepts spawn (app launch) requests.
 * 
 * When focus enforcement is active and spawn blocking is enabled,
 * this prevents launching new applications to keep the user focused.
 * 
 * ## Spawn Blocking Logic
 * 
 * 1. If no session is active, allow all spawns
 * 2. If blocking is disabled (g_fe_block_spawn = false), allow all spawns
 * 3. If during break and enforcement during break is disabled, allow spawns
 * 4. Check if the command is in the spawn whitelist (e.g., "firefox", "kitty")
 * 5. If not whitelisted, block the spawn and show visual feedback
 * 
 * The whitelist checks if any whitelisted app name is contained in the
 * spawn command, allowing for flexibility (e.g., "firefox" matches
 * "firefox --new-window https://example.com").
 * 
 * @param args The spawn command string
 */
static void hkSpawn(std::string args) {
    // If no session active, always allow
    if (!g_fe_is_session_active.load()) {
        if (g_fe_pSpawnHook && g_fe_pSpawnHook->m_original) {
            ((void(*)(std::string))g_fe_pSpawnHook->m_original)(args);
        }
        return;
    }
    
    // Check if spawn blocking is enabled
    if (!g_fe_block_spawn) {
        if (g_fe_pSpawnHook && g_fe_pSpawnHook->m_original) {
            ((void(*)(std::string))g_fe_pSpawnHook->m_original)(args);
        }
        return;
    }
    
    // During breaks, check if enforcement is active
    if (g_fe_is_break_time.load() && !g_fe_enforce_during_break) {
        if (g_fe_pSpawnHook && g_fe_pSpawnHook->m_original) {
            ((void(*)(std::string))g_fe_pSpawnHook->m_original)(args);
        }
        return;
    }
    
    // Check whitelist - see if any whitelisted app is in the command
    std::string argsLower = args;
    std::transform(argsLower.begin(), argsLower.end(), argsLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    // Debug whitelist check
    std::ofstream dbg("/tmp/hyfocus_debug.log", std::ios::app);
    dbg << "hkSpawn: args='" << args << "' whitelist_size=" << g_fe_spawn_whitelist.size() << std::endl;
    for (const auto& item : g_fe_spawn_whitelist) {
        dbg << "  whitelist item: '" << item << "'" << std::endl;
    }
    
    for (const auto& allowed : g_fe_spawn_whitelist) {
        std::string allowedLower = allowed;
        std::transform(allowedLower.begin(), allowedLower.end(), allowedLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        
        if (argsLower.find(allowedLower) != std::string::npos) {
            // Whitelisted app found, allow spawn
            dbg << "  ALLOWED: '" << allowed << "' found in args" << std::endl;
            dbg.close();
            FE_DEBUG("Spawn allowed (whitelisted): {}", args);
            if (g_fe_pSpawnHook && g_fe_pSpawnHook->m_original) {
                ((void(*)(std::string))g_fe_pSpawnHook->m_original)(args);
            }
            return;
        }
    }
    dbg << "  BLOCKED - no whitelist match" << std::endl;
    dbg.close();
    
    // BLOCKED! Trigger visual feedback
    FE_INFO("Blocked spawn: {}", args);
    
    // Trigger shake animation
    if (g_fe_shaker) {
        g_fe_shaker->shake();
    }
    
    // Show flash warning (EWW) or notification (fallback)
    if (g_fe_use_eww_notifications && !g_fe_eww_config_path.empty()) {
        showFlash("Stay focused");
    } else {
        showWarning("Focus mode: App launching is blocked!");
    }
    
    // Do NOT call the original function - spawn is prevented
}

/**
 * @brief Safely create a function hook with error handling.
 */
template<typename T>
static CFunctionHook* createHookSafe(
    const char* name,
    T memberFunc,
    void* hookFunc,
    std::vector<std::string>& errors
) {
    auto hook = HyprlandAPI::createFunctionHook(PHANDLE, memberFunc, hookFunc);
    if (!hook) {
        errors.push_back(std::string("Failed to create hook for ") + name);
        return nullptr;
    }
    if (!hook->hook()) {
        errors.push_back(std::string("Failed to enable hook for ") + name);
        return nullptr;
    }
    FE_DEBUG("Successfully hooked {}", name);
    return hook;
}

void registerEventHooks(std::vector<std::string>& errors) {
    FE_INFO("Registering event hooks...");
    
    // NOTE: We no longer hook changeworkspace - we use a revert strategy instead.
    // The workspace callback detects unauthorized switches and reverts them.
    // This is more stable than trying to intercept and block the function call.
    
    // Hook the spawn function to block app launching during focus
    // This hook IS stable because spawn is a simpler function
    static const auto spawnMethods = HyprlandAPI::findFunctionsByName(PHANDLE, "spawn");
    
    if (!spawnMethods.empty()) {
        g_fe_pSpawnHook = HyprlandAPI::createFunctionHook(
            PHANDLE,
            spawnMethods[0].address,
            (void*)&hkSpawn
        );
        
        if (g_fe_pSpawnHook) {
            // DON'T enable hook here - only create it
            // Hook will be enabled when session starts via enableEnforcementHooks()
            FE_INFO("Created spawn hook (original at {})", 
                    g_fe_pSpawnHook->m_original);
        } else {
            FE_WARN("Failed to create spawn hook - spawn blocking disabled");
        }
    } else {
        FE_WARN("Could not find spawn function - spawn blocking disabled");
    }
    
    // Register callback for post-workspace-change events
    // This is our main enforcement mechanism - revert unauthorized switches
    static auto workspaceCallback = HyprlandAPI::registerCallbackDynamic(
        PHANDLE,
        "workspace",
        onWorkspaceChange
    );
    
    // Debug to file
    std::ofstream dbg("/tmp/hyfocus_debug.log", std::ios::app);
    dbg << "registerEventHooks: workspaceCallback=" << (workspaceCallback ? "OK" : "NULL") << std::endl;
    dbg.close();
    
    if (!workspaceCallback) {
        errors.push_back("Failed to register workspace callback - enforcement disabled");
    } else {
        FE_INFO("Registered workspace callback for enforcement");
    }
    
    FE_INFO("Event hook registration complete ({} errors)", errors.size());
}

// Track hook state ourselves since CFunctionHook doesn't have isHooked()
static bool g_spawnHooked = false;

void enableEnforcementHooks() {
    FE_INFO("Enabling enforcement hooks...");
    
    // Debug
    std::ofstream dbg("/tmp/hyfocus_debug.log", std::ios::app);
    dbg << "enableEnforcementHooks: block_spawn=" << g_fe_block_spawn 
        << " pSpawnHook=" << (g_fe_pSpawnHook != nullptr)
        << " already_hooked=" << g_spawnHooked << std::endl;
    
    // Workspace enforcement is always active via callback - no hook needed
    
    if (g_fe_block_spawn && g_fe_pSpawnHook && !g_spawnHooked) {
        if (g_fe_pSpawnHook->hook()) {
            g_spawnHooked = true;
            dbg << "Spawn hook ENABLED" << std::endl;
            FE_INFO("Enabled spawn hook");
        } else {
            dbg << "Spawn hook FAILED to enable" << std::endl;
            FE_ERR("Failed to enable spawn hook");
        }
    } else {
        dbg << "Spawn hook NOT enabled (conditions not met)" << std::endl;
    }
    dbg.close();
}

void disableEnforcementHooks() {
    FE_INFO("Disabling enforcement hooks...");
    
    // Workspace enforcement is controlled by g_fe_is_session_active flag
    
    if (g_fe_pSpawnHook && g_spawnHooked) {
        g_fe_pSpawnHook->unhook();
        g_spawnHooked = false;
        FE_INFO("Disabled spawn hook");
    }
}

void unregisterEventHooks() {
    FE_INFO("Unregistering event hooks...");
    
    // Make sure hooks are disabled first
    disableEnforcementHooks();
    
    // Then destroy the spawn hook object (no changeworkspace hook anymore)
    g_fe_pSpawnHook = nullptr;
    
    FE_INFO("Event hooks unregistered");
}
