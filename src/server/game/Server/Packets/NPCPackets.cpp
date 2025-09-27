/*
 * TODO
 */

#include "NPCPackets.h"

void WorldPackets::NPC::Hello::Read()
{
    _worldPacket >> Unit;
}

WorldPacket const* WorldPackets::NPC::TrainerList::Write()
{
    _worldPacket << TrainerGUID;
    _worldPacket << int32(TrainerType);

    _worldPacket << int32(Spells.size());
    for (TrainerListSpell const& spell : Spells)
    {
        _worldPacket << int32(spell.SpellID);
        _worldPacket << uint8(spell.Usable);
        _worldPacket << int32(spell.MoneyCost);
        _worldPacket.append(spell.PointCost.data(), spell.PointCost.size());
        _worldPacket << uint8(spell.ReqLevel);
        _worldPacket << int32(spell.ReqSkillLine);
        _worldPacket << int32(spell.ReqSkillRank);
        _worldPacket.append(spell.ReqAbility.data(), spell.ReqAbility.size());
    }

    _worldPacket << Greeting;

    return &_worldPacket;
}

void WorldPackets::NPC::TrainerBuySpell::Read()
{
    _worldPacket >> TrainerGUID;
    _worldPacket >> SpellID;
}

WorldPacket const* WorldPackets::NPC::TrainerBuyFailed::Write()
{
    _worldPacket << TrainerGUID;
    _worldPacket << int32(SpellID);
    _worldPacket << int32(TrainerFailedReason);

    return &_worldPacket;
}

WorldPacket const* WorldPackets::NPC::TrainerBuySucceeded::Write()
{
    _worldPacket << TrainerGUID;
    _worldPacket << int32(SpellID);

    return &_worldPacket;
}
