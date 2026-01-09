// WorkspaceEnforcer - manages which workspaces are allowed during focus
#pragma once

#include "globals.hpp"
#include <algorithm>
#include <mutex>
#include <set>
#include <vector>

class WorkspaceEnforcer {
public:
    WorkspaceEnforcer() = default;
    ~WorkspaceEnforcer() = default;

    void setAllowedWorkspaces(const std::vector<WORKSPACEID>& workspaceIds);
    void addAllowedWorkspace(WORKSPACEID workspaceId);
    void removeAllowedWorkspace(WORKSPACEID workspaceId);
    void clearAllowedWorkspaces();
    std::vector<WORKSPACEID> getAllowedWorkspaces() const;
    bool isWorkspaceAllowed(WORKSPACEID workspaceId) const;

    void addExceptionClass(const std::string& windowClass);
    void removeExceptionClass(const std::string& windowClass);
    void clearExceptionClasses();
    bool isWindowClassExempt(const std::string& windowClass) const;
    bool isWindowExempt(PHLWINDOW pWindow) const;

    // Returns true if the switch should be BLOCKED
    bool shouldBlockSwitch(WORKSPACEID targetWorkspaceId) const;

    WORKSPACEID getLastValidWorkspace() const { return m_lastValidWorkspace; }
    void setLastValidWorkspace(WORKSPACEID workspaceId) { m_lastValidWorkspace = workspaceId; }
    void setFloatingExempt(bool exempt) { m_floatingExempt = exempt; }
    void setEnforceDuringBreak(bool enforce) { m_enforceDuringBreak = enforce; }

private:
    mutable std::mutex m_mutex;
    std::set<WORKSPACEID> m_allowedWorkspaces;
    std::set<std::string> m_exceptionClasses;
    WORKSPACEID m_lastValidWorkspace{1};
    bool m_floatingExempt{true};
    bool m_enforceDuringBreak{false};
};
