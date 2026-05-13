#pragma once
#include <vector>
#include "config.h"

bool Hooks_Install();
void Hooks_SetPendingMods(std::vector<ModPart>* mods);