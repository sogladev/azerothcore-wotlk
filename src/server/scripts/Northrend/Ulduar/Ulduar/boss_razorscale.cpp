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

#include "AchievementCriteriaScript.h"
#include "GameObjectAI.h"
#include "GameObjectScript.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "TaskScheduler.h"
#include "ulduar.h"

/* --------------- Spells --------------- */
enum RazorscaleSpells
{
    // Razorscale
    SPELL_FLAME_BUFFET                      = 64016,
    SPELL_FLAME_BREATH                      = 63317,
    SPELL_FLAME_BREATH_25                   = 64021,
    SPELL_WING_BUFFET                       = 62666,
    SPELL_FIREBALL                          = 63815,
    SPELL_FIREBOLT                          = 62669,
    SPELL_DEVOURING_FLAME                   = 63236,
    SPELL_DEVOURING_FLAME_GROUND            = 64709,
    SPELL_DEVOURING_FLAME_GROUND_25         = 64734,
    SPELL_FUSE_ARMOR                        = 64821,
    SPELL_FUSED_ARMOR                       = 64774,
    SPELL_BERSERK                           = 47008,
    SPELL_STUN_SELF                         = 62794,

    // Harpoon Fire State
    SPELL_HARPOON_FIRE_STATE                = 62696,

    // Harpoon Shots
    SPELL_HARPOON_SHOT_1                    = 63658,
    SPELL_HARPOON_SHOT_2                    = 63657,
    SPELL_HARPOON_SHOT_3                    = 63659,
    SPELL_HARPOON_SHOT_4                    = 63524,
    SPELL_HARPOON_TRIGGER                   = 62505,

    // Spawner
    SPELL_SUMMON_MOLE_MACHINE               = 62899,
    SPELL_SUMMON_IRON_DWARF_GUARDIAN        = 62926,
    SPELL_SUMMON_IRON_DWARF_WATCHER         = 63135,
    SPELL_TRIGGER_SUMMON_IRON_DWARVES       = 63968,
    SPELL_TRIGGER_SUMMON_IRON_DWARVES_2     = 63970,
    SPELL_TRIGGER_SUMMON_IRON_DWARVES_3     = 63969,
    SPELL_TRIGGER_SUMMON_IRON_VRYKUL        = 63798,

    // Dark Rune Sentinel
    SPELL_BATTLE_SHOUT                      = 46763,
    SPELL_WHIRLWIND                         = 63808,
    SPELL_HEROIC_STRIKE                     = 45026,

    // Dark Rune Guardian
    SPELL_STORMSTRIKE                       = 64757,

    // Dark Rune Watcher
    SPELL_LIGHTNING_BOLT                    = 63809,
    SPELL_LIGHTNING_BOLT_25                 = 64696,
    SPELL_CHAIN_LIGHTNING                   = 64758,
    SPELL_CHAIN_LIGHTNING_25                = 64759,

    // Trapper
    SPELL_SHACKLE                           = 62646,

    // Defender
    SPELL_THREAT                            = 65146,
};

/* --------------- Events --------------- */
enum RazorscaleEvents
{
    EVENT_BERSERK = 1,
    EVENT_FIREBALL_AIR,
    EVENT_DEVOURING_FLAME,
    EVENT_SUMMON_MINIONS,
    EVENT_SUMMON_MINIONS_DELAYED,
    EVENT_FLAME_BREATH_GROUNDED,
    EVENT_WING_BUFFET,
    EVENT_RESUME_AIR,
    EVENT_FLAME_BREATH,
    EVENT_FIREBOLT,
    EVENT_FUSE_ARMOR,
    EVENT_RESUME_CHASE,

    EVENT_BUILD_HARPOON_1,
    EVENT_BUILD_HARPOON_2,
    EVENT_BUILD_HARPOON_3,
    EVENT_BUILD_HARPOON_4,
    EVENT_DESTROY_HARPOONS,

    EVENT_START_COMBAT,
    EVENT_HEROIC_STRIKE,
    EVENT_BATTLE_SHOUT,
    EVENT_WHIRLWIND,
    EVENT_LIGHTNING_BOLT,
    EVENT_CHAIN_LIGHTNING,
    EVENT_STORMSTRIKE,
};

/* --------------- Actions --------------- */
enum RazorscaleActions
{
    ACTION_START_FIGHT = 1,
    ACTION_GROUND_PHASE,
    ACTION_START_PERMA_GROUND,
    ACTION_BUILD_HARPOON_1,
    ACTION_BUILD_HARPOON_2,
    ACTION_BUILD_HARPOON_3,
    ACTION_BUILD_HARPOON_4,
    ACTION_DESTROY_HARPOONS,
    ACTION_STOP_CONTROLLERS,
    ACTION_ENGINEER_DEAD,
};

/* --------------- Phases --------------- */
enum RazorscalePhases
{
    PHASE_NONE = 0,
    PHASE_AIR,
    PHASE_GROUND,
    PHASE_PERMA_GROUND,
};

/* --------------- Misc --------------- */
enum RazorscaleMisc
{
    DATA_QUICK_SHAVE_ID       = 29192921,
    DATA_IRON_DWARF_ID        = 29232924,
    GOSSIP_START_ENCOUNTER    = 0,
    GROUP_EXPEDITION          = 1,
    GROUP_FIRE_STATE_10       = 2,
    GROUP_FIRE_STATE_25       = 3,
    POINT_RAZORSCALE_FLIGHT   = 1,
    POINT_RAZORSCALE_LAND     = 2,
    POINT_RAZORSCALE_GROUND   = 3,
    POINT_RAZORSCALE_TAKEOFF  = 4,
    POINT_RAZORSCALE_FLIGHT_2 = 5,
    POINT_DEFENDER_ATTACK     = 10,
};


