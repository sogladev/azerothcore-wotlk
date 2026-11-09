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

#include "ScriptMgr.h"
#include "AllScriptsObjects.h"
#include "ChatCommand.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "LFGScripts.h"
#include "MapMgr.h"
#include "OutdoorPvPMgr.h"
#include "ScriptRegistrySwapHooks.h"
#include "ScriptSystem.h"
#include "SmartAI.h"
#include "SpellMgr.h"
#include "Utilities/TypeList.h"
#include "UnitAI.h"
#include "Vehicle.h"

namespace
{
    enum Spells
    {
        SPELL_HOTSWAP_VISUAL_SPELL_EFFECT = 40162 // 59084
    };

    // Metadata for the script-registry operations derived in this file.
    // EnabledHooks is the legacy *_HOOK_END count for enabled-hook dispatch.
    // LegacyDbValidationCandidate preserves the old database-validation search set.
    // SwapPolicy says what a release or a commit has to do to the live objects of
    // the type, and therefore which swap hooks it has.
    template<typename Script, uint16 EnabledHookCountValue = 0, bool PromotedAfterDbLoadValue = false, bool LegacyDbValidationValue = false,
        ScriptSwapPolicy SwapPolicyValue = ScriptSwapPolicy::Immediate>
    struct ScriptTypeInfo
    {
        using type = Script;
        static constexpr uint16 EnabledHooks = EnabledHookCountValue;
        static constexpr bool HasEnabledHooks = EnabledHookCountValue > 0;
        static constexpr bool PromotedAfterDbLoad = PromotedAfterDbLoadValue;
        static constexpr bool LegacyDbValidationCandidate = LegacyDbValidationValue;
        static constexpr ScriptSwapPolicy SwapPolicy = SwapPolicyValue;
    };

    //                 script type                hooks                    afterLoad  dbCheck
    // A type whose live objects need more than an immediate swap names its
    // ScriptSwapPolicy in the last column and implements the matching hooks below.
    // Battleground and battlefield scripts are the pair a release refuses. Their
    // aliases stay Immediate: BGScript is only the hook-only AllBattlegroundScript,
    // and BattlegroundMapScript maps a parallel map type whose scripts own no live
    // per-instance object, so nothing of theirs has to be rebuilt across a swap.
    using ScriptRegistryTypes = Acore::type_list<
        ScriptTypeInfo<AccountScript,             ACCOUNTHOOK_END>,
        ScriptTypeInfo<AchievementCriteriaScript, 0,                       true,      true>,
        ScriptTypeInfo<AchievementScript,         ACHIEVEMENTHOOK_END,     false,     true>,
        ScriptTypeInfo<AllCreatureScript>,
        ScriptTypeInfo<AllGameObjectScript>,
        ScriptTypeInfo<AllItemScript>,
        ScriptTypeInfo<AllMapScript,              ALLMAPHOOK_END>,
        ScriptTypeInfo<AreaTriggerScript,         0,                       true,      true>,
        ScriptTypeInfo<ArenaScript,               ARENAHOOK_END,           false,     true>,
        ScriptTypeInfo<ArenaTeamScript,           ARENATEAMHOOK_END,       false,     true>,
        ScriptTypeInfo<AuctionHouseScript,        AUCTIONHOUSEHOOK_END,    false,     true>,
        ScriptTypeInfo<BattlefieldScript, BATTLEFIELDHOOK_END, false, false, ScriptSwapPolicy::Unsupported>,
        ScriptTypeInfo<BGScript,                  ALLBATTLEGROUNDHOOK_END, false,     true>,
        ScriptTypeInfo<BattlegroundMapScript,     0,                       true,      true>,
        ScriptTypeInfo<BattlegroundScript, 0, true, true, ScriptSwapPolicy::Unsupported>,
        ScriptTypeInfo<CommandSC,                 ALLCOMMANDHOOK_END,      false,     true>,
        ScriptTypeInfo<CommandScript, 0, false, true, ScriptSwapPolicy::CommandCache>,
        ScriptTypeInfo<ConditionScript,           0,                       true,      true>,
        ScriptTypeInfo<CreatureScript, 0, true, true, ScriptSwapPolicy::CreatureAI>,
        ScriptTypeInfo<DatabaseScript,            DATABASEHOOK_END,        false,     true>,
        ScriptTypeInfo<DynamicObjectScript,       0,                       false,     true>,
        ScriptTypeInfo<ALEScript>,
        ScriptTypeInfo<FormulaScript,             FORMULAHOOK_END,         false,     true>,
        ScriptTypeInfo<GameEventScript,           GAMEEVENTHOOK_END>,
        ScriptTypeInfo<GameObjectScript, 0, true, true, ScriptSwapPolicy::GameObjectAI>,
        ScriptTypeInfo<GlobalScript,              GLOBALHOOK_END>,
        ScriptTypeInfo<GroupScript,               GROUPHOOK_END,           false,     true>,
        ScriptTypeInfo<GuildScript,               GUILDHOOK_END,           false,     true>,
        ScriptTypeInfo<InstanceMapScript, 0, true, true, ScriptSwapPolicy::Lazy>,
        ScriptTypeInfo<ItemScript,                0,                       true,      true>,
        ScriptTypeInfo<LootScript,                LOOTHOOK_END>,
        ScriptTypeInfo<MailScript,                MAILHOOK_END>,
        ScriptTypeInfo<MiscScript,                MISCHOOK_END,            false,     true>,
        ScriptTypeInfo<ModuleScript>,
        ScriptTypeInfo<MovementHandlerScript,     MOVEMENTHOOK_END>,
        ScriptTypeInfo<OutdoorPvPScript, 0, true, true, ScriptSwapPolicy::OutdoorPvP>,
        ScriptTypeInfo<PetScript,                 PETHOOK_END,             false,     true>,
        ScriptTypeInfo<PlayerScript,              PLAYERHOOK_END,          false,     true>,
        ScriptTypeInfo<ServerScript,              SERVERHOOK_END,          false,     true>,
        ScriptTypeInfo<SessionScript,             SESSIONHOOK_END>,
        ScriptTypeInfo<SpellSC,                   ALLSPELLHOOK_END,        false,     true>,
        ScriptTypeInfo<SpellScriptLoader, 0, true, true, ScriptSwapPolicy::Lazy>,
        ScriptTypeInfo<TicketScript,              TICKETHOOK_END,          false,     true>,
        ScriptTypeInfo<TransportScript,           0,                       true,      true>,
        ScriptTypeInfo<UnitScript,                UNITHOOK_END>,
        ScriptTypeInfo<VehicleScript,             0,                       false,     true>,
        ScriptTypeInfo<WeatherScript,             0,                       true,      true>,
        ScriptTypeInfo<WorldMapScript,            0,                       true,      true>,
        ScriptTypeInfo<WorldObjectScript,         WORLDOBJECTHOOK_END>,
        ScriptTypeInfo<WorldScript,               WORLDHOOK_END,           false,     true>>;

