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

#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"

/*######
## npc_lord_gregor_lescovar
######*/

enum LordGregorLescovar
{
    SAY_GUARD_2    = 0,
    SAY_LESCOVAR_2 = 0,
    SAY_LESCOVAR_3 = 1,
    SAY_LESCOVAR_4 = 2,
    SAY_MARZON_1   = 0,
    SAY_MARZON_2   = 1,
    SAY_TYRION_2   = 1,

    NPC_STORMWIND_ROYAL = 1756,
    NPC_MARZON_BLADE    = 1755,
    NPC_TYRION          = 7766,

    QUEST_THE_ATTACK    = 434
};

class npc_lord_gregor_lescovar : public CreatureScript
{
public:
    npc_lord_gregor_lescovar() : CreatureScript("npc_lord_gregor_lescovar") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_lord_gregor_lescovarAI(creature);
    }

    struct npc_lord_gregor_lescovarAI : public npc_escortAI
    {
        npc_lord_gregor_lescovarAI(Creature* creature) : npc_escortAI(creature) { }

        uint32 uiTimer;
        uint32 uiPhase;

        ObjectGuid MarzonGUID;

        void Reset() override
        {
            uiTimer = 0;
            uiPhase = 0;

            MarzonGUID.Clear();
        }

        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            me->DisappearAndDie();

            if (Creature* pMarzon = ObjectAccessor::GetCreature(*me, MarzonGUID))
            {
                if (pMarzon->IsAlive())
                    pMarzon->DisappearAndDie();
            }
        }

        void JustEngagedWith(Unit* who) override
        {
            if (Creature* pMarzon = ObjectAccessor::GetCreature(*me, MarzonGUID))
            {
                if (pMarzon->IsAlive() && !pMarzon->IsInCombat())
                    pMarzon->AI()->AttackStart(who);
            }
        }

        void WaypointReached(uint32 waypointId) override
        {
            switch (waypointId)
            {
                case 14:
                    SetEscortPaused(true);
                    Talk(SAY_LESCOVAR_2);
                    uiTimer = 3000;
                    uiPhase = 1;
                    break;
                case 16:
                    SetEscortPaused(true);
                    if (Creature* pMarzon = me->SummonCreature(NPC_MARZON_BLADE, -8411.360352f, 480.069733f, 123.760895f, 4.941504f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 1000))
                    {
                        pMarzon->GetMotionMaster()->MovePoint(0, -8408.000977f, 468.611450f, 123.759903f);
                        MarzonGUID = pMarzon->GetGUID();
                    }
                    uiTimer = 2000;
                    uiPhase = 4;
                    break;
            }
        }
        //TO-DO: We don't have movemaps, also we can't make 2 npcs walks to one point propperly (and we can not use escort ai, because they are 2 different spawns and with same entry), because of it we make them, disappear.
        void DoGuardsDisappearAndDie()
        {
            std::list<Creature*> GuardList;
            me->GetCreatureListWithEntryInGrid(GuardList, NPC_STORMWIND_ROYAL, 8.0f);
            if (!GuardList.empty())
            {
                for (std::list<Creature*>::const_iterator itr = GuardList.begin(); itr != GuardList.end(); ++itr)
                {
                    if (Creature* pGuard = *itr)
                        pGuard->DisappearAndDie();
                }
            }
        }

        void UpdateAI(uint32 uiDiff) override
        {
            if (uiPhase)
            {
                if (uiTimer <= uiDiff)
                {
                    switch (uiPhase)
                    {
                        case 1:
                            if (Creature* pGuard = me->FindNearestCreature(NPC_STORMWIND_ROYAL, 8.0f, true))
                                pGuard->AI()->Talk(SAY_GUARD_2);
                            uiTimer = 3000;
                            uiPhase = 2;
                            break;
                        case 2:
                            DoGuardsDisappearAndDie();
                            uiTimer = 2000;
                            uiPhase = 3;
                            break;
                        case 3:
                            SetEscortPaused(false);
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                        case 4:
                            Talk(SAY_LESCOVAR_3);
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                        case 5:
                            if (Creature* pMarzon = ObjectAccessor::GetCreature(*me, MarzonGUID))
                                pMarzon->AI()->Talk(SAY_MARZON_1);
                            uiTimer = 3000;
                            uiPhase = 6;
                            break;
                        case 6:
                            Talk(SAY_LESCOVAR_4);
                            if (Player* player = GetPlayerForEscort())
                                player->GroupEventHappens(QUEST_THE_ATTACK, me);
                            uiTimer = 2000;
                            uiPhase = 7;
                            break;
                        case 7:
                            if (Creature* pTyrion = me->FindNearestCreature(NPC_TYRION, 20.0f, true))
                                pTyrion->AI()->Talk(SAY_TYRION_2);
                            if (Creature* pMarzon = ObjectAccessor::GetCreature(*me, MarzonGUID))
                                pMarzon->SetFaction(FACTION_MONSTER);
                            me->SetFaction(FACTION_MONSTER);
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                    }
                }
                else uiTimer -= uiDiff;
            }
            npc_escortAI::UpdateAI(uiDiff);

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    };
};