static Position const RazorFlightPos      = { 585.3610f, -173.5592f, 456.8430f, 1.526665f };
static Position const RazorFlightPos2     = { 619.1450f, -238.0780f, 475.1800f, 1.423917f };
static Position const RazorLandPos        = { 585.4010f, -173.5430f, 408.5080f, 1.570796f };
static Position const RazorGroundPos      = { 585.4010f, -173.5430f, 391.6421f, 1.570796f };

static Position const BrokenHarpoonPos[4] =
{
    { 571.9465f, -136.0118f, 391.5171f, 2.286379f },
    { 589.9233f, -133.6223f, 391.8968f, 3.298687f },
    { 559.1199f, -140.5058f, 391.1803f, 4.049168f },
    { 606.2297f, -136.7212f, 391.1803f, 5.131269f }
};

static Position const HarpoonPositions[4] =
{
    { 571.9012f, -136.5541f, 391.5171f, 4.921829f },
    { 589.9233f, -133.6223f, 391.8968f, 4.81711f  },
    { 559.1199f, -140.5058f, 391.1803f, 5.061456f },
    { 606.2297f, -136.7212f, 391.1803f, 4.537859f }
};

static Position const DefenderPositions[6] =
{
    { 624.3065f, -154.4163f, 391.6442f, 0.0f },
    { 611.6274f, -170.9375f, 391.8087f, 0.0f },
    { 572.1548f, -167.4471f, 391.8087f, 0.0f },
    { 558.4640f, -165.0114f, 391.8087f, 0.0f },
    { 603.3345f, -164.4297f, 391.8087f, 0.0f },
    { 549.1727f, -159.1180f, 391.8087f, 0.0f }
};

static Position const TrapperPositions[3] =
{
    { 574.9293f, -184.5150f, 391.8921f, 0.0f },
    { 539.7838f, -178.5337f, 391.3053f, 0.0f },
    { 627.1754f, -177.9638f, 391.5553f, 0.0f }
};

static uint32 const DwarfSummonSpells[3] =
{
    SPELL_TRIGGER_SUMMON_IRON_DWARVES,
    SPELL_TRIGGER_SUMMON_IRON_DWARVES_2,
    SPELL_TRIGGER_SUMMON_IRON_DWARVES_3
};

/* ==================== boss_razorscale ==================== */

struct boss_razorscale : public BossAI
{
    boss_razorscale(Creature* creature) : BossAI(creature, BOSS_RAZORSCALE)
    {
        Initialize();
    }

    void Initialize()
    {
        _engineersAlive = 3;
        _defenderCnt = 0;
        _trapperCnt = 0;
        _harpoonHits = 0;
        _flyCount = 0;
        _permaGround = false;
        me->SetDisableGravity(true);
    }

