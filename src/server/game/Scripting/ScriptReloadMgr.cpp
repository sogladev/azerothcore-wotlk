/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptReloadMgr.h"
#include "Config.h"
#include "Errors.h"
#include "ScriptMgr.h"
#include "World.h"

#ifndef ACORE_API_USE_DYNAMIC_LINKING

// A statically linked server loads no script library, so there is no module
// reference any script could have come from.
std::shared_ptr<ModuleReference>
    ScriptReloadMgr::AcquireModuleReferenceOfContext(std::string const& /*context*/)
{
    WPAbort();
}

// Nothing is watched or loaded in a static build, so there is nothing to set up.
void ScriptReloadMgr::Initialize()
{
}

// A statically linked server never loads a script library, so it never has a
// change set waiting for a commit either.
void ScriptReloadMgr::Update()
{
}

// The settings only matter to a reloader that can load libraries; an operator
// who asked for hotswapping here only gets told why it does not happen.
void ScriptReloadMgr::LoadSettings()
{
    if (sWorld->getBoolConfig(CONFIG_HOTSWAP_ENABLED))
    {
        LOG_WARN("scripts.hotswap", "HotSwap.Enabled is set, but this server was built without "
            "dynamic linking, so no script library can be loaded.");
    }
}

// Returns the empty implemented ScriptReloadMgr
ScriptReloadMgr* ScriptReloadMgr::instance()
{
    static ScriptReloadMgr instance;
    return &instance;
}

#else

// Starts nothing yet: the scripts are registered before the world configuration
// is read, so the manager only announces that it is waiting for it.
void ScriptReloadMgr::Initialize()
{
    LOG_INFO("scripts.hotswap", "Script hotswapping is waiting for the world configuration.");
}

// Resolves the settings the reloader runs with. Loading the libraries that are
// installed already and starting the watchers comes right after this.
void ScriptReloadMgr::LoadSettings()
{
    if (!sWorld->getBoolConfig(CONFIG_HOTSWAP_ENABLED))
    {
        LOG_INFO("scripts.hotswap", "Script hotswapping is disabled by HotSwap.Enabled.");
        return;
    }

    std::string const scriptDirectory = sConfigMgr->GetOption<std::string>("HotSwap.ScriptDir", "scripts");

    LOG_INFO("scripts.hotswap", "Script hotswapping is enabled: script directory '{}', recompiler {}.",
        scriptDirectory, sWorld->getBoolConfig(CONFIG_HOTSWAP_RECOMPILER_ENABLED) ? "enabled" : "disabled");
}

// The library loading, unloading and the source watcher are added on top of the
// manager by the later steps of the hotswap port; until they exist the dynamic
// build only needs the manager to be present and reachable, and to retry a swap
// the world refused while database scripts were running.
void ScriptReloadMgr::Update()
{
    if (sScriptMgr->IsSwapPending())
        sScriptMgr->SwapScriptContext();
}

std::shared_ptr<ModuleReference>
    ScriptReloadMgr::AcquireModuleReferenceOfContext(std::string const& /*context*/)
{
    return nullptr;
}

ScriptReloadMgr* ScriptReloadMgr::instance()
{
    static ScriptReloadMgr instance;
    return &instance;
}

#endif // ACORE_API_USE_DYNAMIC_LINKING
