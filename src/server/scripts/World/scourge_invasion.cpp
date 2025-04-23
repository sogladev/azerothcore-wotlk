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

#include "scourge_invasion.h"

#include "Weather.h"
#include "WeatherMgr.h"
#include "AreaDefines.h"
#include "CombatAI.h"
#include "GameObjectAI.h"
#include "GridNotifiers.h"
#include "Object.h"
#include "SpellInfo.h"
#include "Unit.h"
#include "ScriptedAI/ScriptedCreature.h"
#include "GameEventMgr.h"
// #include "WorldStateDefines.h"
// #include "../../game/Scripting/ScriptDefines/CreatureScript.h"
// #include "../../game/Scripting/ScriptDefines/GameObjectScript.h"
// #include "World/World.h"
// #include <cmath>

inline uint32 GetCampType(Creature const* unit) { return unit->HasAura(SPELL_CAMP_TYPE_GHOST_SKELETON) || unit->HasAura(SPELL_CAMP_TYPE_GHOST_GHOUL) || unit->HasAura(SPELL_CAMP_TYPE_GHOUL_SKELETON); };

inline bool IsGuardOrBoss(Unit const* unit) {
    return unit->GetEntry() == NPC_ROYAL_DREADGUARD || unit->GetEntry() == NPC_STORMWIND_ROYAL_GUARD || unit->GetEntry() == NPC_UNDERCITY_ELITE_GUARDIAN || unit->GetEntry() == NPC_UNDERCITY_GUARDIAN || unit->GetEntry() == NPC_DEATHGUARD_ELITE ||
        unit->GetEntry() == NPC_STORMWIND_CITY_GUARD || unit->GetEntry() == NPC_HIGHLORD_BOLVAR_FORDRAGON || unit->GetEntry() == NPC_LADY_SYLVANAS_WINDRUNNER || unit->GetEntry() == NPC_VARIMATHRAS;
}

class FlameshockerCheck
{
public:
    bool operator()(Creature* creature)
    {
        return !creature->IsCivilian();
    }
};