    // These counts mirror the four hand-maintained lists this consolidation
    // replaced. If a flag is mistyped or a type is added without its metadata,
    // the build fails here instead of silently drifting.
    static_assert(Acore::size_v<ScriptRegistryTypes> == 50, "Update count when adding a script registry type");
    static_assert(Acore::count_if<ScriptRegistryTypes>([]<typename Info>() { return Info::HasEnabledHooks; }) == 28, "Enabled-hook script type count changed");
    static_assert(Acore::count_if<ScriptRegistryTypes>([]<typename Info>() { return Info::PromotedAfterDbLoad; }) == 14, "After-load script type count changed");
    static_assert(Acore::count_if<ScriptRegistryTypes>([]<typename Info>() { return Info::LegacyDbValidationCandidate; }) == 34, "Database-check script type count changed");
}

ScriptRegistryCompositum* ScriptRegistryCompositum::Instance()
{
    static ScriptRegistryCompositum instance;
    return &instance;
}

void ScriptRegistryCompositum::Register(ScriptRegistryInterface* registry)
{
    ASSERT(registry);
    _registries.insert(registry);
}

void ScriptRegistryCompositum::SetScriptNameInContext(std::string const& scriptname, std::string const& context)
{
    _scriptnames_to_context[scriptname] = context;
}

std::string const& ScriptRegistryCompositum::GetScriptContextOfScriptName(std::string const& scriptname) const
{
    auto const itr = _scriptnames_to_context.find(scriptname);
    if (itr == _scriptnames_to_context.end())
    {
        // A name no script registered, typically one the database knows only.
        // It belongs to no context and is never released.
        static std::string const noContext;
        return noContext;
    }

    return itr->second;
}

