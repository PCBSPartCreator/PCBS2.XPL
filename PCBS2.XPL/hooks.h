#pragma once
#include <set>
#include <string>
#include <vector>
#include "config.h"

bool Hooks_Install();
void Hooks_SetPendingMods(std::vector<ModFile>* mods);
void Hooks_SetSaveFixEnabled(bool enabled);

// All loaded part IDs (valid PartInstance.m_partId values). Populated on the
// first PartsDatabase.Load; ready once Hooks_PartIdsReady() returns true.
const std::set<std::string>& Hooks_GetValidIds();
bool Hooks_PartIdsReady();