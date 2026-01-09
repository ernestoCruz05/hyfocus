#include "WorkspaceEnforcer.hpp"

void WorkspaceEnforcer::setAllowedWorkspaces(const std::vector<WORKSPACEID>& workspaceIds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allowedWorkspaces.clear();
    m_allowedWorkspaces.insert(workspaceIds.begin(), workspaceIds.end());
    
    std::string ids;
    for (auto id : workspaceIds) {
        if (!ids.empty()) ids += ", ";
        ids += std::to_string(id);
    }
    FE_INFO("Allowed workspaces set: [{}]", ids);
}

void WorkspaceEnforcer::addAllowedWorkspace(WORKSPACEID workspaceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allowedWorkspaces.insert(workspaceId);
    FE_DEBUG("Added workspace {} to allowed list", workspaceId);
}

void WorkspaceEnforcer::removeAllowedWorkspace(WORKSPACEID workspaceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allowedWorkspaces.erase(workspaceId);
    FE_DEBUG("Removed workspace {} from allowed list", workspaceId);
}

void WorkspaceEnforcer::clearAllowedWorkspaces() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allowedWorkspaces.clear();
    FE_DEBUG("Cleared all allowed workspaces");
}

std::vector<WORKSPACEID> WorkspaceEnforcer::getAllowedWorkspaces() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::vector<WORKSPACEID>(m_allowedWorkspaces.begin(), m_allowedWorkspaces.end());
}

bool WorkspaceEnforcer::isWorkspaceAllowed(WORKSPACEID workspaceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Special workspaces (negative IDs) are always allowed
    if (workspaceId < 0) {
        return true;
    }
    
    return m_allowedWorkspaces.contains(workspaceId);
}

void WorkspaceEnforcer::addExceptionClass(const std::string& windowClass) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_exceptionClasses.insert(windowClass);
    FE_DEBUG("Added exception class: {}", windowClass);
}

void WorkspaceEnforcer::removeExceptionClass(const std::string& windowClass) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_exceptionClasses.erase(windowClass);
    FE_DEBUG("Removed exception class: {}", windowClass);
}

void WorkspaceEnforcer::clearExceptionClasses() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_exceptionClasses.clear();
    FE_DEBUG("Cleared all exception classes");
}

bool WorkspaceEnforcer::isWindowClassExempt(const std::string& windowClass) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_exceptionClasses.contains(windowClass);
}

bool WorkspaceEnforcer::isWindowExempt(PHLWINDOW pWindow) const {
    if (!pWindow) {
        return true;  // No window = no enforcement needed
    }
    
    // Check if window class is in exception list
    // Try both m_class and m_initialClass for better matching
    std::string windowClass = pWindow->m_initialClass;
    if (isWindowClassExempt(windowClass)) {
        FE_DEBUG("Window {} exempt by class", windowClass);
        return true;
    }
    
    // Check if floating windows are exempt
    if (m_floatingExempt && pWindow->m_isFloating) {
        FE_DEBUG("Window exempt (floating)");
        return true;
    }
    
    // Special workspaces are always accessible
    if (pWindow->m_workspace && pWindow->m_workspace->m_isSpecialWorkspace) {
        FE_DEBUG("Window exempt (special workspace)");
        return true;
    }
    
    return false;
}

bool WorkspaceEnforcer::shouldBlockSwitch(WORKSPACEID targetWorkspaceId) const {
    // Not blocking if no session is active
    if (!g_fe_is_session_active.load()) {
        return false;
    }
    
    // During breaks, check if enforcement is enabled
    if (g_fe_is_break_time.load() && !m_enforceDuringBreak) {
        return false;  // Allow switches during breaks
    }
    
    // Check if target workspace is allowed
    if (isWorkspaceAllowed(targetWorkspaceId)) {
        return false;  // Workspace is in the allowed list
    }
    
    // Block the switch!
    FE_INFO("Blocked switch to workspace {} (not in allowed list)", targetWorkspaceId);
    return true;
}
