// eventhooks.hpp - workspace change interception
#pragma once

#include "globals.hpp"
#include <vector>
#include <string>

void registerEventHooks(std::vector<std::string>& errors);
void enableEnforcementHooks();
void disableEnforcementHooks();
void unregisterEventHooks();
