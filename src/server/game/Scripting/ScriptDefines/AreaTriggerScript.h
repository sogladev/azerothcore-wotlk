/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SCRIPT_OBJECT_AREA_TRIGGER_SCRIPT_H_
#define SCRIPT_OBJECT_AREA_TRIGGER_SCRIPT_H_

#include "ScriptObject.h"

class AreaTriggerScript : public ScriptObject
{
protected:
    AreaTriggerScript(const char* name);

public:
    [[nodiscard]] bool IsDatabaseBound() const override { return true; }

    // Called when the area trigger is activated by a player.
    [[nodiscard]] virtual bool OnTrigger(Player* /*player*/, AreaTrigger const* /*trigger*/) { return false; }
};

template <typename T>
class GenericAreaTriggerScript : public AreaTriggerScript
{
public:
    GenericAreaTriggerScript(const char* name) : AreaTriggerScript(name), script(name) { }

    bool OnTrigger(Player* player, AreaTrigger const* at) override
    {
        return script.OnTrigger(player, at);
    }

private:
    T script;
};

#define RegisterAreaTriggerScript(at_name) new GenericAreaTriggerScript<at_name>(#at_name)
#define PrepareAreaTriggerScript(CLASSNAME) CLASSNAME(const char* name) : AreaTriggerScript(name) {}

class OnlyOnceAreaTriggerScript : public AreaTriggerScript
{
    using AreaTriggerScript::AreaTriggerScript;

public:
    [[nodiscard]] bool OnTrigger(Player* /*player*/, AreaTrigger const* /*trigger*/) override;

protected:
    virtual bool _OnTrigger(Player* /*player*/, AreaTrigger const* /*trigger*/) = 0;
    void ResetAreaTriggerDone(InstanceScript* /*instance*/, uint32 /*triggerId*/);
    void ResetAreaTriggerDone(Player const* /*player*/, AreaTrigger const* /*trigger*/);
};

template <typename T>
class GenericOnlyOnceAreaTriggerScript : public OnlyOnceAreaTriggerScript
{
public:
    GenericOnlyOnceAreaTriggerScript(const char* name) : OnlyOnceAreaTriggerScript(name), script(name) { }

    bool _OnTrigger(Player* player, AreaTrigger const* at) override
    {
        return script._OnTrigger(player, at);
    }

private:
    T script;
};

#define RegisterOnlyOnceAreaTriggerScript(at_name) new GenericOnlyOnceAreaTriggerScript<at_name>(#at_name)
#define PrepareOnlyOnceAreaTriggerScript(CLASSNAME) CLASSNAME(const char* name) : OnlyOnceAreaTriggerScript(name) {}

#endif