/*######
## npc_marzon_silent_blade
######*/

class npc_marzon_silent_blade : public CreatureScript
{
public:
    npc_marzon_silent_blade() : CreatureScript("npc_marzon_silent_blade") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_marzon_silent_bladeAI(creature);
    }

    struct npc_marzon_silent_bladeAI : public ScriptedAI
    {
        npc_marzon_silent_bladeAI(Creature* creature) : ScriptedAI(creature)
        {
            me->SetWalk(true);
        }

        void Reset() override
        {
            me->RestoreFaction();
        }

        void JustEngagedWith(Unit* who) override
        {
            Talk(SAY_MARZON_2);

            if (me->IsSummon())
            {
                if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                {
                    if (summoner->IsCreature() && summoner->IsAlive() && !summoner->IsInCombat())
                        summoner->ToCreature()->AI()->AttackStart(who);
                }
            }
        }

        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            me->DisappearAndDie();

            if (me->IsSummon())
            {
                if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                {
                    if (summoner->IsCreature() && summoner->IsAlive())
                        summoner->ToCreature()->DisappearAndDie();
                }
            }
        }

        void MovementInform(uint32 uiType, uint32 /*uiId*/) override
        {
            if (uiType != POINT_MOTION_TYPE)
                return;

            if (me->IsSummon())
            {
                Unit* summoner = me->ToTempSummon()->GetSummonerUnit();
                if (summoner && summoner->IsCreature() && summoner->IsAIEnabled)
                {
                    npc_lord_gregor_lescovar::npc_lord_gregor_lescovarAI* ai =
                        CAST_AI(npc_lord_gregor_lescovar::npc_lord_gregor_lescovarAI, summoner->GetAI());
                    if (ai)
                    {
                        ai->uiTimer = 2000;
                        ai->uiPhase = 5;
                    }
                    //me->ChangeOrient(0.0f, summoner);
                }
            }
        }

        void UpdateAI(uint32 /*diff*/) override
        {
            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    };
};

/*######
## npc_tyrion_spybot
######*/

enum TyrionSpybot
{
    SAY_QUEST_ACCEPT_ATTACK  = 0,
    SAY_SPYBOT_1             = 1,
    SAY_SPYBOT_2             = 2,
    SAY_SPYBOT_3             = 3,
    SAY_SPYBOT_4             = 4,
    SAY_TYRION_1             = 0,
    SAY_GUARD_1              = 1,
    SAY_LESCOVAR_1           = 3,

    NPC_PRIESTESS_TYRIONA    = 7779,
    NPC_LORD_GREGOR_LESCOVAR = 1754,
};