    void Reset() override
    {
        _Reset();
        Initialize();
        events.SetPhase(PHASE_NONE);
        me->SummonCreatureGroup(GROUP_EXPEDITION);
        me->SummonCreatureGroup(GROUP_FIRE_STATE_10);
        if (Is25ManRaid())
            me->SummonCreatureGroup(GROUP_FIRE_STATE_25);
        me->SetImmuneToPC(true);
        me->SetCombatMovement(false);
        me->SetAnimTier(AnimTier::Fly);
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        me->SetImmuneToPC(false);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me);
        ScheduleAirEvents();
        summons.DoAction(ACTION_START_FIGHT);
        events.ScheduleEvent(EVENT_BERSERK, 10min);
    }

    void ScheduleAirEvents()
    {
        events.SetPhase(PHASE_AIR);
        events.ScheduleEvent(EVENT_FIREBALL_AIR, 3s, 0, PHASE_AIR);
        events.ScheduleEvent(EVENT_DEVOURING_FLAME, 9s, 0, PHASE_AIR);
        events.ScheduleEvent(EVENT_SUMMON_MINIONS, 1s, 0, PHASE_AIR);
    }

    void ScheduleGroundEvents()
    {
        events.SetPhase(PHASE_PERMA_GROUND);
        events.ScheduleEvent(EVENT_FIREBOLT, 3s, 0, PHASE_PERMA_GROUND);
        events.ScheduleEvent(EVENT_FUSE_ARMOR, 15s, 0, PHASE_PERMA_GROUND);
        events.ScheduleEvent(EVENT_FLAME_BREATH_GROUNDED, 18s, 0, PHASE_PERMA_GROUND);
        events.ScheduleEvent(EVENT_DEVOURING_FLAME, 22s, 0, PHASE_PERMA_GROUND);
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_START_FIGHT:
                me->SetImmuneToPC(false);
                me->SetSpeedRate(MOVE_RUN, 3.0f);
                me->StopMoving();
                me->GetMotionMaster()->MovePoint(POINT_RAZORSCALE_FLIGHT, RazorFlightPos, FORCED_MOVEMENT_NONE, 0.0f, false, false, AnimTier::Fly);
                break;
            case ACTION_GROUND_PHASE:
                me->InterruptNonMeleeSpells(false);
                events.SetPhase(PHASE_GROUND);
                _harpoonHits = 0;
                me->SetSpeedRate(MOVE_RUN, 3.0f);
                me->GetMotionMaster()->MovePoint(POINT_RAZORSCALE_LAND, RazorLandPos, FORCED_MOVEMENT_NONE, 0.0f, false, false, AnimTier::Fly);
                break;
            case ACTION_START_PERMA_GROUND:
                me->SetDisableGravity(false);
                me->RemoveAura(SPELL_STUN_SELF);
                Talk(0);
                DoCastSelf(SPELL_WING_BUFFET);
                events.ScheduleEvent(EVENT_RESUME_CHASE, 1s);
                ScheduleGroundEvents();
                break;
        }
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE && type != EFFECT_MOTION_TYPE)
            return;

        switch (pointId)
        {
            case POINT_RAZORSCALE_FLIGHT:
                me->SetFacingTo(RazorFlightPos.GetOrientation());
                DoZoneInCombat();
                break;
            case POINT_RAZORSCALE_LAND:
                me->SetDisableGravity(false);
                if (!_permaGround)
                {
                    DoCastSelf(SPELL_STUN_SELF, true);
                    events.ScheduleEvent(EVENT_FLAME_BREATH, 30s, 0, PHASE_GROUND);
                }
                break;
            case POINT_RAZORSCALE_TAKEOFF:
                me->SetSpeedRate(MOVE_RUN, 3.0f);
                me->GetMotionMaster()->MovePoint(POINT_RAZORSCALE_FLIGHT_2, RazorFlightPos2, FORCED_MOVEMENT_NONE, 0.0f, false, false, AnimTier::Fly);
                break;
            case POINT_RAZORSCALE_FLIGHT_2:
                me->SetFacingTo(RazorFlightPos2.GetOrientation());
                me->SetReactState(REACT_AGGRESSIVE);
                ScheduleAirEvents();
                ++_flyCount;
                break;
        }
    }

    void SpellHit(Unit*, SpellInfo const* spell) override
    {
        if (spell->Id == SPELL_HARPOON_TRIGGER)
        {
            _harpoonHits++;
            if (_harpoonHits >= RAID_MODE<uint32>(2, 4))
                DoAction(ACTION_GROUND_PHASE);
        }
    }

    void SummonMinions()
    {
        float x = frand(540.0f, 640.0f);
        float y = frand(-230.0f, -195.0f);
        float z = 391.517f;
        me->SummonCreature(NPC_RAZORSCALE_SPAWNER, x, y, z, 0, TEMPSUMMON_TIMED_DESPAWN, 15000);
    }

    void DamageTaken(Unit*, uint32& damage, DamageEffectType, SpellSchoolMask) override
    {
        if (!_permaGround && me->HealthBelowPctDamaged(50, damage) && events.IsInPhase(PHASE_GROUND))
        {
            _permaGround = true;
            me->SetReactState(REACT_AGGRESSIVE);
            DoAction(ACTION_START_PERMA_GROUND);
        }
    }

    void JustSummoned(Creature* summon) override
    {
        BossAI::JustSummoned(summon);
        if (summon->GetEntry() == NPC_EXPEDITION_DEFENDER)
            summon->AI()->SetData(0, _defenderCnt++);
        else if (summon->GetEntry() == NPC_EXPEDITION_TRAPPER)
            summon->AI()->SetData(0, _trapperCnt++);
    }

    void SummonedCreatureDies(Creature* summon, Unit*) override
    {
        if (summon->GetEntry() == NPC_EXPEDITION_ENGINEER)
        {
            _engineersAlive--;
            if (_engineersAlive == 0)
            {
                EntryCheckPredicate pred(NPC_EXPEDITION_COMMANDER);
                summons.DoAction(ACTION_ENGINEER_DEAD, pred);
            }
        }
    }

    uint32 GetData(uint32 id) const override
    {
        return (id == DATA_QUICK_SHAVE_ID && _flyCount <= 1) ? 1 : 0;
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        if (why == EVADE_REASON_BOUNDARY && !events.IsInPhase(PHASE_PERMA_GROUND))
            return;
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);

        // Despawn dark rune minions summoned by spawner spells (not tracked by BossAI summons)
        for (uint32 entry : {NPC_DARK_RUNE_GUARDIAN, NPC_DARK_RUNE_WATCHER, NPC_DARK_RUNE_SENTINEL})
        {
            std::list<Creature*> minions;
            me->GetCreatureListWithEntryInGrid(minions, entry, 200.0f);
            for (Creature* c : minions)
                c->DespawnOrUnsummon();
        }

        summons.DespawnAll();
        _EnterEvadeMode();
    }

    void JustDied(Unit*) override
    {
        _JustDied();
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_BERSERK:
                    Talk(2);
                    DoCastSelf(SPELL_BERSERK, true);
                    break;
                case EVENT_FIREBALL_AIR:
                    if (Unit* t = SelectTarget(SelectTargetMethod::Random, 0, 200.0f, true))
                        DoCast(t, SPELL_FIREBALL);
                    events.Repeat(3s, 4s);
                    break;
                case EVENT_DEVOURING_FLAME:
                    if (Unit* t = SelectTarget(SelectTargetMethod::Random, 0, 200.0f, true))
                        DoCast(t, SPELL_DEVOURING_FLAME);
                    events.Repeat(_permaGround ? Seconds(10) : Seconds(6), _permaGround ? Seconds(12) : Seconds(12));
                    break;
                case EVENT_SUMMON_MINIONS:
                {
                    uint8 cnt = RAID_MODE<uint8>(2, urand(2, 4));
                    uint32 d = 5;
                    for (uint8 i = 0; i < cnt; ++i)
                    {
                        events.ScheduleEvent(EVENT_SUMMON_MINIONS_DELAYED, Milliseconds(d * 1000), 0, PHASE_AIR);
                        d += urand(2, 5);
                    }
                    events.Repeat(48s);
                    break;
                }
                case EVENT_SUMMON_MINIONS_DELAYED:
                    SummonMinions();
                    break;
                case EVENT_FLAME_BREATH:
                    me->RemoveAura(SPELL_STUN_SELF);
                    Talk(1);
                    if (Unit* v = me->GetVictim())
                        DoCast(v, SPELL_FLAME_BREATH);
                    events.ScheduleEvent(EVENT_WING_BUFFET, 2s, 0, PHASE_GROUND);
                    break;
                case EVENT_FLAME_BREATH_GROUNDED:
                    Talk(1);
                    if (Unit* v = me->GetVictim())
                        DoCast(v, SPELL_FLAME_BREATH);
                    events.Repeat(15s, 18s);
                    break;
                case EVENT_WING_BUFFET:
                    DoCastSelf(SPELL_WING_BUFFET);
                    events.ScheduleEvent(EVENT_FIREBOLT, 2s, 0, PHASE_GROUND);
                    events.ScheduleEvent(EVENT_RESUME_AIR, 4s, 0, PHASE_GROUND);
                    break;
                case EVENT_RESUME_AIR:
                {
                    me->SetDisableGravity(true);
                    events.SetPhase(PHASE_AIR);
                    me->SetReactState(REACT_PASSIVE);
                    Position p = me->GetPosition();
                    p.m_positionZ += 10.0f;
                    me->GetMotionMaster()->MoveTakeoff(POINT_RAZORSCALE_TAKEOFF, p);
                    EntryCheckPredicate pred(NPC_EXPEDITION_ENGINEER);
                    summons.DoAction(ACTION_START_FIGHT, pred);
                    break;
                }
                case EVENT_FIREBOLT:
                    DoCastSelf(SPELL_FIREBOLT);
                    break;
                case EVENT_FUSE_ARMOR:
                    if (Unit* v = me->GetVictim())
                        if (!v->HasAura(SPELL_FUSED_ARMOR))
                            DoCast(v, SPELL_FUSE_ARMOR);
                    events.Repeat(10s, 15s);
                    break;
                case EVENT_RESUME_CHASE:
                    me->SetCombatMovement(true);
                    if (Unit* v = me->GetVictim())
                        me->GetMotionMaster()->MoveChase(v);
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        if (events.IsInPhase(PHASE_PERMA_GROUND))
            DoMeleeAttackIfReady();
    }