// @todo replace CreatureListSearcher
Unit* SelectRandomFlameshockerSpawnTarget(Creature* unit, Creature* except, float radius)
{
    std::list<Creature*> targets;

    // remove current target
    if (except)
        targets.remove(except);

    FlameshockerCheck check;
    Acore::CreatureListSearcher<FlameshockerCheck> searcher(unit, targets, check);
    if (except)
        targets.remove(except);

    for (auto const& target : targets)
        target->AI()->DoAction(1); // ACTION_ENTER_COMBAT

    for (auto it = targets.begin(); it != targets.end(); )
    {
        Creature* target = *it;
        if (target->GetZoneId() != unit->GetZoneId() ||
            unit->IsValidAttackTarget(target) ||
            GetClosestCreatureWithEntry(target, NPC_FLAMESHOCKER, VISIBILITY_DISTANCE_TINY))
        {
            it = targets.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // no appropriate targets
    if (targets.empty())
        return nullptr;

    // select random
    Creature* randomTarget = Acore::Containers::SelectRandomContainerElement(targets);

    return randomTarget;
}

void ChangeZoneEventStatus(Creature* mouth, bool on)
{
    if (!mouth)
        return;

    switch (mouth->GetZoneId())
    {
        case AREA_WINTERSPRING:
            if (on)
            {
                if (!sGameEventMgr->IsActiveEvent(GAME_EVENT_SCOURGE_INVASION_WINTERSPRING))
                    sGameEventMgr->StartEvent(GAME_EVENT_SCOURGE_INVASION_WINTERSPRING, true);
            }
            else
                sGameEventMgr->StopEvent(GAME_EVENT_SCOURGE_INVASION_WINTERSPRING, true);
            break;
        case AREA_TANARIS:
            if (on)
            {
                if (!sGameEventMgr->IsActiveEvent(GAME_EVENT_SCOURGE_INVASION_TANARIS))
                    sGameEventMgr->StartEvent(GAME_EVENT_SCOURGE_INVASION_TANARIS, true);
            }
            else
                sGameEventMgr->StopEvent(GAME_EVENT_SCOURGE_INVASION_TANARIS, true);
            break;
        case AREA_AZSHARA:
            if (on)
            {
                if (!sGameEventMgr->IsActiveEvent(GAME_EVENT_SCOURGE_INVASION_AZSHARA))
                    sGameEventMgr->StartEvent(GAME_EVENT_SCOURGE_INVASION_AZSHARA, true);
            }
            else
                sGameEventMgr->StopEvent(GAME_EVENT_SCOURGE_INVASION_AZSHARA, true);
            break;
        case AREA_BLASTED_LANDS:
            if (on)
            {
                if (!sGameEventMgr->IsActiveEvent(GAME_EVENT_SCOURGE_INVASION_BLASTED_LANDS))
                    sGameEventMgr->StartEvent(GAME_EVENT_SCOURGE_INVASION_BLASTED_LANDS, true);
            }
            else
                sGameEventMgr->StopEvent(GAME_EVENT_SCOURGE_INVASION_BLASTED_LANDS, true);
            break;
        case AREA_EASTERN_PLAGUELANDS:
            if (on)
            {
                if (!sGameEventMgr->IsActiveEvent(GAME_EVENT_SCOURGE_INVASION_EASTERN_PLAGUELANDS))
                    sGameEventMgr->StartEvent(GAME_EVENT_SCOURGE_INVASION_EASTERN_PLAGUELANDS, true);
            }
            else
                sGameEventMgr->StopEvent(GAME_EVENT_SCOURGE_INVASION_EASTERN_PLAGUELANDS, true);
            break;
        case AREA_BURNING_STEPPES:
            if (on)
            {
                if (!sGameEventMgr->IsActiveEvent(GAME_EVENT_SCOURGE_INVASION_BURNING_STEPPES))
                    sGameEventMgr->StartEvent(GAME_EVENT_SCOURGE_INVASION_BURNING_STEPPES, true);
            }
            else
                sGameEventMgr->StopEvent(GAME_EVENT_SCOURGE_INVASION_BURNING_STEPPES, true);
            break;
        default:
            break;
    }

    // sWorld.GetMessager().AddMessage([](World* /*world*/)
    // {
        // sWorldState.Save(SAVE_ID_SCOURGE_INVASION);
    // });
}

void DespawnEventDoodads(Creature* shard)
{
    if (!shard)
        return;

    std::list<GameObject*> doodadList;
    shard->GetGameObjectListWithEntryInGrid(doodadList, { GO_SUMMON_CIRCLE, GO_UNDEAD_FIRE, GO_UNDEAD_FIRE_AURA, GO_SKULLPILE_01, GO_SKULLPILE_02, GO_SKULLPILE_03, GO_SKULLPILE_04, GO_SUMMONER_SHIELD }, 60.0f);
    for (auto const& pDoodad : doodadList)
    {
        pDoodad->SetRespawnDelay(-1);
        pDoodad->DespawnOrUnsummon();
    }

    std::list<Creature*> finderList;
    shard->GetCreatureListWithEntryInGrid(finderList, NPC_SCOURGE_INVASION_MINION_FINDER, 60.0f);
    for (auto const& finder : finderList)
        finder->DespawnOrUnsummon();
}

void DespawnNecropolis(Unit* despawner)
{
    if (!despawner)
        return;

    std::list<GameObject*>  necropolisList;
    despawner->GetGameObjectListWithEntryInGrid(necropolisList, { GO_NECROPOLIS_TINY, GO_NECROPOLIS_SMALL, GO_NECROPOLIS_MEDIUM, GO_NECROPOLIS_BIG, GO_NECROPOLIS_HUGE }, ATTACK_DISTANCE);
    for (auto const& necropolis : necropolisList)
        necropolis->DespawnOrUnsummon();
}

void SummonCultists(Unit* shard)
{
    if (!shard)
        return;

    std::list<GameObject*>   summonerShieldList;
    shard->GetGameObjectListWithEntryInGrid(summonerShieldList, GO_SUMMONER_SHIELD, INSPECT_DISTANCE);
    for (auto const& summonerShield : summonerShieldList)
        summonerShield->DespawnOrUnsummon();

    // We don't have all positions sniffed from the Cultists, so why not using this code which placing them almost perfectly into the circle while B's positions are sometimes way off?
    if (GameObject* gameObject = GetClosestGameObjectWithEntry(shard, GO_SUMMON_CIRCLE, CONTACT_DISTANCE))
    {
        for (int i = 0; i < 4; ++i)
        {
            float angle = (float(i) * (M_PIf / 2.f)) + gameObject->GetOrientation();
            float x = gameObject->GetPositionX() + 6.95f * std::cos(angle);
            float y = gameObject->GetPositionY() + 6.75f * std::sin(angle);
            float z = gameObject->GetPositionZ() + 5.0f;
            shard->UpdateGroundPositionZ(x, y, z);
            // if (Creature* cultist = shard->SummonCreature(NPC_CULTIST_ENGINEER, x, y, z, angle - M_PIf, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, IN_MILLISECONDS * HOUR))
                // cultist->AI()->DoAction(1, shard, cultist, NPC_CULTIST_ENGINEER); // AI_EVENT_CUSTOM_A
        }
    }
}

void DespawnCultists(Unit* despawner)
{
    if (!despawner)
        return;

    std::list<Creature*>  cultistList;
    GetCreatureListWithEntryInGrid(cultistList, despawner, NPC_CULTIST_ENGINEER, INSPECT_DISTANCE);
    for (const auto pCultist : cultistList)
        if (pCultist)
            pCultist->DespawnOrUnsummon();
}

void DespawnShadowsOfDoom(Unit* despawner)
{
    if (!despawner)
        return;

    std::list<Creature*> shadowList;
    GetCreatureListWithEntryInGrid(shadowList, despawner, NPC_SHADOW_OF_DOOM, 200.0f);
    for (const auto pShadow : shadowList)
        if (pShadow && pShadow->IsAlive() && !pShadow->IsInCombat())
            pShadow->DespawnOrUnsummon();
}

uint32 HasMinion(Creature* summoner, float /*range*/)
{
    if (!summoner)
        return false;

    uint32 minionCounter = 0;
    std::list<Creature*>  minionList;

    std::list<Creature*> shadowList;
    summoner->GetCreatureListWithEntryInGrid(minionList, { NPC_SKELETAL_SHOCKTROOPER, NPC_GHOUL_BERSERKER, NPC_SPECTRAL_SOLDIER, NPC_LUMBERING_HORROR, NPC_BONE_WITCH, NPC_SPIRIT_OF_THE_DAMNED }, ATTACK_DISTANCE);
    for (const auto pMinion : minionList)
        if (pMinion && pMinion->IsAlive())
            minionCounter++;

    return minionCounter;
}

bool UncommonMinionspawner(Creature* pSummoner) // Rare Minion Spawner.
{
    if (!pSummoner)
        return false;

    std::list<Creature*>  uncommonMinionList;
    pSummoner->GetCreatureListWithEntryInGrid(uncommonMinionList, { NPC_LUMBERING_HORROR, NPC_BONE_WITCH, NPC_SPIRIT_OF_THE_DAMNED }, 100.0f);
    for (const auto pMinion : uncommonMinionList)
        if (pMinion)
            return false; // Already a rare found (dead or alive).

    /*
    The chance or timer for a Rare minion spawn is unknown, and I don't see an exact pattern for a spawn sequence.
    Sniffed are: 19669 Minions and 90 Rares (Ratio: 217 to 1).
    */
    uint32 chance = urand(1, 217);
    if (chance > 1)
        return false; // Above 1 = Minion, else Rare.

    return true;
}

uint32 GetFindersAmount(Creature* shard)
{
    uint32 finderCounter = 0;
    std::list<Creature*> finderList;
    GetCreatureListWithEntryInGrid(finderList, shard, NPC_SCOURGE_INVASION_MINION_FINDER, 60.0f);
    for (const auto pFinder : finderList)
        if (pFinder)
            finderCounter++;

    return finderCounter;
}

/*
Circle
*/

class scourge_invasion_go_circle : public GameObjectAI
{
    public:
        scourge_invasion_go_circle(GameObject* gameobject) : GameObjectAI(gameobject)
        {
            me->CastSpell(nullptr, SPELL_CREATE_CRYSTAL);
        }
};

/*
Necropolis
*/
class scourge_invasion_go_necropolis : public GameObjectAI
{
    public:
        scourge_invasion_go_necropolis(GameObject* gameobject) : GameObjectAI(gameobject)
        {
            // m_go->SetActiveObjectState(true);
            me->setActive(true); // @todo check if correct active
            //m_go->SetVisibilityModifier(3000.0f);
        }
};

/*
Mouth of Kel'Thuzad
*/
struct scourge_invasion_mouth : public ScriptedAI
{
    scourge_invasion_mouth(Creature* creature) : ScriptedAI(creature)
    {
        me->SetReactState(REACT_PASSIVE);
        // AddCustomAction(EVENT_MOUTH_OF_KELTHUZAD_YELL, urand((IN_MILLISECONDS * 150), (IN_MILLISECONDS * HOUR)), [&]()
        // {
        //     DoBroadcastText(PickRandomValue(BCT_MOUTH_OF_KELTHUZAD_RANDOM_1, BCT_MOUTH_OF_KELTHUZAD_RANDOM_2, BCT_MOUTH_OF_KELTHUZAD_RANDOM_3, BCT_MOUTH_OF_KELTHUZAD_RANDOM_4), m_creature, nullptr, CHAT_TYPE_ZONE_YELL);
        //     ResetTimer(EVENT_MOUTH_OF_KELTHUZAD_YELL, urand((IN_MILLISECONDS * 150), (IN_MILLISECONDS * HOUR)));
        // });
    }

    void Reset() override {}

    void DoAction(int32 action) override
    {
        if (action == EVENT_MOUTH_OF_KELTHUZAD_ZONE_START)
        {
            Talk(MOUTH_SAY_ATTACK_START); // attack start
            // DoBroadcastText(PickRandomValue(BCT_MOUTH_OF_KELTHUZAD_ZONE_ATTACK_START_1, BCT_MOUTH_OF_KELTHUZAD_ZONE_ATTACK_START_2), m_creature, nullptr, CHAT_TYPE_ZONE_YELL);
            ChangeZoneEventStatus(me, true);
            if (Weather* weather = WeatherMgr::FindWeather(me->GetZoneId()))
                weather->SetWeather(WEATHER_TYPE_STORM, 0.25f);
        }
        else if (action == EVENT_MOUTH_OF_KELTHUZAD_ZONE_STOP)
        {
            Talk(MOUTH_SAY_ATTACK_END); // attack end
            // DoBroadcastText(PickRandomValue(BCT_MOUTH_OF_KELTHUZAD_ZONE_ATTACK_ENDS_1, BCT_MOUTH_OF_KELTHUZAD_ZONE_ATTACK_ENDS_2, BCT_MOUTH_OF_KELTHUZAD_ZONE_ATTACK_ENDS_3), m_creature, nullptr, CHAT_TYPE_ZONE_YELL);
            ChangeZoneEventStatus(me, false);
            if (Weather* weather = WeatherMgr::FindWeather(me->GetZoneId()))
                weather->SetWeather(WEATHER_TYPE_RAIN, 0.0f);
            me->DespawnOrUnsummon();
        }
    }
};

/*
Necropolis
*/
struct scourge_invasion_necropolis : public ScriptedAI
{
    scourge_invasion_necropolis(Creature* creature) : ScriptedAI(creature)
    {
        me->setActive(true);
        //m_creature->SetVisibilityModifier(3000.0f);
    }

    void Reset() override {}

    void SpellHit(Unit* /* caster */, SpellInfo const* spell) override
    {
        if (me->HasAura(SPELL_COMMUNIQUE_TIMER_NECROPOLIS))
            return;

        if (spell->Id == SPELL_COMMUNIQUE_PROXY_TO_NECROPOLIS)
            me->CastSpell(me, SPELL_COMMUNIQUE_TIMER_NECROPOLIS, true);
             // m_creature->AddAura(SPELL_COMMUNIQUE_TIMER_NECROPOLIS);
    }

    void UpdateAI(uint32 const /*diff*/) override
    {
        if (me && me->IsAlive() && me->GetPositionZ() < (me->GetHomePosition().GetPositionZ() - 10))
        {
            Position respawn = me->GetHomePosition();
            me->NearTeleportTo(respawn.GetPositionX(),respawn.GetPositionY(),respawn.GetPositionZ(), respawn.GetOrientation());
        }
    }
};

/*
Necropolis Health
*/
struct scourge_invasion_necropolis_health : public ScriptedAI
{
    std::set<ObjectGuid> ownedCircles;
    bool m_firstSpawn = true;
    explicit scourge_invasion_necropolis_health(Creature* creature) : ScriptedAI(creature)
    {
        ScheduleTimedEvent(5s, [&]
        {
            if (me->HealthAbovePct(99))
            {
                std::list<Creature*> proxies;
                GetCreatureListWithEntryInGrid(proxies, me, NPC_NECROPOLIS_PROXY, 200.f);

                for (Creature* proxy : proxies)
                {
                    if (proxy && !proxy->IsAlive())
                    {
                        proxy->SetRespawnTime(0);
                        proxy->Respawn();
                    }
                }

                std::vector<ObjectGuid> circles;
                // if (auto* continent = dynamic_cast<ScriptedIn*>(me->GetMap()->GetInstanceData()))
                    // continent->GetGameObjectGuidVectorFromStorage(GO_SUMMON_CIRCLE, circles);

                if (ownedCircles.size() < 3)
                {
                    if (!circles.empty())
                    {
                        for (ObjectGuid circleGUID : circles)
                            if (GameObject* circle = me->GetMap()->GetGameObject(circleGUID))
                                if (circle->GetZoneId() == me->GetZoneId())
                                    if (circle->GetDistance2d(me) <= 350.f)
                                        ownedCircles.insert(circleGUID);

                    }
                }
                else if (ownedCircles.size() > 3)
                {
                    for (auto itr = ownedCircles.begin(); itr != ownedCircles.end();)
                    {
                        if (!me->GetMap()->GetGameObject(*itr))
                        {
                            auto itr2 = itr;
                            ownedCircles.erase(itr2);
                        }
                        itr++;
                    }
                }

                for (ObjectGuid circle: ownedCircles)
                {
                    if (auto *t_circle = me->GetMap()->GetGameObject(circle))
                    {
                        if (!t_circle->isSpawned())
                        {
                            auto *shard = GetClosestCreatureWithEntry(t_circle, NPC_NECROTIC_SHARD, 1.f);
                            if (m_firstSpawn || (shard && shard->IsAlive()))
                            {
                                t_circle->SetRespawnTime(0);
                                t_circle->Respawn();
                                m_firstSpawn = false;
                            }
                        }
                    }
                }

                for (ScourgeInvasionMisc t_necId : {GO_NECROPOLIS_BIG, GO_NECROPOLIS_HUGE, GO_NECROPOLIS_MEDIUM, GO_NECROPOLIS_SMALL, GO_NECROPOLIS_TINY})
                {
                    if (GameObject* t_necGo = GetClosestGameObjectWithEntry(me, t_necId, 20.f))
                    {
                        if (!t_necGo->isSpawned())
                        {
                            t_necGo->SetRespawnTime(0);
                            t_necGo->Respawn();
                        }
                    }
                }

            }
        }, 60s);
    }

    int m_zapped = 0; // 3 = death.

    void Reset() override {}

    void SpellHit(Unit* /* caster */, SpellInfo const* spell) override
    {
        if (spell->Id == SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH)
            me->CastSpell(me, SPELL_ZAP_NECROPOLIS, true);

        // Just to make sure it finally dies!
        if (spell->Id == SPELL_ZAP_NECROPOLIS)
        {
            if (++m_zapped >= 3)
                me->KillSelf(); //m_creature->DoKillUnit(m_creature);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (Creature* necropolis = GetClosestCreatureWithEntry(me, NPC_NECROPOLIS, ATTACK_DISTANCE))
            me->CastSpell(necropolis, SPELL_DESPAWNER_OTHER, true);

        SIRemaining remainingID;

        switch (me->GetZoneId())
        {
            default:
            case AREA_TANARIS:
                remainingID = SI_REMAINING_TANARIS;
                break;
            case AREA_BLASTED_LANDS:
                remainingID = SI_REMAINING_BLASTED_LANDS;
                break;
            case AREA_EASTERN_PLAGUELANDS:
                remainingID = SI_REMAINING_EASTERN_PLAGUELANDS;
                break;
            case AREA_BURNING_STEPPES:
                remainingID = SI_REMAINING_BURNING_STEPPES;
                break;
            case AREA_WINTERSPRING:
                remainingID = SI_REMAINING_WINTERSPRING;
                break;
            case AREA_AZSHARA:
                remainingID = SI_REMAINING_AZSHARA;
                break;
        }

        uint32 remaining = sWorldState->GetSIRemaining(remainingID);
        if (remaining > 0)
            sWorldState->SetSIRemaining(remainingID, (remaining - 1));
    }

    void SpellHitTarget(Unit* target, SpellInfo const* spellInfo) override
    {
        // Make sure m_creature despawn after SPELL_DESPAWNER_OTHER triggered.
        if (spellInfo->Id == SPELL_DESPAWNER_OTHER && target->GetEntry() == NPC_NECROPOLIS)
        {
            DespawnNecropolis(target);
            static_cast<Creature*>(target)->DespawnOrUnsummon();
            me->DespawnOrUnsummon();
        }
    }
    void UpdateAI(uint32 const diff) override
    {
        ScriptedAI::UpdateAI(diff);
        if (me && me->IsAlive() && me->GetPositionZ() < (me->GetHomePosition().GetPositionZ() - 10))
        {
            Position respawn = me->GetHomePosition();
            me->NearTeleportTo(respawn.GetPositionX(), respawn.GetPositionY(), respawn.GetPositionZ(), respawn.GetOrientation());
        }
    }
};

/*
Necropolis Proxy
*/
struct scourge_invasion_necropolis_proxy : public ScriptedAI
{
    scourge_invasion_necropolis_proxy(Creature* creature) : ScriptedAI(creature)
    {
        me->setActive(true);
        //m_creature->SetVisibilityModifier(3000.0f);
        me->SetDisableGravity(true); // Set in DB?
        Reset();
    }

    void Reset() override {}

    void SpellHit(Unit* /*caster*/, SpellInfo const* spellInfo) override
    {
        switch (spellInfo->Id)
        {
            case SPELL_COMMUNIQUE_NECROPOLIS_TO_PROXIES:
                me->CastSpell(me, SPELL_COMMUNIQUE_PROXY_TO_RELAY, true);
                break;
            case SPELL_COMMUNIQUE_RELAY_TO_PROXY:
                me->CastSpell(me, SPELL_COMMUNIQUE_PROXY_TO_NECROPOLIS, true);
                break;
            case SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH:
                if (Creature* health = GetClosestCreatureWithEntry(me, NPC_NECROPOLIS_HEALTH, 200.0f))
                    me->CastSpell(health, SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH, true);
                break;
        }
    }

    void SpellHitTarget(Unit* /*target*/, SpellInfo const* spellInfo) override
    {
        // Make sure me despawn after SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH hits the target to avoid getting hit by Purple bolt again.
        if (spellInfo->Id == SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH)
            me->DespawnOrUnsummon();
    }

    void UpdateAI(uint32 const /*diff*/) override
    {
        if (me && me->IsAlive() && me->GetPositionZ() < (me->GetHomePosition().GetPositionZ() - 10))
        {
            Position respawn = me->GetHomePosition();
            me->NearTeleportTo(respawn.GetPositionX(), respawn.GetPositionY(), respawn.GetPositionZ(), respawn.GetOrientation());
        }
    }
};

/*
Necropolis Relay
*/
struct scourge_invasion_necropolis_relay : public ScriptedAI
{
    scourge_invasion_necropolis_relay(Creature* creature) : ScriptedAI(creature)
    {
        me->setActive(true);
        //m_creature->SetVisibilityModifier(3000.0f);
        Reset();
    }

    void Reset() override {}

    void SpellHit(Unit* /*caster*/, SpellInfo const* spell) override
    {
        switch (spell->Id)
        {
            case SPELL_COMMUNIQUE_PROXY_TO_RELAY:
                me->CastSpell(me, SPELL_COMMUNIQUE_RELAY_TO_CAMP, true);
                break;
            case SPELL_COMMUNIQUE_CAMP_TO_RELAY:
                me->CastSpell(me, SPELL_COMMUNIQUE_RELAY_TO_PROXY, true);
                break;
            case SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH:
                if (Creature* pProxy = GetClosestCreatureWithEntry(me, NPC_NECROPOLIS_PROXY, 200.0f))
                    me->CastSpell(pProxy, SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH, true);
                break;
        }
    }

    void SpellHitTarget(Unit* /*target*/, SpellInfo const* spell) override
    {
        // Make sure m_creature despawn after SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH hits the target to avoid getting hit by Purple bolt again.
        if (spell->Id == SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH)
            me->DespawnOrUnsummon();
    }

    void UpdateAI(uint32 const /*diff*/) override
    {
        if (me && me->IsAlive() && me->GetPositionZ() < (me->GetHomePosition().GetPositionZ() - 5))
        {
            Position respawn = me->GetHomePosition();
            me->NearTeleportTo(respawn.GetPositionX(), respawn.GetPositionY(), respawn.GetPositionZ(), respawn.GetOrientation());
        }
    }
};

/*
Necrotic Shard
*/
struct scourge_invasion_necrotic_shard : public ScriptedAI
{
    scourge_invasion_necrotic_shard(Creature* creature) : ScriptedAI(creature)
    {
        me->setActive(true);
        // AddCustomAction(EVENT_SHARD_MINION_SPAWNER_SMALL, true, [&]() // Spawn Minions every 5 seconds.
        // {
        //     HandleShardMinionSpawnerSmall();
        // });
        // AddCustomAction(11, 25000u, [&]() // Check if there are a summoning circle and a necropolis nearby, otherwise despawn
        // {
        //     if (!m_creature)
        //         return;

        //     bool despawnMe = false;

        //     if (!GetClosestGameObjectWithEntry(m_creature, GO_SUMMON_CIRCLE, 2.f))
        //     {
        //         despawnMe = true;
        //     }

        //     std::vector<ObjectGuid> necropoli;

        //     if (auto* continent = dynamic_cast<ScriptedInstance*>(m_creature->GetMap()->GetInstanceData()))
        //         continent->GetCreatureGuidVectorFromStorage(NPC_NECROPOLIS_HEALTH, necropoli);

        //     if (!despawnMe && !necropoli.empty())
        //     {
        //         int8 t_necroNear = 0;
        //         for (ObjectGuid necropolis : necropoli)
        //             if (auto* t_crNecro = m_creature->GetMap()->GetCreature(necropolis))
        //                 if (t_crNecro->GetZoneId() == m_creature->GetZoneId())
        //                     if (t_crNecro && t_crNecro->IsAlive()
        //                     && sqrtf(t_crNecro->GetPosition().GetDistance(m_creature->GetPosition())) <= 350.f)
        //                         t_necroNear++;

        //         if (t_necroNear == 0)
        //         {
        //             despawnMe = true;
        //         }
        //     }
        //
        //     if (despawnMe)
        //     {
        //         DespawnEventDoodads(m_creature);
        //         m_creature->ForcedDespawn();
        //         return;
        //     }

        //     ResetTimer(11, 60000u);
        // });
        //m_creature->SetVisibilityModifier(3000.0f);
        if (me->GetEntry() == NPC_DAMAGED_NECROTIC_SHARD)
        {
            m_finders = GetFindersAmount(me);                       // Count all finders to limit Minions spawns.
            // ResetTimer(EVENT_SHARD_MINION_SPAWNER_SMALL, 5000); // Spawn Minions every 5 seconds.
            // AddCustomAction(EVENT_SHARD_MINION_SPAWNER_BUTTRESS, 5000u, [&]() // Spawn Cultists every 60 minutes.
            // {
            //     /*
            //     This is a placeholder for SPELL_MINION_SPAWNER_BUTTRESS [27888] which also activates unknown, not sniffable gameobjects
            //     and happens every hour if a Damaged Necrotic Shard is active. The Cultists despawning after 1 hour,
            //     so this just resets everything and spawn them again and Refill the Health of the Shard.
            //     */
            //     m_creature->SetHealthPercent(100.f); // m_creature->SetFullHealth();
            //     // Despawn all remaining Shadows before respawning the Cultists?
            //     DespawnShadowsOfDoom(m_creature);
            //     // Respawn the Cultists.
            //     SummonCultists(m_creature);

            //     ResetTimer(EVENT_SHARD_MINION_SPAWNER_BUTTRESS, IN_MILLISECONDS * HOUR);
            // });
        }
        else
        {
            // Just in case.
            // CreatureList shardList;
            // GetCreatureListWithEntryInGrid(shardList, m_creature, { NPC_NECROTIC_SHARD, NPC_DAMAGED_NECROTIC_SHARD }, CONTACT_DISTANCE);
            // for (const auto shard : shardList)
            //     if (shard != m_creature)
            //         shard->ForcedDespawn();

            // AddCustomAction(10, 5000u, [&]() // Check if Doodads are spawned 5 seconds after spawn. If not: spawn them
            // {
            //     GameObjectList objectList;
            //     GetGameObjectListWithEntryInGrid(objectList, m_creature, {GO_UNDEAD_FIRE, GO_UNDEAD_FIRE_AURA, GO_SKULLPILE_01, GO_SKULLPILE_02, GO_SKULLPILE_03, GO_SKULLPILE_04}, 50.f);

            //     for (GameObject* object : objectList)
            //     {
            //         if (object && !object->IsSpawned())
            //         {
            //             object->SetRespawnTime(0);
            //             object->Respawn();
            //         }
            //     }
            // });
        }
        me->SetReactState(REACT_PASSIVE);
    }

    uint32 m_camptype = 0;
    uint32 m_finders = 0;

    void Reset() override
    {
        // DoCastSpellIfCan(nullptr, SPELL_COMMUNIQUE_TIMER_CAMP, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
    }

    void SpellHit(Unit* caster, SpellInfo const* spell) override
    {
        switch (spell->Id)
        {
            case SPELL_ZAP_CRYSTAL_CORPSE:
            {
                Creature::DealDamage(me, me, (me->GetMaxHealth() / 4), nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
                break;
            }
            case SPELL_COMMUNIQUE_RELAY_TO_CAMP:
            {
                me->CastSpell((Unit*)nullptr, SPELL_CAMP_RECEIVES_COMMUNIQUE, true);
                break;
            }
            case SPELL_CHOOSE_CAMP_TYPE:
            {
                // m_camptype = PickRandomValue(SPELL_CAMP_TYPE_GHOUL_SKELETON, SPELL_CAMP_TYPE_GHOST_GHOUL, SPELL_CAMP_TYPE_GHOST_SKELETON);
                // m_creature->CastSpell(m_creature, m_camptype, TRIGGERED_OLD_TRIGGERED);
                break;
            }
            case SPELL_CAMP_RECEIVES_COMMUNIQUE:
            {
                if (!GetCampType(me) && me->GetEntry() == NPC_NECROTIC_SHARD)
                {
                    m_finders = GetFindersAmount(me);
                    me->CastSpell(me, SPELL_CHOOSE_CAMP_TYPE, true);
                    // ResetTimer(EVENT_SHARD_MINION_SPAWNER_SMALL, 0); // Spawn Minions every 5 seconds.
                }
                break;
            }
            case SPELL_FIND_CAMP_TYPE:
            {
                // Don't spawn more Minions than finders.
                if (m_finders < HasMinion(me, 60.0f))
                    return;

                // Lets the finder spawn the associated spawner.
                if (me->HasAura(SPELL_CAMP_TYPE_GHOST_SKELETON))
                    caster->CastSpell(caster, SPELL_PH_SUMMON_MINION_TRAP_GHOST_SKELETON, true);
                else if (me->HasAura(SPELL_CAMP_TYPE_GHOST_GHOUL))
                    caster->CastSpell(caster, SPELL_PH_SUMMON_MINION_TRAP_GHOST_GHOUL, true);
                else if (me->HasAura(SPELL_CAMP_TYPE_GHOUL_SKELETON))
                    caster->CastSpell(caster, SPELL_PH_SUMMON_MINION_TRAP_GHOUL_SKELETON, true);
                break;
            }
        }
    }

    void SpellHitTarget(Unit* /*target*/, SpellInfo const* spellInfo) override
    {
        if (me->GetEntry() != NPC_DAMAGED_NECROTIC_SHARD)
            return;

        if (spellInfo->Id == SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH)
            me->DespawnOrUnsummon();
    }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*damageType*/, SpellSchoolMask /*damageSchoolMask*/) override
    {
        // Only Minions and the shard itself can deal damage.
        if (attacker->GetFactionTemplateEntry() != me->GetFactionTemplateEntry())
            damage = 0;
    }

    // No healing possible.
    // void HealedBy(Unit* /*healer*/, uint32& uiHealedAmount) override
    // {
    //     uiHealedAmount = 0;
    // }

    void JustDied(Unit* /*killer*/) override
    {
        switch (me->GetEntry())
        {
            case NPC_NECROTIC_SHARD:
                if (Creature* pShard = me->SummonCreature(NPC_DAMAGED_NECROTIC_SHARD, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation(), TEMPSUMMON_MANUAL_DESPAWN, 0))
                {
                    // Get the camp type from the Necrotic Shard.
                    if (m_camptype)
                        pShard->CastSpell(pShard, m_camptype, true);
                    else
                        pShard->CastSpell(pShard, SPELL_CHOOSE_CAMP_TYPE, true);

                    me->DespawnOrUnsummon();
                }
                break;
            case NPC_DAMAGED_NECROTIC_SHARD:
                // Buff Players.
                me->CastSpell(me, SPELL_SOUL_REVIVAL, true);
                // Sending the Death Bolt.
                if (Creature* pRelay = GetClosestCreatureWithEntry(me, NPC_NECROPOLIS_RELAY, 200.0f))
                    me->CastSpell(pRelay, SPELL_COMMUNIQUE_CAMP_TO_RELAY_DEATH, true);
                // Despawn remaining Cultists (should never happen).
                DespawnCultists(me);
                // Remove Objects from the event around the Shard (Yes this is Blizzlike).
                DespawnEventDoodads(me);
                // sWorldGetMessager().AddMessage([](World* /*world*/)
                // {
                    // sWorldState.Save(SAVE_ID_SCOURGE_INVASION);
                // });
                break;
        }
    }

    void HandleShardMinionSpawnerSmall()
    {
        /*
        This is a placeholder for SPELL_MINION_SPAWNER_SMALL [27887] which also activates unknown, not sniffable objects, which possibly checks whether a minion is in his range
        and happens every 15 seconds for both, Necrotic Shard and Damaged Necrotic Shard.
        */

        uint32 finderCounter = 0;
        uint32 finderAmount = urand(1, 3); // Up to 3 spawns.

        std::list<Creature*> finderList;
        GetCreatureListWithEntryInGrid(finderList, me, NPC_SCOURGE_INVASION_MINION_FINDER, 60.0f);
        if (finderList.empty())
            return;

        // On a fresh camp, first the minions are spawned close to the shard and then further and further out.
        finderList.sort(Acore::ObjectDistanceOrderPred(me));

        for (auto const& pFinder : finderList)
        {
            // Stop summoning Minions if we reached the max spawn amount.
            if (finderAmount == finderCounter)
                break;

            // Skip dead finders.
            if (!pFinder->IsAlive())
                continue;

            // Don't take finders with Minions.
            if (HasMinion(pFinder, ATTACK_DISTANCE))
                continue;

            /*
            A finder disappears after summoning the spawner NPC (which summons the minion).
            after 160-185 seconds a finder respawns on the same position as before.
            */
            // if (pFinder->AI()->DoCastSpellIfCan(m_creature, SPELL_FIND_CAMP_TYPE, CAST_TRIGGERED) == CAST_OK)
            // {
            //     pFinder->SetRespawnDelay(urand(150, 200)); // Values are from Sniffs (rounded). Shortest and Longest respawn time from a finder on the same spot.
            //     pFinder->ForcedDespawn();
            //     finderCounter++;
            // }
        }
        // ResetTimer(EVENT_SHARD_MINION_SPAWNER_SMALL, 5000);
    }
};

/*
Minion Spawner
*/
struct scourge_invasion_minion_spawner : public ScriptedAI
{
    scourge_invasion_minion_spawner(Creature* creature) : ScriptedAI(creature)
    {
        // AddCustomAction(EVENT_SPAWNER_SUMMON_MINION, 2000, 5000, [&]() // Spawn Minions every 5 seconds.
        // {
        //     uint32 Entry = NPC_GHOUL_BERSERKER; // just in case.

        //     switch (me->GetEntry())
        //     {
        //         case NPC_SCOURGE_INVASION_MINION_SPAWNER_GHOST_GHOUL:
        //             Entry = UncommonMinionspawner(me) ? PickRandomValue(NPC_SPIRIT_OF_THE_DAMNED, NPC_LUMBERING_HORROR) : PickRandomValue(NPC_SPECTRAL_SOLDIER, NPC_GHOUL_BERSERKER);
        //             break;
        //         case NPC_SCOURGE_INVASION_MINION_SPAWNER_GHOST_SKELETON:
        //             Entry = UncommonMinionspawner(me) ? PickRandomValue(NPC_SPIRIT_OF_THE_DAMNED, NPC_BONE_WITCH) : PickRandomValue(NPC_SPECTRAL_SOLDIER, NPC_SKELETAL_SHOCKTROOPER);
        //             break;
        //         case NPC_SCOURGE_INVASION_MINION_SPAWNER_GHOUL_SKELETON:
        //             Entry = UncommonMinionspawner(me) ? PickRandomValue(NPC_LUMBERING_HORROR, NPC_BONE_WITCH) : PickRandomValue(NPC_GHOUL_BERSERKER, NPC_SKELETAL_SHOCKTROOPER);
        //             break;
        //     }
        //     if (Creature* pMinion = me->SummonCreature(Entry, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation(), TEMPSPAWN_TIMED_OR_DEAD_DESPAWN, IN_MILLISECONDS * HOUR, true))
        //     {
        //         pMinion->GetMotionMaster()->MoveRandomAroundPoint(pMinion->GetPositionX(), pMinion->GetPositionY(), pMinion->GetPositionZ(), 1.0f); // pMinion->SetWanderDistance(1.0f); // Seems to be very low.
        //         me->CastSpell(nullptr, SPELL_MINION_SPAWN_IN, TRIGGERED_NONE);
        //     }
        // });
        me->SetReactState(REACT_PASSIVE);
    }

    void Reset() override {}
};

struct scourge_invasion_cultist_engineer : public ScriptedAI
{
    scourge_invasion_cultist_engineer(Creature* creature) : ScriptedAI(creature)
    {
        // AddCustomAction(EVENT_CULTIST_CHANNELING, false, [&]()
        // {
        //     me->CastSpell(nullptr, SPELL_BUTTRESS_CHANNEL, TRIGGERED_OLD_TRIGGERED);
        // });
        me->SetReactState(REACT_PASSIVE);
    }

    void Reset() override {}

    void JustDied(Unit*) override
    {
        if (Creature* shard = GetClosestCreatureWithEntry(me, NPC_DAMAGED_NECROTIC_SHARD, 15.0f))
            shard->CastSpell(shard, SPELL_DAMAGE_CRYSTAL, true);

        if (GameObject* gameObject = GetClosestGameObjectWithEntry(me, GO_SUMMONER_SHIELD, CONTACT_DISTANCE))
            gameObject->Delete();
    }

    // void ReceiveAIEvent(AIEventType eventType, Unit* /*sender*/, Unit* invoker, uint32 miscValue) override
    // {
    //     int32 eT = eventType;
    //     if (eT == 7166 && miscValue == 0)
    //     {
    //         if (Player* player = dynamic_cast<Player*>(invoker))
    //         {
    //             // Player summons a Shadow of Doom for 1 hour.
    //             player->CastSpell(nullptr, SPELL_SUMMON_BOSS, TRIGGERED_OLD_TRIGGERED);
    //             m_creature->CastSpell(nullptr, SPELL_QUIET_SUICIDE, TRIGGERED_OLD_TRIGGERED);
    //         }
    //     }
    //     if (eventType == AI_EVENT_CUSTOM_A && miscValue == NPC_CULTIST_ENGINEER)
    //     {
    //         m_creature->SetCorpseDelay(10); // Corpse despawns 10 seconds after a Shadow of Doom spawns.
    //         m_creature->CastSpell(m_creature, SPELL_CREATE_SUMMONER_SHIELD, TRIGGERED_OLD_TRIGGERED);
    //         m_creature->CastSpell(m_creature, SPELL_MINION_SPAWN_IN, TRIGGERED_OLD_TRIGGERED);
    //         ResetTimer(EVENT_CULTIST_CHANNELING, 1000);
    //     }
    // }
};

// struct SummonBoss : public SpellScript
// {
//     void OnSummon(Spell* spell, Creature* summon) const override
//     {
//         Unit* caster = spell->GetCaster();
//         summon->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
//         summon->SetFacingToObject(caster);
//         summon->AI()->SendAIEvent(AI_EVENT_CUSTOM_A, caster, summon, NPC_SHADOW_OF_DOOM);
//         if (caster->IsPlayer())
//             static_cast<Player*>(caster)->DestroyItemCount(ITEM_NECROTIC_RUNE, 8, true);
//     }
// };

/*
npc_minion
Notes: Shard Minions, Rares and Shadow of Doom.
*/
struct scourge_invasion_minion : public CombatAI
{
    scourge_invasion_minion(Creature* creature) : CombatAI(creature) // (creature, 999)
    {
        switch (me->GetEntry())
        {
            case NPC_SHADOW_OF_DOOM:
                // AddCombatAction(EVENT_DOOM_MINDFLAY, 2000u);
                // AddCombatAction(EVENT_DOOM_FEAR, 2000u);
                // AddCustomAction(EVENT_DOOM_START_ATTACK, 5000u, [&]()
                // {
                //     m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
                //     // Shadow of Doom seems to attack the Summoner here.
                //     if (Player* player = m_creature->GetMap()->GetPlayer(m_summonerGuid))
                //     {
                //         if (player->IsWithinLOSInMap(m_creature))
                //         {
                //             m_creature->SetInCombatWith(player);
                //             m_creature->SetDetectionRange(2.f);
                //             m_creature->AI()->AttackStart(player);
                //             ResetCombatAction(EVENT_DOOM_MINDFLAY, 2000u);
                //             ResetCombatAction(EVENT_DOOM_FEAR, 2000u);
                //         }
                //     }
                // });
                break;
            case NPC_FLAMESHOCKER:
                // AddCombatAction(EVENT_MINION_FLAMESHOCKERS_TOUCH, 2000u);
                // AddCustomAction(EVENT_MINION_FLAMESHOCKERS_DESPAWN, true, [&]()
                // {
                //     if (!m_creature->IsInCombat())
                //         m_creature->CastSpell(m_creature, SPELL_DESPAWNER_SELF, TRIGGERED_OLD_TRIGGERED);
                //     else
                //         ResetCombatAction(EVENT_MINION_FLAMESHOCKERS_DESPAWN, 60000);
                // });
                // m_creature->CastSpell(m_creature, SPELL_FLAMESHOCKER_IMMOLATE_VISUAL, TRIGGERED_OLD_TRIGGERED);
                break;
        }
    }

    ObjectGuid m_summonerGuid;

    // void ReceiveAIEvent(AIEventType eventType, Unit* /*sender*/, Unit* invoker, uint32 miscValue) override
    // {
    //     if (!invoker)
    //         return;

    //     if (eventType == AI_EVENT_CUSTOM_A)
    //     {
    //         if (miscValue == NPC_SHADOW_OF_DOOM)
    //         {
    //             m_summonerGuid = invoker->GetObjectGuid();
    //             // Pickup random emote like here: https://youtu.be/evOs9aJa2Jw?t=229
    //             DoBroadcastText(PickRandomValue(BCT_SHADOW_OF_DOOM_TEXT_0, BCT_SHADOW_OF_DOOM_TEXT_1, BCT_SHADOW_OF_DOOM_TEXT_2, BCT_SHADOW_OF_DOOM_TEXT_3), m_creature, invoker);
    //             m_creature->CastSpell(m_creature, SPELL_SPAWN_SMOKE, TRIGGERED_OLD_TRIGGERED);
    //         }
    //         if (miscValue == NPC_FLAMESHOCKER)
    //             ResetCombatAction(EVENT_MINION_FLAMESHOCKERS_DESPAWN, 60000);
    //     }
    // }

    void JustDied(Unit* /*pKiller*/) override
    {
        switch (me->GetEntry())
        {
            case NPC_SHADOW_OF_DOOM:
                me->CastSpell(me, SPELL_ZAP_CRYSTAL_CORPSE, true);
                break;
            case NPC_FLAMESHOCKER:
                me->CastSpell(me, SPELL_FLAMESHOCKERS_REVENGE, true);
                break;
        }
    }

    void SpellHit(Unit* /* caster */, SpellInfo const* spell) override
    {
        switch (spell->Id)
        {
            case SPELL_SPIRIT_SPAWN_OUT:
                me->DespawnOrUnsummon(3000);
                break;
        }
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        if (me->GetEntry() == NPC_FLAMESHOCKER)
            if (pWho->IsCreature() && me->IsWithinDistInMap(pWho, VISIBILITY_DISTANCE_TINY) && me->IsWithinLOSInMap(pWho) && !pWho->GetVictim())
                if (IsGuardOrBoss(pWho) && pWho->GetAI())
                    pWho->GetAI()->AttackStart(me);

        CombatAI::MoveInLineOfSight(pWho);
    }

    // void ExecuteAction(uint32 action) override
    // {
    //     switch (action)
    //     {
    //         case EVENT_DOOM_MINDFLAY:
    //         {
    //             DoCastSpellIfCan(m_creature->GetVictim(), SPELL_MINDFLAY);
    //             ResetCombatAction(EVENT_DOOM_MINDFLAY, urand(6500, 13000));
    //             break;
    //         }
    //         case EVENT_DOOM_FEAR:
    //         {
    //             DoCastSpellIfCan(m_creature->GetVictim(), SPELL_FEAR);
    //             ResetCombatAction(EVENT_DOOM_FEAR, 14500);
    //             break;
    //         }
    //         case EVENT_MINION_FLAMESHOCKERS_TOUCH:
    //         {
    //             DoCastSpellIfCan(m_creature->GetVictim(), PickRandomValue(SPELL_FLAMESHOCKERS_TOUCH, SPELL_FLAMESHOCKERS_TOUCH2), CAST_TRIGGERED);
    //             ResetCombatAction(EVENT_MINION_FLAMESHOCKERS_TOUCH, urand(30000, 45000));
    //             break;
    //         }
    //         default:
    //             break;
    //     }
    // }

    void UpdateAI(uint32 const diff) override
    {
        CombatAI::UpdateAI(diff);
        //UpdateTimers(diff, m_creature->SelectHostileTarget());

        // Instakill every mob nearby, except Players, Pets or NPCs with the same faction.
        // m_creature->IsValidAttackTarget(m_creature->GetVictim(), true)
        if (me->GetEntry() != NPC_FLAMESHOCKER && me->IsWithinDistInMap(me->GetVictim(), 30.0f) && !me->GetVictim()->IsControlledByPlayer() && me->CanCreatureAttack(me->GetVictim()), true)
            me->CastSpell(me->GetVictim(), SPELL_SCOURGE_STRIKE, true);

        DoMeleeAttackIfReady();
    }
};

struct npc_pallid_horror : public CombatAI
{
    std::set<ObjectGuid> m_flameshockers;
    std::unordered_map<ObjectGuid, uint32> m_flameshockFollowers;

    npc_pallid_horror(Creature* creature) : CombatAI(creature) // 999
    {
        // AddCustomAction(25, 2500u, [&]() // Hacky solution for flameshockers to remain in formation
        // {
        //     for (std::pair<ObjectGuid, uint32> follower : m_flameshockFollowers)
        //     {
        //         if (Unit* f = m_creature->GetMap()->GetUnit(follower.first))
        //         {
        //             if (!f->IsInCombat() && f->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        //             {
        //                 float angle = (float(follower.second) * (M_PI_F / (float(m_flameshockFollowers.size()) / 2.f))) + m_creature->GetOrientation();
        //                 f->GetMotionMaster()->Clear(true, true);
        //                 f->GetMotionMaster()->MoveFollow(m_creature, 2.5f, angle);
        //             }
        //         }
        //     }
        //     ResetTimer(25, 2500u);
        // });
        uint32 amount = urand(5, 9); // sniffed are group sizes of 5-9 shockers on spawn.

        if (me->GetHealthPct() == 100.0f)
        {
            for (uint32 i = 0; i < amount; ++i)
            {
                if (Creature* _flameshocker = me->SummonCreature(NPC_FLAMESHOCKER, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, HOUR * IN_MILLISECONDS))
                {
                    float angle = (float(i) * (M_PIf / (static_cast<float>(amount) / 2.f))) + me->GetOrientation();
                    //pFlameshocker->JoinCreatureGroup(m_creature, 5.0f, angle - M_PI, OPTION_FORMATION_MOVE); // Perfect Circle around the Pallid.
                    _flameshocker->GetMotionMaster()->Clear(true);
                    _flameshocker->GetMotionMaster()->MoveFollow(me, 2.5f, angle);
                    _flameshocker->CastSpell(_flameshocker, SPELL_MINION_SPAWN_IN, true);
                    _flameshocker->SetWalk(false);
                    m_flameshockers.insert(_flameshocker->GetGUID());
                    m_flameshockFollowers.insert(std::pair<ObjectGuid, uint32>(_flameshocker->GetGUID(), i));
                }
            }
        }
        me->SetCorpseDelay(10); // Corpse despawns 10 seconds after a crystal spawns.
        // AddCombatAction(EVENT_PALLID_RANDOM_YELL, 5000u);
        // AddCombatAction(EVENT_PALLID_SPELL_DAMAGE_VS_GUARDS, 5000u);
        // AddCombatAction(EVENT_PALLID_SUMMON_FLAMESHOCKER, 5000u);
        // AddCombatAction(25, 5000u);
        // WEATHER_TYPE_STORM
        if (Weather* weather = WeatherMgr::FindWeather(me->GetZoneId()))
            weather->SetWeather(WEATHER_TYPE_STORM, 0.25f);
    }

    void Reset() override
    {
        CombatAI::Reset();
        //m_creature->AddAura(SPELL_AURA_OF_FEAR);

        // DoCastSpellIfCan(nullptr, SPELL_AURA_OF_FEAR, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
        // ResetTimer(25, 0);
        me->SetWalk(false);
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        if (pWho->IsCreature() && me->IsWithinDistInMap(pWho, VISIBILITY_DISTANCE_TINY) && me->IsWithinLOSInMap(pWho) && !pWho->GetVictim())
            if (IsGuardOrBoss(pWho) && pWho->GetAI())
                pWho->GetAI()->AttackStart(me);

        CombatAI::MoveInLineOfSight(pWho);
    }

    void JustDied(Unit* /*unit*/) override
    {
        //if (Creature* creature = GetClosestCreatureWithEntry(me, NPC_HIGHLORD_BOLVAR_FORDRAGON, VISIBILITY_DISTANCE_NORMAL))
        //    DoBroadcastText(BCT_STORMWIND_BOLVAR_2, creature, me, CHAT_TYPE_ZONE_YELL);

        //if (Creature* creature = GetClosestCreatureWithEntry(me, NPC_LADY_SYLVANAS_WINDRUNNER, VISIBILITY_DISTANCE_NORMAL))
        //    DoBroadcastText(BCT_UNDERCITY_SYLVANAS_1, creature, me, CHAT_TYPE_ZONE_YELL);

        // Remove all custom summoned Flameshockers.
        for (auto const& guid : m_flameshockers)
            if (Creature* flameshocker = me->GetMap()->GetCreature(guid))
                flameshocker->KillSelf(); //pFlameshocker->DoKillUnit(pFlameshocker);

        me->CastSpell(me, (me->GetZoneId() == AREA_UNDERCITY ? SPELL_SUMMON_FAINT_NECROTIC_CRYSTAL : SPELL_SUMMON_CRACKED_NECROTIC_CRYSTAL), true);
        me->RemoveAurasDueToSpell(SPELL_AURA_OF_FEAR);

        // TimePoint now = me->GetMap()->GetCurrentClockTime();
        TimePoint now = TimePoint();
        uint32 cityAttackTimer = urand(CITY_ATTACK_TIMER_MIN, CITY_ATTACK_TIMER_MAX);
        TimePoint nextAttack = now + std::chrono::seconds(cityAttackTimer);
        uint64 timeToNextAttack = std::chrono::duration_cast<std::chrono::minutes>(nextAttack - now).count();
        SITimers index = me->GetZoneId() == AREA_UNDERCITY ? SI_TIMER_UNDERCITY : SI_TIMER_STORMWIND;
        sWorldState->SetSITimer(index, nextAttack);
        sWorldState->SetPallidGuid(index, ObjectGuid());

        if (Weather* weather = WeatherMgr::FindWeather(me->GetZoneId()))
            weather->SetWeather(WEATHER_TYPE_RAIN, 0.0f);

        LOG_DEBUG("gameevent", "[Scourge Invasion Event] The Scourge has been defeated in {}, next attack starting in {} minutes", me->GetZoneId() == AREA_UNDERCITY ? "Undercity" : "Stormwind", timeToNextAttack);
    }

    void SummonedCreatureDies(Creature* unit, Unit* /*killer*/) override
    {
        // Remove dead Flameshockers here to respawn them if needed.
        if (m_flameshockers.contains(unit->GetGUID()))
            m_flameshockers.erase(unit->GetGUID());
        if (m_flameshockFollowers.contains(unit->GetGUID()))
            m_flameshockFollowers.erase(unit->GetGUID());
    }

    void SummonedCreatureDespawn(Creature* unit) override
    {
        // Remove despawned Flameshockers here to respawn them if needed.
        if (m_flameshockers.contains(unit->GetGUID()))
            m_flameshockers.erase(unit->GetGUID());
        if (m_flameshockFollowers.contains(unit->GetGUID()))
            m_flameshockFollowers.erase(unit->GetGUID());
    }

    void CorpseRemoved(uint32& /*respawnDelay*/) override
    {
        // Remove all custom summoned Flameshockers.
        for (auto const& guid : m_flameshockers)
            if (Creature* flameshocker = me->GetMap()->GetCreature(guid))
                // flameshocker->AddObjectToRemoveList();
                flameshocker->DespawnOrUnsummon();
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case EVENT_PALLID_RANDOM_YELL:
            {
                // DoBroadcastText(PickRandomValue(BCT_PALLID_HORROR_YELL1, BCT_PALLID_HORROR_YELL2, BCT_PALLID_HORROR_YELL3, BCT_PALLID_HORROR_YELL4,
                //     BCT_PALLID_HORROR_YELL5, BCT_PALLID_HORROR_YELL6, BCT_PALLID_HORROR_YELL7, BCT_PALLID_HORROR_YELL8), me, nullptr, CHAT_TYPE_ZONE_YELL);
                // ResetCombatAction(EVENT_PALLID_RANDOM_YELL, urand(IN_MILLISECONDS * 65, IN_MILLISECONDS * 300));
                break;
            }
            case EVENT_PALLID_SPELL_DAMAGE_VS_GUARDS:
            {
                // DoCastSpellIfCan(me->GetVictim(), SPELL_DAMAGE_VS_GUARDS, CAST_TRIGGERED);
                // ResetCombatAction(EVENT_PALLID_SPELL_DAMAGE_VS_GUARDS, urand(11000, 81000));
                break;
            }
            case EVENT_PALLID_SUMMON_FLAMESHOCKER:
            {
                if (m_flameshockers.size() < 30)
                {
                    if (Unit* target = SelectRandomFlameshockerSpawnTarget(me, nullptr, DEFAULT_VISIBILITY_BGARENAS))
                    {
                        float x, y, z;
                        target->GetNearPoint(target, x, y, z, 5.0f, 5.0f, 0.0f);
                        if (Creature* flameshocker = me->SummonCreature(NPC_FLAMESHOCKER, x, y, z, target->GetOrientation(), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, IN_MILLISECONDS * HOUR))
                        {
                            m_flameshockers.insert(flameshocker->GetGUID());
                            flameshocker->CastSpell(flameshocker, SPELL_MINION_SPAWN_IN, true);
                            // flameshocker->AI()->SendAIEvent(AI_EVENT_CUSTOM_A, flameshocker, flameshocker, NPC_FLAMESHOCKER);
                        }
                    }
                }
                // ResetCombatAction(EVENT_PALLID_SUMMON_FLAMESHOCKER, 2000);
                break;
            }
            default:
                break;
        }
    }
};

// struct DespawnerSelf : public SpellScript
// {
//     void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
//     {
//         Unit* caster = spell->GetCaster();
//         if (!caster->IsInCombat())
//             caster->CastSpell(nullptr, SPELL_SPIRIT_SPAWN_OUT, true);
//     }
// };
//
// struct CommuniqueTrigger : public SpellScript
// {
//     void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
//     {
//         if (Unit* target = spell->GetUnitTarget())
//             target->CastSpell(nullptr, SPELL_COMMUNIQUE_CAMP_TO_RELAY, true);
//     }
// };

void AddSC_scourge_invasion()
{
    RegisterGameObjectAI(scourge_invasion_go_circle);
    RegisterGameObjectAI(scourge_invasion_go_necropolis);
    RegisterCreatureAI(scourge_invasion_mouth);
    RegisterCreatureAI(scourge_invasion_necropolis);
    RegisterCreatureAI(scourge_invasion_necropolis_health);
    RegisterCreatureAI(scourge_invasion_necropolis_proxy);
    RegisterCreatureAI(scourge_invasion_necropolis_relay);
    RegisterCreatureAI(scourge_invasion_necrotic_shard);
    RegisterCreatureAI(scourge_invasion_minion_spawner);
    RegisterCreatureAI(scourge_invasion_minion);
    RegisterCreatureAI(npc_pallid_horror);
    RegisterCreatureAI(scourge_invasion_cultist_engineer);
    RegisterCreatureAI(scourge_invasion_minion);
    // RegisterSpellScript<SummonBoss>("spell_summon_boss");
    // RegisterSpellScript<DespawnerSelf>("spell_despawner_self");
    // RegisterSpellScript<CommuniqueTrigger>("spell_communique_trigger");
}