void ScriptRegistryCompositum::ReleaseContext(std::string const& context)
{
    for (ScriptRegistryInterface* registry : _registries)
        registry->ReleaseContext(context);

    // The names leave the map after the registries ran their release hooks, which
    // may still resolve a name, and after every transfer into another context.
    std::size_t releasedNames = 0;
    for (auto itr = _scriptnames_to_context.begin(); itr != _scriptnames_to_context.end();)
    {
        if (itr->second != context)
        {
            ++itr;
            continue;
        }

        itr = _scriptnames_to_context.erase(itr);
        ++releasedNames;
    }

    LOG_DEBUG("scripts", "Released {} script name(s) of the '{}' script context", releasedNames, context);
}

void ScriptRegistryCompositum::SwapContext(bool initialize)
{
    for (ScriptRegistryInterface* registry : _registries)
        registry->SwapContext(initialize);

    DoDelayedDelete();
}

void ScriptRegistryCompositum::RemoveUsedScriptsFromContainer(std::unordered_set<std::string>& scripts)
{
    for (ScriptRegistryInterface* registry : _registries)
        registry->RemoveUsedScriptsFromContainer(scripts);
}

void ScriptRegistryCompositum::Unload()
{
    DoDelayedDelete();
    _scriptnames_to_context.clear();
}

void ScriptRegistryCompositum::DoDelayedDelete()
{
    _delayed_delete_queue.clear();
}

// The script registry containers belong to the worldserver, so they are defined
// once here and not in the header: a script module that instantiated them from the
// header would get a private copy of the hook dispatch and of the registries, and
// the scripts it adds would never be visible to the core.
#define DEFINE_SCRIPT_REGISTRY_STORAGE(ScriptType)                                                  \
    template<> ScriptRegistry<ScriptType>::ScriptMap ScriptRegistry<ScriptType>::ScriptPointerList{}; \
    template<> ScriptRegistry<ScriptType>::ScriptVector ScriptRegistry<ScriptType>::ALScripts{};     \
    template<> ScriptRegistry<ScriptType>::EnabledHooksVector                                        \
    ScriptRegistry<ScriptType>::EnabledHooks{};                                                      \
    template<> std::unordered_multimap<std::string, uint32> ScriptRegistry<ScriptType>::ContextScriptIds{}; \
    template<> std::unordered_set<uint32> ScriptRegistry<ScriptType>::RecentlyAddedScriptIds{};      \
    template<> uint32 ScriptRegistry<ScriptType>::_scriptIdCounter = 0;