private:
    uint8 _engineersAlive;
    uint8 _defenderCnt;
    uint8 _trapperCnt;
    uint8 _harpoonHits;
    uint32 _flyCount;
    bool _permaGround;
};

/* ==================== npc_expedition_commander ==================== */

struct npc_expedition_commander : public ScriptedAI
{
    npc_expedition_commander(Creature* c) : ScriptedAI(c), _instance(c->GetInstanceScript()),
        _is25(Is25ManRaid()), _building(false), _destroyCd(false), _stopCtrl(false), _started(false) { }

    void Reset() override
    {
        _events.Reset();
        _started = false;
        me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        BuildBrokenHarpoons();
    }

    void BuildBrokenHarpoons()
    {
        uint8 n = _is25 ? 4 : 2;
        for (uint8 i = 0; i < n; ++i)
            me->SummonGameObject(GO_RAZOR_BROKEN_HARPOON, BrokenHarpoonPos[i].GetPositionX(), BrokenHarpoonPos[i].GetPositionY(), BrokenHarpoonPos[i].GetPositionZ(), BrokenHarpoonPos[i].GetOrientation(), 0.0f, 0.0f, -0.8987932f, 0.4383728f, 0);
    }

    void DoAction(int32 action) override
    {
        if (_building && action != ACTION_ENGINEER_DEAD)
            return;

        switch (action)
        {
            case ACTION_START_FIGHT:
                _started = true;
                break;
            case ACTION_GROUND_PHASE:    Talk(1); break;
            case ACTION_ENGINEER_DEAD:   Talk(2); _events.Reset(); _building = false; break;
            case ACTION_BUILD_HARPOON_1: _building = true; _events.ScheduleEvent(EVENT_BUILD_HARPOON_1, 18s); break;
            case ACTION_BUILD_HARPOON_2: _building = true; _events.ScheduleEvent(EVENT_BUILD_HARPOON_2, 18s); break;
            case ACTION_BUILD_HARPOON_3: _building = true; _events.ScheduleEvent(EVENT_BUILD_HARPOON_3, 18s); break;
            case ACTION_BUILD_HARPOON_4: _building = true; _events.ScheduleEvent(EVENT_BUILD_HARPOON_4, 18s); break;
            case ACTION_DESTROY_HARPOONS:
                if (_destroyCd) return;
                _destroyCd = true;
                DestroyHarpoons();
                break;
            case ACTION_STOP_CONTROLLERS:
                if (_stopCtrl) return;
                _stopCtrl = true;
                StopControllers();
                break;
        }
    }

    void DestroyHarpoons()
    {
        for (ObjectGuid g : _harpoonGUIDs)
            if (GameObject* h = ObjectAccessor::GetGameObject(*me, g))
                h->RemoveFromWorld();
        _harpoonGUIDs.clear();
        BuildBrokenHarpoons();
        _events.ScheduleEvent(EVENT_DESTROY_HARPOONS, 10s);
    }

    void StopControllers()
    {
        std::list<Creature*> l;
        me->GetCreatureListWithEntryInGrid(l, NPC_RAZORSCALE_CONTROLLER, 100.0f);
        for (Creature* c : l)
            c->InterruptNonMeleeSpells(false);
        _stopCtrl = false;
    }

    void BuildHarpoon(uint8 idx)
    {
        if (GameObject* h = me->SummonGameObject(HarpoonEntry(idx), HarpoonPositions[idx].GetPositionX(), HarpoonPositions[idx].GetPositionY(), HarpoonPositions[idx].GetPositionZ(), HarpoonPositions[idx].GetOrientation(), 0.0f, 0.0f, -0.573576f, 0.8191524f, 0))
            _harpoonGUIDs.push_back(h->GetGUID());
    }

