/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SCRIPT_REGISTRY_SWAP_HOOKS_H_
#define _SCRIPT_REGISTRY_SWAP_HOOKS_H_

#include "Define.h"
#include <string>

/// What a script type needs done to the live objects it owns when a script
/// context is released or committed. Every type in the registry type list names
/// one, which keeps the answer to "what happens to this type on a swap" in the
/// one place that already enumerates the types.
enum class ScriptSwapPolicy : uint8
{
    Immediate,      // swap on commit: everything not listed below
    CreatureAI,     // two-pass AI swap over all maps
    GameObjectAI,   // two-pass AI swap over all maps
    CommandCache,   // invalidate the chat-command map on release and commit
    OutdoorPvP,     // shut the system down before, re-initialize after
    Lazy,           // instance scripts and spell/aura script loaders
    Unsupported     // battleground and battlefield: assert on release
};

/// The swap hooks of one script type. The primary template does nothing, which
/// is the whole job of an Immediate type; a type whose policy has to prepare or
/// repair live objects specialises it beside the registry type list, in
/// ScriptMgr.cpp, because the list names the types and is private to that file.
template<typename ScriptType>
struct ScriptSwapHooks
{
    /// Called before the scripts of the given context are unregistered.
    static void BeforeReleaseContext(std::string const& /*context*/) { }

    /// Called before the change set collected so far is applied.
    static void BeforeSwapContext(bool /*initialize*/) { }

    /// Called before the registries drop every script.
    static void BeforeUnload() { }
};

#endif
