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
#include "Errors.h"

#ifndef ACORE_API_USE_DYNAMIC_LINKING

// A statically linked server loads no script library, so there is no module
// reference any script could have come from.
std::shared_ptr<ModuleReference>
    ScriptReloadMgr::AcquireModuleReferenceOfContext(std::string const& /*context*/)
{
    WPAbort();
}

// Returns the empty implemented ScriptReloadMgr
ScriptReloadMgr* ScriptReloadMgr::instance()
{
    static ScriptReloadMgr instance;
    return &instance;
}

#else

// The library loading, unloading and the source watcher are added on top of the
// manager by the later steps of the hotswap port; until they exist the dynamic
// build only needs the manager to be present and reachable.
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