    uint32 HarpoonEntry(uint8 idx) const
    {
        if (_is25)
        {
            switch (idx)
            {
                case 0: return GO_RAZOR_HARPOON_3;
                case 1: return GO_RAZOR_HARPOON_1;
                case 2: return GO_RAZOR_HARPOON_2;
                case 3: return GO_RAZOR_HARPOON_4;
            }
        }
        switch (idx)
        {
            case 0: return GO_RAZOR_HARPOON_1;
            case 1: return GO_RAZOR_HARPOON_2;
        }
        return 0;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_started)
            return;
        _events.Update(diff);
        while (uint32 id = _events.ExecuteEvent())
        {
            switch (id)
            {
                case EVENT_BUILD_HARPOON_1: BuildHarpoon(0); _building = false; break;
                case EVENT_BUILD_HARPOON_2: BuildHarpoon(1); _building = false; break;
                case EVENT_BUILD_HARPOON_3: BuildHarpoon(2); _building = false; break;
                case EVENT_BUILD_HARPOON_4: BuildHarpoon(3); _building = false; break;
                case EVENT_DESTROY_HARPOONS: _destroyCd = false; break;
            }
        }
    }

    void sGossipSelect(Player* player, uint32 /*sender*/, uint32 /*action*/) override
    {
        CloseGossipMenuFor(player);

        InstanceScript* instance = me->GetInstanceScript();
        if (!instance)
            return;
        
        Creature* razorscale = instance->GetCreature(BOSS_RAZORSCALE);
        if (!razorscale || razorscale->IsInCombat())
            return;

        Talk(0);

        razorscale->SetImmuneToPC(false);
        razorscale->AI()->DoAction(ACTION_START_FIGHT);
        me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        return;
    }

private:
    InstanceScript* _instance;
    GuidVector _harpoonGUIDs;
    EventMap _events;
    bool _is25, _building, _destroyCd, _stopCtrl, _started;
};

/* ==================== npc_expedition_defender ==================== */

struct npc_expedition_defender : public ScriptedAI
{
    npc_expedition_defender(Creature* c) : ScriptedAI(c), _idx(0) { me->SetRegeneratingHealth(false); }

    void Reset() override { DoCastSelf(SPELL_THREAT); }

    bool CanAIAttack(Unit const* t) const override
    {
        return t && t->GetEntry() != NPC_RAZORSCALE && t->GetEntry() != NPC_RAZORSCALE_SPAWNER;
    }

    void SetData(uint32, uint32 v) override { _idx = v; }

    void DoAction(int32 a) override
    {
        if (a == ACTION_START_FIGHT)
            me->GetMotionMaster()->MovePoint(POINT_DEFENDER_ATTACK, DefenderPositions[_idx]);
    }

    void MovementInform(uint32 t, uint32 p) override
    {
        if (t == POINT_MOTION_TYPE && p == POINT_DEFENDER_ATTACK)
        {
            me->SetHomePosition(DefenderPositions[_idx]);
            me->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC);
        }
    }

private:
    uint8 _idx;
};

/* ==================== npc_expedition_trapper ==================== */

struct npc_expedition_trapper : public ScriptedAI
{
    npc_expedition_trapper(Creature* c) : ScriptedAI(c), _idx(0)
    {
        me->SetCombatMovement(false);
        me->SetReactState(REACT_PASSIVE);
    }

    void SetData(uint32, uint32 v) override { _idx = v; }

    void DoAction(int32 a) override
    {
        if (!me->IsAlive()) return;
        switch (a)
        {
            case ACTION_GROUND_PHASE:
                me->GetMotionMaster()->MovePoint(1, TrapperPositions[_idx]);
                break;
            case ACTION_START_FIGHT:
                me->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC);
                break;
            case ACTION_STOP_CONTROLLERS:
                me->InterruptNonMeleeSpells(false);
                scheduler.Schedule(2s, [this](TaskContext) { me->GetMotionMaster()->MoveTargetedHome(); });
                break;
        }
    }

    void MovementInform(uint32 t, uint32 p) override
    {
        if (t == POINT_MOTION_TYPE && p == 1)
            DoCastSelf(SPELL_SHACKLE);
    }

    void UpdateAI(uint32 diff) override { scheduler.Update(diff); }

private:
    uint8 _idx;
};

/* ==================== npc_expedition_engineer ==================== */

struct npc_expedition_engineer : public NullCreatureAI
{
    npc_expedition_engineer(Creature* c) : NullCreatureAI(c), _instance(c->GetInstanceScript()),
        _state(0), _timer(0), _harpoonGUID() { }

    void Reset() override { _state = 0; _timer = 0; _harpoonGUID.Clear(); me->SetReactState(REACT_PASSIVE); }

    void DoAction(int32 a) override
    {
        if (!me->IsAlive()) return;
        if (a == ACTION_START_FIGHT)
        {
            if (IsEast()) Talk(0);
            _state = 1; _timer = 0; _harpoonGUID.Clear();
            me->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC);
        }
    }

    bool IsEast() const { return me->GetHomePosition().GetPositionX() > 594.0f; }

    void UpdateAI(uint32 diff) override
    {
        if (_state == 0) return;
        if (_timer <= diff)
        {
            _timer = 3000;
            if (_harpoonGUID)
            {
                if (Creature* hfs = ObjectAccessor::GetCreature(*me, _harpoonGUID))
                {
                    if (me->GetExactDist2dSq(hfs) <= 25.0f)
                    {
                        if (me->GetUInt32Value(UNIT_NPC_EMOTESTATE) != EMOTE_STATE_WORK)
                            me->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_WORK);
                        if (std::fabs(me->GetOrientation() - me->GetAngle(hfs)) > M_PI / 4)
                            me->SetFacingToObject(hfs);
                        hfs->AI()->SetData(2, 0);
                        if (hfs->AI()->GetData(2))
                            _harpoonGUID.Clear();
                    }
                }
            }
            if (!_harpoonGUID)
            {
                Creature* rs = _instance->GetCreature(BOSS_RAZORSCALE);
                if (!rs || !rs->IsInCombat()) { _state = 0; me->GetMotionMaster()->MoveTargetedHome(); return; }
                std::list<Creature*> l;
                me->GetCreaturesWithEntryInRange(l, 300.0f, NPC_HARPOON_FIRE_STATE);
                l.sort([](Creature const* a, Creature const* b) { return a->GetPositionX() < b->GetPositionX(); });
                for (Creature* h : l)
                {
                    if (!h->AI()->GetData(2))
                    {
                        float ang = rand_norm() * M_PI;
                        me->GetMotionMaster()->MovePoint(0, h->GetPositionX() + 3.0f * cos(ang),
                            h->GetPositionY() + 3.0f * std::sin(ang), h->GetPositionZ());
                        _harpoonGUID = h->GetGUID();
                        return;
                    }
                }
                _state = 0;
                me->GetMotionMaster()->MoveTargetedHome();
            }
        }
        else _timer -= diff;
        if (_state == 0 && me->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_WORK)
            me->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_STAND);
    }

