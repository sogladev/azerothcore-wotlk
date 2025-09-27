/*
 * TODO
 */

#ifndef Trainer_h__
#define Trainer_h__

#include "Common.h"
#include <array>
#include <vector>

class Creature;
class ObjectMgr;
class Player;

namespace Trainer
{
    enum class Type : uint32
    {
        Class = 0,
        Mount = 1,
        Tradeskill = 2,
        Pet = 3
    };

    enum class SpellState : uint8
    {
        Available = 0,
        Unavailable = 1,
        Known = 2
    };

    enum class FailReason : uint32
    {
        Unavailable = 0,
        NotEnoughMoney = 1,
        NotEnoughSkill = 2
    };

    struct AC_GAME_API Spell
    {
        uint32 SpellId = 0;
        uint32 MoneyCost = 0;
        uint32 ReqSkillLine = 0;
        uint32 ReqSkillRank = 0;
        std::array<uint32, 3> ReqAbility = { };
        uint8 ReqLevel = 0;

        [[nodiscard]] bool IsCastable() const;
    };

    class AC_GAME_API Trainer
    {
        public:
            Trainer(uint32 trainerId, Type type, uint32 requirement, std::string greeting, std::vector<Spell> spells);

            [[nodiscard]] Spell const* GetSpell(uint32 spellId) const;
            [[nodiscard]] std::vector<Spell> const& GetSpells() const { return _spells; }
            void SendSpells(Creature* npc, Player* player, LocaleConstant locale) const;
            bool CanTeachSpell(Player const* player, Spell const* trainerSpell);
            void TeachSpell(Creature* npc, Player* player, uint32 spellId);

            [[nodiscard]] Type GetTrainerType() const { return _type; }
            [[nodiscard]] uint32 GetTrainerRequirement() const { return _requirement; }
            bool IsTrainerValidForPlayer(Player const* player) const;

            private:
            SpellState GetSpellState(Player const* player, Spell const* trainerSpell) const;
            void SendTeachFailure(Creature const* npc, Player const* player, uint32 spellId, FailReason reason) const;
            void SendTeachSucceeded(Creature const* npc, Player const* player, uint32 spellId) const;
            [[nodiscard]] std::string const& GetGreeting(LocaleConstant locale) const;

            friend ObjectMgr;
            void AddGreetingLocale(LocaleConstant locale, std::string greeting);

            uint32 _trainerId;
            Type _type;
            uint32 _requirement;
            std::vector<Spell> _spells;
            std::array<std::string, TOTAL_LOCALES> _greeting;
    };
}

#endif // Trainer_h__
