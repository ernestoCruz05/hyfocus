// dispatchers.hpp - command handlers for hyfocus keybinds
#pragma once

#include "globals.hpp"
#include "FocusTimer.hpp"
#include "WorkspaceEnforcer.hpp"
#include "WindowShake.hpp"
#include "ExitChallenge.hpp"

void dispatch_startSession(std::string args);    // hyfocus:start [workspaces@duration]
void dispatch_stopSession(std::string args);     // hyfocus:stop
void dispatch_pauseSession(std::string args);    // hyfocus:pause
void dispatch_resumeSession(std::string args);   // hyfocus:resume
void dispatch_toggleSession(std::string args);   // hyfocus:toggle
void dispatch_allowWorkspace(std::string args);  // hyfocus:allow <id>
void dispatch_disallowWorkspace(std::string args); // hyfocus:disallow <id>
void dispatch_addException(std::string args);    // hyfocus:except <class>
void dispatch_showStatus(std::string args);      // hyfocus:status
void dispatch_confirmStop(std::string args);     // hyfocus:confirm <answer>
void dispatch_allowApp(std::string args);        // hyfocus:allowapp <app>
void dispatch_disallowApp(std::string args);     // hyfocus:disallowapp <app>

void registerDispatchers();