private:
    InstanceScript* _instance;
    uint8 _state;
    uint16 _timer;
    ObjectGuid _harpoonGUID;
};

/* ==================== npc_razorscale_spawner ==================== */

struct npc_razorscale_spawner : public ScriptedAI
{
    npc_razorscale_spawner(Creature* c) : ScriptedAI(c) { }

    void Reset() override
    {
        me->setActive(true);
        me->SetReactState(REACT_PASSIVE);
        scheduler
            .Schedule(1s, [this](TaskContext) { DoCastSelf(SPELL_SUMMON_MOLE_MACHINE); })
            .Schedule(6s, [this](TaskContext)
            {
                DoCastSelf(DwarfSummonSpells[urand(0, 2)]);
                // Vrykul: RNG-based, independent chance per spawner
                if (roll_chance_i(30))
                    DoCastSelf(SPELL_TRIGGER_SUMMON_IRON_VRYKUL);
            });
    }

    void UpdateAI(uint32 diff) override { scheduler.Update(diff); }
};

/* ==================== npc_razorscale_controller ==================== */

struct npc_razorscale_controller : public NullCreatureAI
{
    npc_razorscale_controller(Creature* c) : NullCreatureAI(c) { }
};

/* ==================== npc_razorscale_harpoon_fire_state ==================== */

struct npc_razorscale_harpoon_fire_state : public NullCreatureAI
{
    npc_razorscale_harpoon_fire_state(Creature* c) : NullCreatureAI(c), _instance(c->GetInstanceScript()), _pts(0) { }

    void Reset() override { _pts = 0; }

    void SpellHit(Unit*, SpellInfo const* s) override
    {
        if (s->Id == SPELL_FIREBOLT)
        {
            DoCastSelf(SPELL_HARPOON_FIRE_STATE);
            if (Creature* cmd = _instance->GetCreature(BOSS_RAZORSCALE))
                if (cmd->AI())
                    cmd->AI()->DoAction(ACTION_DESTROY_HARPOONS);
        }
    }

    void SetData(uint32 id, uint32) override
    {
        if (id == 1) { Reset(); return; }
        if (id == 2 && _pts < 25)
        {
            if (++_pts >= 25)
            {
                if (GameObject* bh = me->FindNearestGameObject(GO_RAZOR_BROKEN_HARPOON, 5.0f))
                    bh->SetPhaseMask(2, true);
                uint32 he = HarpoonEntry();
                if (he)
                {
                    if (GameObject* wh = me->SummonGameObject(he, me->GetPositionX(),
                        me->GetPositionY(), me->GetPositionZ(), 3 * M_PI / 2, 0, 0, 0, 0, 0))
                        me->RemoveGameObject(wh, false);
                }
                if (Creature* ctrl = me->FindNearestCreature(NPC_RAZORSCALE_CONTROLLER, 10.0f))
                    ctrl->AI()->Talk(0);
            }
        }
    }

    uint32 GetData(uint32 id) const override { return (id == 2 && _pts >= 25) ? 1 : 0; }

    uint32 HarpoonEntry() const
    {
        static uint32 const ents[] = { GO_RAZOR_HARPOON_1, GO_RAZOR_HARPOON_2, GO_RAZOR_HARPOON_3, GO_RAZOR_HARPOON_4 };
        for (uint32 e : ents)
            if (me->FindNearestGameObject(e, 5.0f) || me->FindNearestGameObject(GO_RAZOR_BROKEN_HARPOON, 5.0f))
                return e;
        if (me->GetPositionX() > 600) return GO_RAZOR_HARPOON_4;
        if (me->GetPositionX() > 585) return GO_RAZOR_HARPOON_2;
        if (me->GetPositionX() > 570) return GO_RAZOR_HARPOON_1;
        return GO_RAZOR_HARPOON_3;
    }

private:
    InstanceScript* _instance;
    uint8 _pts;
};

/* ==================== npc_razorscale_devouring_flame ==================== */

struct npc_razorscale_devouring_flame : public NullCreatureAI
{
    npc_razorscale_devouring_flame(Creature* c) : NullCreatureAI(c) { }

    void Reset() override { me->SetReactState(REACT_PASSIVE); DoCastSelf(SPELL_DEVOURING_FLAME_GROUND, true); }
    void EnterEvadeMode(EvadeReason) override { }
};

/* ==================== Dark Rune AIs ==================== */

struct npc_razorscale_dark_rune_watcher : public ScriptedAI
{
    npc_razorscale_dark_rune_watcher(Creature* c) : ScriptedAI(c) { }