class npc_tyrion_spybot : public CreatureScript
{
public:
    npc_tyrion_spybot() : CreatureScript("npc_tyrion_spybot") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_tyrion_spybotAI(creature);
    }

    struct npc_tyrion_spybotAI : public npc_escortAI
    {
        npc_tyrion_spybotAI(Creature* creature) : npc_escortAI(creature) { }

        uint32 uiTimer;
        uint32 uiPhase;

        void Reset() override
        {
            uiTimer = 0;
            uiPhase = 0;
        }

        void WaypointReached(uint32 waypointId) override
        {
            switch (waypointId)
            {
                case 1:
                    SetEscortPaused(true);
                    uiTimer = 2000;
                    uiPhase = 1;
                    break;
                case 5:
                    SetEscortPaused(true);
                    Talk(SAY_SPYBOT_1);
                    uiTimer = 2000;
                    uiPhase = 5;
                    break;
                case 17:
                    SetEscortPaused(true);
                    Talk(SAY_SPYBOT_3);
                    uiTimer = 3000;
                    uiPhase = 8;
                    break;
            }
        }

        void UpdateAI(uint32 uiDiff) override
        {
            if (uiPhase)
            {
                if (uiTimer <= uiDiff)
                {
                    switch (uiPhase)
                    {
                        case 1:
                            Talk(SAY_QUEST_ACCEPT_ATTACK);
                            uiTimer = 3000;
                            uiPhase = 2;
                            break;
                        case 2:
                            if (Creature* pTyrion = me->FindNearestCreature(NPC_TYRION, 10.0f))
                            {
                                if (Player* player = GetPlayerForEscort())
                                    pTyrion->AI()->Talk(SAY_TYRION_1, player);
                            }
                            uiTimer = 3000;
                            uiPhase = 3;
                            break;
                        case 3:
                            me->UpdateEntry(NPC_PRIESTESS_TYRIONA);
                            uiTimer = 2000;
                            uiPhase = 4;
                            break;
                        case 4:
                            SetEscortPaused(false);
                            uiPhase = 0;
                            uiTimer = 0;
                            break;
                        case 5:
                            if (Creature* pGuard = me->FindNearestCreature(NPC_STORMWIND_ROYAL, 10.0f, true))
                                pGuard->AI()->Talk(SAY_GUARD_1);
                            uiTimer = 3000;
                            uiPhase = 6;
                            break;
                        case 6:
                            Talk(SAY_SPYBOT_2);
                            uiTimer = 3000;
                            uiPhase = 7;
                            break;
                        case 7:
                            SetEscortPaused(false);
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                        case 8:
                            if (Creature* pLescovar = me->FindNearestCreature(NPC_LORD_GREGOR_LESCOVAR, 10.0f))
                                pLescovar->AI()->Talk(SAY_LESCOVAR_1);
                            uiTimer = 3000;
                            uiPhase = 9;
                            break;
                        case 9:
                            Talk(SAY_SPYBOT_4);
                            uiTimer = 3000;
                            uiPhase = 10;
                            break;
                        case 10:
                            if (Creature* pLescovar = me->FindNearestCreature(NPC_LORD_GREGOR_LESCOVAR, 10.0f))
                            {
                                if (Player* player = GetPlayerForEscort())
                                {
                                    CAST_AI(npc_lord_gregor_lescovar::npc_lord_gregor_lescovarAI, pLescovar->AI())->Start(false, false, player->GetGUID());
                                    CAST_AI(npc_lord_gregor_lescovar::npc_lord_gregor_lescovarAI, pLescovar->AI())->SetMaxPlayerDistance(200.0f);
                                }
                            }
                            me->DisappearAndDie();
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                    }
                }
                else uiTimer -= uiDiff;
            }
            npc_escortAI::UpdateAI(uiDiff);

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    };
};

/*######
## npc_tyrion
######*/

enum Tyrion
{
    NPC_TYRION_SPYBOT = 8856
};

class npc_tyrion : public CreatureScript
{
public:
    npc_tyrion() : CreatureScript("npc_tyrion") { }

    bool OnQuestAccept(Player* player, Creature* creature, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_THE_ATTACK)
        {
            if (Creature* pSpybot = creature->FindNearestCreature(NPC_TYRION_SPYBOT, 5.0f, true))
            {
                CAST_AI(npc_tyrion_spybot::npc_tyrion_spybotAI, pSpybot->AI())->Start(false, false, player->GetGUID());
                CAST_AI(npc_tyrion_spybot::npc_tyrion_spybotAI, pSpybot->AI())->SetMaxPlayerDistance(200.0f);
            }
            return true;
        }
        return false;
    }
};

enum KingVarianWrynn : uint32
{
    // Deathknight Starting Zone End
    QUEST_WHERE_KINGS_WALK       = 13188,
};

class npc_king_varian_wrynn : public CreatureScript
{
public:
    npc_king_varian_wrynn() : CreatureScript("npc_king_varian_wrynn") { }

    bool OnQuestReward(Player* player, Creature* /*creature*/, Quest const* quest, uint32 /*item*/) override
    {

        if (quest->GetQuestId() == QUEST_WHERE_KINGS_WALK)
            sLFGMgr->InitializeLockedDungeons(player);

        return true;
    }
};

void AddSC_stormwind_city()
{
    new npc_tyrion();
    new npc_tyrion_spybot();
    new npc_lord_gregor_lescovar();
    new npc_marzon_silent_blade();
    new npc_king_varian_wrynn();
}