DEFINE_SCRIPT_REGISTRY_STORAGE(AccountScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AchievementCriteriaScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AchievementScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(ALEScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AllBattlegroundScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AllCommandScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AllCreatureScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AllGameObjectScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AllItemScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AllMapScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AllSpellScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AreaTriggerScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(ArenaScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(ArenaTeamScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(AuctionHouseScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(BattlefieldScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(BattlegroundMapScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(BattlegroundScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(CommandScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(ConditionScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(CreatureScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(DatabaseScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(DynamicObjectScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(FormulaScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(GameEventScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(GameObjectScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(GlobalScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(GroupScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(GuildScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(InstanceMapScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(ItemScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(LootScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(MailScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(MiscScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(ModuleScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(MovementHandlerScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(OutdoorPvPScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(PetScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(PlayerScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(ServerScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(SessionScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(SpellScriptLoader)
DEFINE_SCRIPT_REGISTRY_STORAGE(TicketScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(TransportScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(UnitScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(VehicleScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(WeatherScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(WorldMapScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(WorldObjectScript)
DEFINE_SCRIPT_REGISTRY_STORAGE(WorldScript)

#undef DEFINE_SCRIPT_REGISTRY_STORAGE

// The swap hooks of the types whose policy is not Immediate. They live here
// because the type list that names the policies is private to this file; a policy
// that resolves to the primary template of ScriptRegistrySwapHooks.h does nothing
// before the change set is applied.

/// The chat command map is built from the command scripts, so it is rebuilt from
/// the registries after them rather than carried across a swap.
template<>
struct ScriptSwapHooks<CommandScript>
{
    static void BeforeReleaseContext(std::string const& /*context*/)
    {
        Acore::ChatCommands::InvalidateCommandMap();
    }

    static void BeforeSwapContext(bool /*initialize*/)
    {
        Acore::ChatCommands::InvalidateCommandMap();
    }

    static void BeforeUnload()
    {
        Acore::ChatCommands::InvalidateCommandMap();
    }
};

/// Outdoor PvP content is rebuilt rather than swapped: the systems are shut down
/// before their scripts disappear and initialized again once the change set is
/// committed.
template<>
struct ScriptSwapHooks<OutdoorPvPScript>
{
    static void BeforeReleaseContext(std::string const& context)
    {
        if (!_swapped && ScriptRegistry<OutdoorPvPScript>::ContextScriptIds.count(context))
        {
            _swapped = true;
            sOutdoorPvPMgr->Die();
        }
    }

    static void BeforeSwapContext(bool initialize)
    {
        // The initial set of scripts is registered, not swapped: the world
        // initializes the systems itself at start-up.
        if (!initialize && _swapped)
        {
            sOutdoorPvPMgr->InitOutdoorPvP();
            _swapped = false;
        }
    }

    static void BeforeUnload()
    {
        ASSERT(!_swapped, "The outdoor pvp systems should have been initialized by now.");
    }

private:
    static inline bool _swapped = false;
};

/// Battleground and battlefield scripts cannot change owners: their live objects
/// carry the library's code for as long as they exist, so releasing a context
/// that still owns them would leave those objects dispatching into an unmapped
/// library.
namespace
{
    template<typename ScriptType>
    struct UnsupportedScriptSwapHooks
    {
        static void BeforeReleaseContext(std::string const& context)
        {
            ASSERT(!ScriptRegistry<ScriptType>::ContextScriptIds.count(context),
                   "A library that owns battleground or battlefield scripts cannot be unloaded.");
        }

        static void BeforeSwapContext(bool /*initialize*/) { }
        static void BeforeUnload() { }
    };
}

template<>
struct ScriptSwapHooks<BattlefieldScript> : UnsupportedScriptSwapHooks<BattlefieldScript> { };

template<>
struct ScriptSwapHooks<BattlegroundScript> : UnsupportedScriptSwapHooks<BattlegroundScript> { };

namespace
{
    /// Shows the players of a swapped creature that its behaviour changed, instead
    /// of resetting it silently.
    class AsyncCastHotswapEffectEvent : public BasicEvent
    {
    public:
        explicit AsyncCastHotswapEffectEvent(Unit* owner) : _owner(owner) { }

        bool Execute(uint64 /*e_time*/, uint32 /*p_time*/) override
        {
            _owner->CastSpell(_owner, SPELL_HOTSWAP_VISUAL_SPELL_EFFECT, true);
            return true;
        }

    private:
        Unit* _owner;
    };

    /// Fails the first swap that needs the visual effect when the client data has
    /// no such spell. The check cannot run at start-up: AC registers its scripts
    /// before it loads the DBC stores.
    void ValidateHotswapVisualSpell()
    {
        static bool validated = false;
        if (validated)
            return;

        validated = true;
        ASSERT(sSpellMgr->GetSpellInfo(SPELL_HOTSWAP_VISUAL_SPELL_EFFECT),
               "The hotswap visual effect spell is missing from the client data.");
    }

    /// The ids of the scripts the given context registered for one script type.
    template<typename ScriptType>
    std::unordered_set<uint32> GetScriptIdsOfContext(std::string const& context)
    {
        std::unordered_set<uint32> ids;

        auto const bounds = ScriptRegistry<ScriptType>::ContextScriptIds.equal_range(context);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            ids.insert(itr->second);

        return ids;
    }

    /// Resolves a stored object of the given type on a map. The object is looked
    /// up again at every use because a reset may have removed the previous one.
    template<typename ObjectType>
    ObjectType* GetEntityFromMap(Map* map, ObjectGuid const& guid)
    {
        if constexpr (std::is_same_v<ObjectType, Creature>)
            return map->GetCreature(guid);
        else
            return map->GetGameObject(guid);
    }

    /// Passes every stored object of the type that uses one of the given script
    /// ids to the visitor; other object types of the map store are ignored.
    template<typename ObjectType, typename Visitor>
    class SwapObjectsWorker
    {
    public:
        SwapObjectsWorker(std::unordered_set<uint32> const& idsToSwap, Visitor const& visitor)
            : _idsToSwap(idsToSwap), _visitor(visitor) { }

        void Visit(std::unordered_map<ObjectGuid, ObjectType*>& objects)
        {
            for (auto const& objectEntry : objects)
            {
                ObjectType* object = objectEntry.second;
                if (_idsToSwap.count(object->GetScriptId()) != 0)
                    _visitor(object);
            }
        }

        template<typename OtherType>
        void Visit(std::unordered_map<ObjectGuid, OtherType*>&) { }

    private:
        std::unordered_set<uint32> const& _idsToSwap;
        Visitor const& _visitor;
    };

    template<typename ObjectType, typename Visitor>
    void VisitObjectsToSwapOnMap(Map* map, std::unordered_set<uint32> const& idsToSwap, Visitor const& visitor)
    {
        SwapObjectsWorker<ObjectType, Visitor> worker(idsToSwap, visitor);
        TypeContainerVisitor<SwapObjectsWorker<ObjectType, Visitor>, MapStoredObjectTypesContainer> containerVisitor(worker);
        containerVisitor.Visit(map->GetObjectsStore());
    }

    /// A creature keeps its own state, so a swap has to put it back in the state a
    /// freshly loaded creature is in, both when its AI is taken away and when the
    /// new one arrives.
    void UnloadResetScript(Creature* creature)
    {
        // Remove deletable events only, otherwise non-deletable spell events die
        // in the middle of a cast.
        creature->m_Events.KillAllEvents(false);

        if (creature->IsCharmed())
        {
            creature->RemoveCharmedBy(nullptr);

            // AC restores the disabled AI on the next update only, and the AI the
            // charm left in place is the charm one. Restore it here, or the AI of
            // the released library stays reachable in i_disabledAI.
            creature->UpdateCharmAI();
        }

        ASSERT(!creature->IsCharmed(), "A charmed creature must not be swapped.");
        ASSERT(!creature->HasDisabledAI(), "The disabled AI of a creature must be restored before it is swapped.");

        if (creature->IsAlive())
            creature->AI()->EnterEvadeMode();
    }

    void UnloadDestroyScript(Creature* creature)
    {
        bool const destroyed = creature->AIM_Destroy();
        ASSERT(destroyed, "Destroying the AI of a creature must not fail here.");
        (void)destroyed;

        ASSERT(!creature->AI(), "The AI of a creature must be null here.");
    }

    void LoadInitializeScript(Creature* creature)
    {
        ASSERT(!creature->AI(), "The AI of a creature must be null here.");

        if (creature->IsAlive())
            creature->ClearUnitState(UNIT_STATE_EVADE);

        bool const created = creature->AIM_Create();
        ASSERT(created, "Creating the AI of a creature must not fail here.");
        (void)created;
    }

    void LoadResetScript(Creature* creature)
    {
        if (!creature->IsAlive())
            return;

        creature->AI()->InitializeAI();

        if (creature->GetVehicleKit())
            creature->GetVehicleKit()->Reset();

        creature->AI()->EnterEvadeMode();

        // Signal the swap to the players instead of resetting the creature silently.
        creature->m_Events.AddEvent(new AsyncCastHotswapEffectEvent(creature), creature->m_Events.CalculateTime(0));
    }

    void UnloadResetScript(GameObject* gameObject)
    {
        // Remove deletable events only, otherwise non-deletable spell events die
        // in the middle of a cast.
        gameObject->m_Events.KillAllEvents(false);

        gameObject->AI()->Reset();
    }

    void UnloadDestroyScript(GameObject* gameObject)
    {
        gameObject->AIM_Destroy();

        ASSERT(!gameObject->AI(), "The AI of a game object must be null here.");
    }

    void LoadInitializeScript(GameObject* gameObject)
    {
        ASSERT(!gameObject->AI(), "The AI of a game object must be null here.");

        gameObject->AIM_Initialize();
    }

    void LoadResetScript(GameObject* gameObject)
    {
        gameObject->AI()->Reset();
    }

    /// Unload pass shared by creature and gameobject scripts: let the objects put
    /// themselves back in order, then drop the AIs of the released scripts.
    template<typename ObjectType>
    void UnloadAIsOfScriptIds(std::unordered_set<uint32> const& idsToRelease)
    {
        if (idsToRelease.empty())
            return;

        sMapMgr->DoForAllMaps([&idsToRelease](Map* map)
        {
            std::vector<ObjectGuid> guidsToReset;

            // An object that is not in the world yet has no AI to reset; it gets
            // one when it is added.
            VisitObjectsToSwapOnMap<ObjectType>(map, idsToRelease, [&guidsToReset](ObjectType* object)
            {
                if (object->AI() && !object->GetGUID().IsEmpty())
                    guidsToReset.push_back(object->GetGUID());
            });

            for (ObjectGuid const& guid : guidsToReset)
                if (ObjectType* object = GetEntityFromMap<ObjectType>(map, guid))
                    UnloadResetScript(object);

            VisitObjectsToSwapOnMap<ObjectType>(map, idsToRelease, [](ObjectType* object)
            {
                UnloadDestroyScript(object);
            });
        });
    }

    /// Load pass shared by creature and gameobject scripts: give every object that
    /// lost its AI one of the newest code, then initialize it like a fresh load.
    template<typename ObjectType>
    void CreateAIsOfScriptIds(std::unordered_set<uint32> const& idsToCreate)
    {
        if (idsToCreate.empty())
            return;

        sMapMgr->DoForAllMaps([&idsToCreate](Map* map)
        {
            std::vector<ObjectGuid> guidsToReset;

            VisitObjectsToSwapOnMap<ObjectType>(map, idsToCreate, [&guidsToReset](ObjectType* object)
            {
                if (!object->AI() && !object->GetGUID().IsEmpty())
                {
                    LoadInitializeScript(object);
                    guidsToReset.push_back(object->GetGUID());
                }
            });

            for (ObjectGuid const& guid : guidsToReset)
            {
                ObjectType* object = GetEntityFromMap<ObjectType>(map, guid);
                if (!object)
                    continue;

                if (!object->AI())
                    LoadInitializeScript(object);

                LoadResetScript(object);
            }
        });
    }

    /// Recreates the AI of a creature or game object when the script that selects
    /// it changes owner. The release takes every live object of the type through an
    /// unload pass, the commit gives the ones that lost their AI a new one, so a
    /// released library's vtable never survives in a live object.
    template<typename ObjectType, typename ScriptType>
    struct AIObjectScriptSwapHooks
    {
        static void BeforeReleaseContext(std::string const& context)
        {
            std::unordered_set<uint32> const idsToRelease = GetScriptIdsOfContext<ScriptType>(context);
            UnloadAIsOfScriptIds<ObjectType>(idsToRelease);
            _idsToRelease.insert(idsToRelease.begin(), idsToRelease.end());
        }

        static void BeforeSwapContext(bool initialize)
        {
            // The scripts registered at start-up are not swapped: every object
            // already runs the AI its script selects.
            if (initialize)
                return;

            ValidateHotswapVisualSpell();

            // A script registered since the last commit takes over the objects it
            // is assigned to, so their default AI is replaced as well.
            _idsToRelease.insert(ScriptRegistry<ScriptType>::RecentlyAddedScriptIds.begin(),
                                 ScriptRegistry<ScriptType>::RecentlyAddedScriptIds.end());

            UnloadAIsOfScriptIds<ObjectType>(_idsToRelease);
            CreateAIsOfScriptIds<ObjectType>(_idsToRelease);

            _idsToRelease.clear();
        }

        static void BeforeUnload()
        {
            ASSERT(_idsToRelease.empty(), "The AI of a released script should have been recreated by now.");
        }

    private:
        /// Script ids released since the last commit, whose objects wait for the
        /// AI the newest code selects.
        static inline std::unordered_set<uint32> _idsToRelease;
    };
}

template<>
struct ScriptSwapHooks<CreatureScript> : AIObjectScriptSwapHooks<Creature, CreatureScript> { };

template<>
struct ScriptSwapHooks<GameObjectScript> : AIObjectScriptSwapHooks<GameObject, GameObjectScript> { };

namespace
{
    /// Runs one swap hook of every registry type whose policy needs it. The whole
    /// type list is walked, so the hooks run in the same order as the registries
    /// are unloaded in.
    void RunBeforeReleaseContextHooks(std::string const& context)
    {
        Acore::for_each<ScriptRegistryTypes>([&context]<typename Info>()
        {
            if constexpr (Info::SwapPolicy != ScriptSwapPolicy::Immediate)
                ScriptSwapHooks<typename Info::type>::BeforeReleaseContext(context);
        });
    }

    void RunBeforeSwapContextHooks(bool initialize)
    {
        Acore::for_each<ScriptRegistryTypes>([initialize]<typename Info>()
        {
            if constexpr (Info::SwapPolicy != ScriptSwapPolicy::Immediate)
                ScriptSwapHooks<typename Info::type>::BeforeSwapContext(initialize);
        });
    }

    void RunBeforeUnloadHooks()
    {
        Acore::for_each<ScriptRegistryTypes>([]<typename Info>()
        {
            if constexpr (Info::SwapPolicy != ScriptSwapPolicy::Immediate)
                ScriptSwapHooks<typename Info::type>::BeforeUnload();
        });
    }
}

struct TSpellSummary
{
    uint8 Targets; // set of enum SelectTarget
    uint8 Effects; // set of enum SelectEffect
}*SpellSummary;

ScriptMgr::ScriptMgr()
    : _scriptCount(0),
    _scheduledScripts(0),
    _script_loader_callback(nullptr),
    _modules_loader_callback(nullptr),
    _currentContext(GetNameOfStaticContext()) { }

ScriptMgr::~ScriptMgr() { }

ScriptMgr* ScriptMgr::instance()
{
    static ScriptMgr instance;
    return &instance;
}

void ScriptMgr::Initialize()
{
    LOG_INFO("server.loading", "> Loading C++ scripts");
    LOG_INFO("server.loading", " ");

    AddSC_SmartScripts();

    // LFGScripts
    lfg::AddSC_LFGScripts();

    ASSERT(_script_loader_callback,
        "Script loader callback wasn't registered!");

    ASSERT(_modules_loader_callback,
        "Modules loader callback wasn't registered!");

    _script_loader_callback();
    _modules_loader_callback();

    Acore::for_each<ScriptRegistryTypes>([]<typename Info>()
    {
        // Touch every registry, so the compositum knows all of them before the
        // first change set, including the types no script registered yet.
        ScriptRegistry<typename Info::type>::Instance();

        if constexpr (Info::HasEnabledHooks)
            ScriptRegistry<typename Info::type>::InitEnabledHooksIfNeeded(Info::EnabledHooks);
    });
}

void ScriptMgr::Unload()
{
    RunBeforeUnloadHooks();

    Acore::for_each<ScriptRegistryTypes>([]<typename Info>()
    {
        ScriptRegistry<typename Info::type>::Unload();
    });

    sScriptRegistryCompositum->Unload();

    delete[] SpellSummary;
}

void ScriptMgr::SetScriptContext(std::string const& context)
{
    _currentContext = context;
}

std::string const& ScriptMgr::GetCurrentScriptContext() const
{
    return _currentContext;
}

std::string const& ScriptMgr::GetNameOfStaticContext()
{
    static std::string const name = "___static___";
    return name;
}

void ScriptMgr::ReleaseScriptContext(std::string const& context)
{
    // The released ids are still known to their registries here, so a policy can
    // tell whether the context owned anything of its type before it disappears.
    RunBeforeReleaseContextHooks(context);

    sScriptRegistryCompositum->ReleaseContext(context);
}

void ScriptMgr::SwapScriptContext(bool initialize)
{
    // Called before the registries clear their change set, so a policy still sees
    // which scripts were added since the last commit.
    RunBeforeSwapContextHooks(initialize);

    sScriptRegistryCompositum->SwapContext(initialize);

    // Everything registered from here on belongs to the statically linked set
    // again, so a script whose library forgot to set a context is never orphaned.
    _currentContext = GetNameOfStaticContext();
}

std::shared_ptr<ModuleReference> ScriptMgr::AcquireModuleReferenceOfScriptName(std::string const& /*scriptname*/) const
{
    // Handing out a module reference needs the reload manager that owns the loaded
    // libraries; until it can, no script outlives the library it came from.
    return nullptr;
}

void ScriptMgr::LoadDatabase()
{
    uint32 oldMSTime = getMSTime();

    sScriptSystemMgr->LoadScriptWaypoints();

    // Add all scripts that must be loaded after db/maps. Each registry's
    // after-load list is independent, so iteration order does not matter.
    Acore::for_each<ScriptRegistryTypes>([]<typename Info>()
    {
        if constexpr (Info::PromotedAfterDbLoad)
            ScriptRegistry<typename Info::type>::AddALScripts();
    });

    FillSpellSummary();

    CheckIfScriptsInDatabaseExist();

    LOG_INFO("server.loading", ">> Loaded {} C++ scripts in {} ms", GetScriptCount(), GetMSTimeDiffToNow(oldMSTime));
    LOG_INFO("server.loading", " ");
}

void ScriptMgr::CheckIfScriptsInDatabaseExist()
{
    for (auto const& scriptName : sObjectMgr->GetScriptNames())
    {
        if (uint32 sid = sObjectMgr->GetScriptId(scriptName))
        {
            bool const hasRegisteredScript = Acore::any_of<ScriptRegistryTypes>([sid]<typename Info>()
            {
                if constexpr (Info::LegacyDbValidationCandidate)
                    return ScriptRegistry<typename Info::type>::GetScriptById(sid) != nullptr;

                return false;
            });

            if (!hasRegisteredScript)
                LOG_ERROR("sql.sql", "Script named '{}' is assigned in the database, but has no code!", scriptName);
        }
    }
}

void ScriptMgr::FillSpellSummary()
{
    UnitAI::FillAISpellInfo();

    SpellSummary = new TSpellSummary[sSpellMgr->GetSpellInfoStoreSize()];

    SpellInfo const* pTempSpell;

    for (uint32 i = 0; i < sSpellMgr->GetSpellInfoStoreSize(); ++i)
    {
        SpellSummary[i].Effects = 0;
        SpellSummary[i].Targets = 0;

        pTempSpell = sSpellMgr->GetSpellInfo(i);
        // This spell doesn't exist.
        if (!pTempSpell)
            continue;

        for (uint32 j = 0; j < MAX_SPELL_EFFECTS; ++j)
        {
            // Spell targets self.
            if (pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_CASTER)
                SpellSummary[i].Targets |= 1 << (SELECT_TARGET_SELF - 1);

            // Spell targets a single enemy.
            if (pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_TARGET_ENEMY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_DEST_TARGET_ENEMY)
                SpellSummary[i].Targets |= 1 << (SELECT_TARGET_SINGLE_ENEMY - 1);

            // Spell targets AoE at enemy.
            if (pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_DEST_AREA_ENEMY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_SRC_CASTER ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_DEST_DYNOBJ_ENEMY)
                SpellSummary[i].Targets |= 1 << (SELECT_TARGET_AOE_ENEMY - 1);

            // Spell targets an enemy.
            if (pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_TARGET_ENEMY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_DEST_TARGET_ENEMY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_DEST_AREA_ENEMY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_SRC_CASTER ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_DEST_DYNOBJ_ENEMY)
                SpellSummary[i].Targets |= 1 << (SELECT_TARGET_ANY_ENEMY - 1);

            // Spell targets a single friend (or self).
            if (pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_CASTER ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_TARGET_ALLY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_TARGET_PARTY)
                SpellSummary[i].Targets |= 1 << (SELECT_TARGET_SINGLE_FRIEND - 1);

            // Spell targets AoE friends.
            if (pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_CASTER_AREA_PARTY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_LASTTARGET_AREA_PARTY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_SRC_CASTER)
                SpellSummary[i].Targets |= 1 << (SELECT_TARGET_AOE_FRIEND - 1);

            // Spell targets any friend (or self).
            if (pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_CASTER ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_TARGET_ALLY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_TARGET_PARTY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_CASTER_AREA_PARTY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_UNIT_LASTTARGET_AREA_PARTY ||
                    pTempSpell->Effects[j].TargetA.GetTarget() == TARGET_SRC_CASTER)
                SpellSummary[i].Targets |= 1 << (SELECT_TARGET_ANY_FRIEND - 1);

            // Make sure that this spell includes a damage effect.
            if (pTempSpell->Effects[j].Effect == SPELL_EFFECT_SCHOOL_DAMAGE ||
                    pTempSpell->Effects[j].Effect == SPELL_EFFECT_INSTAKILL ||
                    pTempSpell->Effects[j].Effect == SPELL_EFFECT_ENVIRONMENTAL_DAMAGE ||
                    pTempSpell->Effects[j].Effect == SPELL_EFFECT_HEALTH_LEECH)
                SpellSummary[i].Effects |= 1 << (SELECT_EFFECT_DAMAGE - 1);

            // Make sure that this spell includes a healing effect (or an apply aura with a periodic heal).
            if (pTempSpell->Effects[j].Effect == SPELL_EFFECT_HEAL ||
                    pTempSpell->Effects[j].Effect == SPELL_EFFECT_HEAL_MAX_HEALTH ||
                    pTempSpell->Effects[j].Effect == SPELL_EFFECT_HEAL_MECHANICAL ||
                    (pTempSpell->Effects[j].Effect == SPELL_EFFECT_APPLY_AURA  && pTempSpell->Effects[j].ApplyAuraName == 8))
                SpellSummary[i].Effects |= 1 << (SELECT_EFFECT_HEALING - 1);

            // Make sure that this spell applies an aura.
            if (pTempSpell->Effects[j].Effect == SPELL_EFFECT_APPLY_AURA)
                SpellSummary[i].Effects |= 1 << (SELECT_EFFECT_AURA - 1);
        }
    }
}