    void Reset() override { _events.Reset(); me->SetReactState(REACT_AGGRESSIVE); DoZoneInCombat(); }
    bool CanAIAttack(Unit const* t) const override { return t && t->GetEntry() != NPC_RAZORSCALE && t->GetEntry() != NPC_RAZORSCALE_SPAWNER; }
    void JustEngagedWith(Unit*) override { _events.ScheduleEvent(EVENT_LIGHTNING_BOLT, 5s); _events.ScheduleEvent(EVENT_CHAIN_LIGHTNING, 34s); }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 80.0f, true))
                AttackStart(target);
            return;
        }
        _events.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING)) return;
        while (uint32 id = _events.ExecuteEvent())
        {
            switch (id)
            {
                case EVENT_LIGHTNING_BOLT: DoCastVictim(SPELL_LIGHTNING_BOLT); _events.Repeat(3s); break;
                case EVENT_CHAIN_LIGHTNING: DoCastVictim(SPELL_CHAIN_LIGHTNING); _events.Repeat(9s, 15s); break;
            }
            if (me->HasUnitState(UNIT_STATE_CASTING)) return;
        }
        DoMeleeAttackIfReady();
    }
private:
    EventMap _events;
};

struct npc_razorscale_dark_rune_guardian : public ScriptedAI
{
    npc_razorscale_dark_rune_guardian(Creature* c) : ScriptedAI(c), _killed(false) { }

    void Reset() override { _events.Reset(); me->SetReactState(REACT_AGGRESSIVE); DoZoneInCombat(); }
    bool CanAIAttack(Unit const* t) const override { return t && t->GetEntry() != NPC_RAZORSCALE && t->GetEntry() != NPC_RAZORSCALE_SPAWNER; }
    void JustEngagedWith(Unit*) override { _events.ScheduleEvent(EVENT_STORMSTRIKE, 23s); }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType, SpellSchoolMask) override
    {
        if (attacker && attacker->GetEntry() == NPC_RAZORSCALE && damage >= me->GetHealth())
            _killed = true;
    }

    uint32 GetData(uint32 id) const override { return (id == DATA_IRON_DWARF_ID && _killed) ? 1 : 0; }
    void SetData(uint32 id, uint32 v) override { if (id == DATA_IRON_DWARF_ID) _killed = (v != 0); }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 80.0f, true))
                AttackStart(target);
            return;
        }
        _events.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING)) return;
        while (uint32 id = _events.ExecuteEvent())
        {
            switch (id)
            {
                case EVENT_STORMSTRIKE: DoCastVictim(SPELL_STORMSTRIKE); _events.Repeat(13s, 25s); break;
            }
            if (me->HasUnitState(UNIT_STATE_CASTING)) return;
        }
        DoMeleeAttackIfReady();
    }
private:
    EventMap _events;
    bool _killed;
};

struct npc_razorscale_dark_rune_sentinel : public ScriptedAI
{
    npc_razorscale_dark_rune_sentinel(Creature* c) : ScriptedAI(c) { }

    void Reset() override { _events.Reset(); me->SetReactState(REACT_AGGRESSIVE); DoZoneInCombat(); }
    bool CanAIAttack(Unit const* t) const override { return t && t->GetEntry() != NPC_RAZORSCALE && t->GetEntry() != NPC_RAZORSCALE_SPAWNER; }
    void JustEngagedWith(Unit*) override
    {
        _events.ScheduleEvent(EVENT_HEROIC_STRIKE, 9s);
        _events.ScheduleEvent(EVENT_BATTLE_SHOUT, 15s);
        _events.ScheduleEvent(EVENT_WHIRLWIND, 17s);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 80.0f, true))
                AttackStart(target);
            return;
        }
        _events.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING)) return;
        while (uint32 id = _events.ExecuteEvent())
        {
            switch (id)
            {
                case EVENT_HEROIC_STRIKE: DoCastVictim(SPELL_HEROIC_STRIKE); _events.Repeat(5s, 9s); break;
                case EVENT_BATTLE_SHOUT: DoCastSelf(SPELL_BATTLE_SHOUT); _events.Repeat(25s); break;
                case EVENT_WHIRLWIND: DoCastSelf(SPELL_WHIRLWIND); _events.Repeat(10s, 13s); break;
            }
            if (me->HasUnitState(UNIT_STATE_CASTING)) return;
        }
        DoMeleeAttackIfReady();
    }
private:
    EventMap _events;
};

/* ==================== go_razorscale_harpoon ==================== */

class go_razorscale_harpoon : public GameObjectScript
{
public:
    go_razorscale_harpoon() : GameObjectScript("go_razorscale_harpoon") { }

    bool OnGossipHello(Player* /*player*/, GameObject* go) override
    {
        go->SetGameObjectFlag(GO_FLAG_NOT_SELECTABLE);
        if (Creature* ctrl = go->FindNearestCreature(NPC_RAZORSCALE_CONTROLLER, 10.0f))
        {
            if (ctrl->HasUnitState(UNIT_STATE_CASTING))
                return true;
            uint32 spell = 0;
            switch (go->GetEntry())
            {
                case GO_RAZOR_HARPOON_1: spell = SPELL_HARPOON_SHOT_1; break;
                case GO_RAZOR_HARPOON_2: spell = SPELL_HARPOON_SHOT_2; break;
                case GO_RAZOR_HARPOON_3: spell = SPELL_HARPOON_SHOT_3; break;
                case GO_RAZOR_HARPOON_4: spell = SPELL_HARPOON_SHOT_4; break;
            }
            if (spell)
                ctrl->CastSpell(ctrl, spell, true);
        }
        return true;
    }

    struct go_razorscale_harpoonAI : public GameObjectAI
    {
        go_razorscale_harpoonAI(GameObject* go) : GameObjectAI(go) { }

        void Reset() override
        {
            _scheduler.Schedule(1s, [this](TaskContext)
            {
                if (Creature* ctrl = me->FindNearestCreature(NPC_RAZORSCALE_CONTROLLER, 10.0f))
                    ctrl->AI()->Talk(0);
                if (GameObject* bh = me->FindNearestGameObject(GO_RAZOR_BROKEN_HARPOON, 5.0f))
                    bh->RemoveFromWorld();
            });
        }

        void UpdateAI(uint32 diff) override { _scheduler.Update(diff); }

    private:
        TaskScheduler _scheduler;
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return GetUlduarAI<go_razorscale_harpoonAI>(go);
    }
};

/* ==================== go_razorscale_mole_machine ==================== */

class go_razorscale_mole_machine : public GameObjectScript
{
public:
    go_razorscale_mole_machine() : GameObjectScript("go_razorscale_mole_machine") { }

    struct go_razorscale_mole_machineAI : public GameObjectAI
    {
        go_razorscale_mole_machineAI(GameObject* go) : GameObjectAI(go) { }

        void Reset() override
        {
            me->SetGameObjectFlag(GO_FLAG_NOT_SELECTABLE);
            _scheduler
                .Schedule(1s, [this](TaskContext) { me->UseDoorOrButton(); })
                .Schedule(10s, [this](TaskContext) { me->Delete(); });
        }

        void UpdateAI(uint32 diff) override { _scheduler.Update(diff); }

    private:
        TaskScheduler _scheduler;
    };

    GameObjectAI* GetAI(GameObject* go) const override { return GetUlduarAI<go_razorscale_mole_machineAI>(go); }
};

/* ==================== spell_razorscale_summon_iron_dwarves ==================== */

class spell_razorscale_summon_iron_dwarves : public SpellScript
{
    PrepareSpellScript(spell_razorscale_summon_iron_dwarves);

    bool Validate(SpellInfo const*) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_IRON_DWARF_GUARDIAN, SPELL_SUMMON_IRON_DWARF_WATCHER });
    }

    void HandleScript(SpellEffIndex)
    {
        Unit* c = GetCaster();
        switch (GetSpellInfo()->Id)
        {
            case SPELL_TRIGGER_SUMMON_IRON_DWARVES:
                c->CastSpell(c, SPELL_SUMMON_IRON_DWARF_GUARDIAN, true);
                c->CastSpell(c, SPELL_SUMMON_IRON_DWARF_GUARDIAN, true);
                c->CastSpell(c, SPELL_SUMMON_IRON_DWARF_WATCHER, true);
                break;
            case SPELL_TRIGGER_SUMMON_IRON_DWARVES_2:
            case SPELL_TRIGGER_SUMMON_IRON_DWARVES_3:
                c->CastSpell(c, SPELL_SUMMON_IRON_DWARF_GUARDIAN, true);
                c->CastSpell(c, SPELL_SUMMON_IRON_DWARF_WATCHER, true);
                c->CastSpell(c, SPELL_SUMMON_IRON_DWARF_WATCHER, true);
                break;
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_razorscale_summon_iron_dwarves::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/* ==================== spell_razorscale_fuse_armor ==================== */

class spell_razorscale_fuse_armor : public AuraScript
{
    PrepareAuraScript(spell_razorscale_fuse_armor);

    bool Validate(SpellInfo const*) override { return ValidateSpellInfo({ SPELL_FUSED_ARMOR }); }

    void HandleFused(AuraEffect const*)
    {
        if (GetStackAmount() != GetSpellInfo()->StackAmount) return;
        GetTarget()->CastSpell(GetTarget(), SPELL_FUSED_ARMOR, true);
        Remove();
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_razorscale_fuse_armor::HandleFused, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/* ==================== spell_razorscale_firebolt ==================== */

class spell_razorscale_firebolt : public SpellScript
{
    PrepareSpellScript(spell_razorscale_firebolt);

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* o) { return o->GetEntry() != NPC_HARPOON_FIRE_STATE; });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_razorscale_firebolt::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

/* ==================== Achievements ==================== */

class achievement_quick_shave : public AchievementCriteriaScript
{
public:
    achievement_quick_shave() : AchievementCriteriaScript("achievement_quick_shave") { }

    bool OnCheck(Player*, Unit* tgt, uint32) override
    {
        return tgt && tgt->IsCreature() && tgt->ToCreature()->AI()->GetData(DATA_QUICK_SHAVE_ID);
    }
};

class achievement_iron_dwarf_medium_rare : public AchievementCriteriaScript
{
public:
    achievement_iron_dwarf_medium_rare() : AchievementCriteriaScript("achievement_iron_dwarf_medium_rare") { }

    bool OnCheck(Player*, Unit* tgt, uint32) override
    {
        return tgt && tgt->GetAI() && tgt->GetAI()->GetData(DATA_IRON_DWARF_ID);
    }
};

/* ==================== Registration ==================== */

void AddSC_boss_razorscale()
{
    RegisterUlduarCreatureAI(boss_razorscale);
    RegisterUlduarCreatureAI(npc_expedition_commander);
    RegisterUlduarCreatureAI(npc_expedition_defender);
    RegisterUlduarCreatureAI(npc_expedition_trapper);
    RegisterUlduarCreatureAI(npc_expedition_engineer);
    RegisterUlduarCreatureAI(npc_razorscale_spawner);
    RegisterUlduarCreatureAI(npc_razorscale_controller);
    RegisterUlduarCreatureAI(npc_razorscale_harpoon_fire_state);
    RegisterUlduarCreatureAI(npc_razorscale_devouring_flame);
    RegisterUlduarCreatureAI(npc_razorscale_dark_rune_watcher);
    RegisterUlduarCreatureAI(npc_razorscale_dark_rune_guardian);
    RegisterUlduarCreatureAI(npc_razorscale_dark_rune_sentinel);
    new go_razorscale_harpoon();
    new go_razorscale_mole_machine();
    RegisterSpellScript(spell_razorscale_summon_iron_dwarves);
    RegisterSpellAndAuraScriptPair(spell_razorscale_fuse_armor, spell_razorscale_fuse_armor);
    RegisterSpellScript(spell_razorscale_firebolt);
    new achievement_quick_shave();
    new achievement_iron_dwarf_medium_rare();
}
