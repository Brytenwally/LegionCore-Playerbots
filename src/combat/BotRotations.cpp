/*
 * mod-playerbots: per-spec rotations. See BotRotations.h.
 */

#include "BotRotations.h"
#include "BotCombat.h"
#include "DB2Stores.h"
#include "Log.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <set>

namespace BotRotations
{
    namespace
    {
        // AshamaneCore-Legion735 g_BotSpecRotations, 36 specs (see the header). Steps are tried top to
        // bottom; the first whose spell is known, ready and whose condition holds is cast.
        std::vector<SpecRotation> const Rotations =
        {
            // Warrior Protection
            {
                73, SPELLFAMILY_WARRIOR, CombatRole::Tank,
                {
                    { "Shield Block",  Cond::AuraMissingOnSelf, 0.0f, 0, "Shield Block" },
                    { "Ignore Pain",   Cond::ResourceAtLeast,   60.0f, POWER_RAGE },
                    { "Revenge",       Cond::Always },
                    { "Shield Slam",   Cond::Always },
                    { "Devastate",     Cond::Always }
                },
                "Pummel", nullptr, "Taunt"
            },
            // Warrior Fury
            {
                72, SPELLFAMILY_WARRIOR, CombatRole::Melee,
                {
                    { "Bloodthirst",   Cond::Always },
                    { "Raging Blow",   Cond::Always },
                    { "Rampage",       Cond::ResourceAtLeast,     80.0f, POWER_RAGE },
                    { "Execute",       Cond::TargetHealthPctBelow, 20.0f },
                    { "Whirlwind",     Cond::Always }
                },
                "Pummel"
            },
            // Mage Frost
            {
                64, SPELLFAMILY_MAGE, CombatRole::Ranged,
                {
                    { "Flurry",        Cond::AuraPresentOnSelf, 0.0f, 0, "Brain Freeze" },
                    { "Ice Lance",     Cond::AuraPresentOnSelf, 0.0f, 0, "Fingers of Frost" },
                    { "Frozen Orb",    Cond::Always },
                    { "Ebonbolt",      Cond::Always },
                    { "Frostbolt",     Cond::Always }
                },
                "Counterspell"
            },
            // Shaman Restoration
            {
                264, SPELLFAMILY_SHAMAN, CombatRole::Healer,
                {
                    { "Healing Surge", Cond::TargetHealthPctBelow, 35.0f },
                    { "Riptide",       Cond::AuraMissingOnTarget, 0.0f, 0, "Riptide" },
                    { "Chain Heal",    Cond::TargetHealthPctBelow, 80.0f },
                    { "Healing Wave",  Cond::Always }
                },
                nullptr, "Purify Spirit"
            },
            // Warrior Arms
            {
                71, SPELLFAMILY_WARRIOR, CombatRole::Melee,
                {
                    { "Colossus Smash", Cond::Always },
                    { "Mortal Strike",  Cond::Always },
                    { "Execute",        Cond::TargetHealthPctBelow, 20.0f },
                    { "Overpower",      Cond::Always },
                    { "Slam",           Cond::Always }
                },
                "Pummel"
            },
            // Paladin Protection
            {
                66, SPELLFAMILY_PALADIN, CombatRole::Tank,
                {
                    { "Shield of the Righteous", Cond::AuraMissingOnSelf, 0.0f, 0, "Shield of the Righteous" },
                    { "Judgment",                Cond::Always },
                    { "Avenger's Shield",        Cond::Always },
                    { "Consecration",            Cond::AuraMissingOnSelf, 0.0f, 0, "Consecration" },
                    { "Hammer of the Righteous", Cond::Always }
                },
                "Rebuke", nullptr, "Hand of Reckoning"
            },
            // Paladin Retribution
            {
                70, SPELLFAMILY_PALADIN, CombatRole::Melee,
                {
                    { "Judgment",         Cond::Always },
                    { "Templar's Verdict", Cond::ResourceAtLeast, 3.0f, POWER_HOLY_POWER },
                    { "Blade of Justice", Cond::Always },
                    { "Crusader Strike",  Cond::Always }
                },
                "Rebuke"
            },
            // Paladin Holy: LegionBotAI's CastHealAbility / MaintainBotBuff (plan step 4.3); beacons on
            // the tank (Beacon of Faith, a talent, on the owner)
            {
                65, SPELLFAMILY_PALADIN, CombatRole::Healer,
                {
                    { "Lay on Hands",     Cond::TargetHealthPctBelow, 25.0f },
                    { "Beacon of Light",  Cond::AuraMissingOnTarget, 0.0f, 0, "Beacon of Light", StepTarget::Tank },
                    { "Beacon of Faith",  Cond::AuraMissingOnTarget, 0.0f, 0, "Beacon of Faith", StepTarget::Owner },
                    { "Light of Dawn",    Cond::InjuredNearSelf, 3.0f, 0, nullptr, StepTarget::RoleDefault, 15.0f },
                    { "Holy Shock",       Cond::TargetHealthPctBelow, 75.0f },
                    { "Flash of Light",   Cond::TargetHealthPctBelow, 55.0f },
                    { "Bestow Faith",     Cond::TargetHealthPctBelow, 85.0f },
                    { "Holy Light",       Cond::Always },
                    { "Flash of Light",   Cond::Always }        // low levels, before Holy Light is learned
                },
                nullptr, "Cleanse"
            },
            // Death Knight Blood
            {
                250, SPELLFAMILY_DEATHKNIGHT, CombatRole::Tank,
                {
                    // LegionBotAI's Blood defensives (plan step 4.4) before AshamaneCore's list
                    { "Vampiric Blood",      Cond::SelfHealthPctBelow, 35.0f, 0, nullptr, StepTarget::Self },
                    { "Icebound Fortitude",  Cond::SelfHealthPctBelow, 30.0f, 0, nullptr, StepTarget::Self },
                    { "Dancing Rune Weapon", Cond::SelfHealthPctBelow, 50.0f },
                    { "Death Strike",        Cond::SelfHealthPctBelow, 70.0f },
                    { "Marrowrend",          Cond::AuraStacksBelowOnSelf, 5.0f, 0, "Bone Shield" },
                    { "Death Strike",        Cond::ResourceAtLeast, 45.0f, POWER_RUNIC_POWER },
                    { "Heart Strike",        Cond::Always }
                },
                "Mind Freeze", nullptr, "Dark Command", "Death Grip"
            },
            // Death Knight Frost
            {
                251, SPELLFAMILY_DEATHKNIGHT, CombatRole::Melee,
                {
                    { "Obliterate",   Cond::AuraPresentOnSelf, 0.0f, 0, "Killing Machine" },
                    { "Frost Strike", Cond::ResourceAtLeast, 60.0f, POWER_RUNIC_POWER },
                    { "Howling Blast", Cond::Always },
                    { "Remorseless Winter", Cond::Always }
                },
                "Mind Freeze"
            },
            // Death Knight Unholy
            {
                252, SPELLFAMILY_DEATHKNIGHT, CombatRole::Melee,
                {
                    { "Festering Strike", Cond::Always },
                    { "Scourge Strike",   Cond::Always },
                    { "Death Coil",       Cond::ResourceAtLeast, 60.0f, POWER_RUNIC_POWER },
                    { "Outbreak",         Cond::AuraMissingOnTarget, 0.0f, 0, "Virulent Plague" }
                },
                "Mind Freeze"
            },
            // Rogue Assassination
            {
                259, SPELLFAMILY_ROGUE, CombatRole::Melee,
                {
                    { "Garrote",  Cond::AuraMissingOnTarget, 0.0f, 0, "Garrote" },
                    { "Rupture",  Cond::AuraMissingOnTarget, 0.0f, 0, "Rupture" },
                    { "Envenom",  Cond::ResourceAtLeast, 4.0f, POWER_COMBO_POINTS },
                    { "Mutilate", Cond::Always }
                },
                "Kick"
            },
            // Rogue Outlaw
            {
                260, SPELLFAMILY_ROGUE, CombatRole::Melee,
                {
                    { "Roll the Bones",  Cond::AuraMissingOnSelf, 0.0f, 0, "Roll the Bones" },
                    { "Between the Eyes", Cond::ResourceAtLeast, 5.0f, POWER_COMBO_POINTS },
                    { "Saber Slash",     Cond::Always }
                },
                "Kick"
            },
            // Rogue Subtlety
            {
                261, SPELLFAMILY_ROGUE, CombatRole::Melee,
                {
                    { "Symbols of Death", Cond::AuraMissingOnSelf, 0.0f, 0, "Symbols of Death" },
                    { "Nightblade",       Cond::AuraMissingOnTarget, 0.0f, 0, "Nightblade" },
                    { "Eviscerate",       Cond::ResourceAtLeast, 5.0f, POWER_COMBO_POINTS },
                    { "Backstab",         Cond::Always }
                },
                "Kick"
            },
            // Demon Hunter Havoc
            {
                577, SPELLFAMILY_DEMON_HUNTER, CombatRole::Melee,
                {
                    { "Chaos Strike", Cond::Always },
                    { "Blade Dance",  Cond::Always },
                    { "Eye Beam",     Cond::Always },
                    { "Demon's Bite", Cond::Always }
                },
                "Disrupt"
            },
            // Demon Hunter Vengeance
            {
                581, SPELLFAMILY_DEMON_HUNTER, CombatRole::Tank,
                {
                    { "Immolation Aura", Cond::Always },
                    { "Demon Spikes",    Cond::AuraMissingOnSelf, 0.0f, 0, "Demon Spikes" },
                    { "Sigil of Flame",  Cond::Always },
                    { "Soul Cleave",     Cond::Always }
                },
                "Disrupt", nullptr, "Torment"
            },
            // Mage Arcane
            {
                62, SPELLFAMILY_MAGE, CombatRole::Ranged,
                {
                    { "Arcane Missiles", Cond::AuraPresentOnSelf, 0.0f, 0, "Clearcasting" },
                    { "Arcane Barrage",  Cond::ResourceAtLeast, 4.0f, POWER_ARCANE_CHARGES },
                    { "Arcane Blast",    Cond::Always }
                },
                "Counterspell"
            },
            // Mage Fire
            {
                63, SPELLFAMILY_MAGE, CombatRole::Ranged,
                {
                    { "Pyroblast",       Cond::AuraPresentOnSelf, 0.0f, 0, "Hot Streak" },
                    { "Fire Blast",      Cond::AuraPresentOnSelf, 0.0f, 0, "Heating Up" },
                    { "Phoenix's Flames", Cond::Always },
                    { "Fireball",        Cond::Always }
                },
                "Counterspell"
            },
            // Warlock Affliction
            {
                265, SPELLFAMILY_WARLOCK, CombatRole::Ranged,
                {
                    { "Agony",               Cond::AuraMissingOnTarget, 0.0f, 0, "Agony" },
                    { "Corruption",          Cond::AuraMissingOnTarget, 0.0f, 0, "Corruption" },
                    { "Unstable Affliction", Cond::AuraMissingOnTarget, 0.0f, 0, "Unstable Affliction" },
                    { "Drain Soul",          Cond::Always }
                }
            },
            // Warlock Destruction
            {
                267, SPELLFAMILY_WARLOCK, CombatRole::Ranged,
                {
                    { "Immolate",    Cond::AuraMissingOnTarget, 0.0f, 0, "Immolate" },
                    { "Conflagrate", Cond::Always },
                    { "Chaos Bolt",  Cond::ResourceAtLeast, 2.0f, POWER_SOUL_SHARDS },
                    { "Incinerate",  Cond::Always }
                }
            },
            // Priest Shadow
            {
                258, SPELLFAMILY_PRIEST, CombatRole::Ranged,
                {
                    { "Void Eruption",     Cond::ResourceAtLeast, 90.0f, POWER_INSANITY },
                    { "Void Bolt",         Cond::AuraPresentOnSelf, 0.0f, 0, "Voidform" },
                    { "Shadow Word: Pain", Cond::AuraMissingOnTarget, 0.0f, 0, "Shadow Word: Pain" },
                    { "Vampiric Touch",    Cond::AuraMissingOnTarget, 0.0f, 0, "Vampiric Touch" },
                    { "Mind Blast",        Cond::Always },
                    { "Mind Flay",         Cond::Always }
                }
            },
            // Priest Holy: LegionBotAI's CastHealAbility / MaintainBotBuff (plan step 4.3) instead of
            // AshamaneCore's three steps; Purify is Holy's dispel on 7.3.5 (Dispel Magic is offensive)
            {
                257, SPELLFAMILY_PRIEST, CombatRole::Healer,
                {
                    { "Guardian Spirit",      Cond::TargetHealthPctBelow, 25.0f },
                    { "Prayer of Mending",    Cond::AuraMissingOnTarget, 0.0f, 0, "Prayer of Mending", StepTarget::Tank },
                    { "Prayer of Healing",    Cond::InjuredNearTarget, 3.0f, 0, nullptr, StepTarget::RoleDefault, 15.0f },
                    { "Holy Word: Serenity",  Cond::TargetHealthPctBelow, 75.0f },
                    { "Flash Heal",           Cond::TargetHealthPctBelow, 55.0f },
                    { "Renew",                Cond::AuraMissingOnTarget, 0.0f, 0, "Renew" },
                    { "Heal",                 Cond::Always },
                    { "Flash Heal",           Cond::Always }        // low levels, before Heal is learned
                },
                nullptr, "Purify"
            },
            // Shaman Elemental
            {
                262, SPELLFAMILY_SHAMAN, CombatRole::Ranged,
                {
                    { "Lava Burst",    Cond::AuraPresentOnSelf, 0.0f, 0, "Lava Surge" },
                    { "Earth Shock",   Cond::ResourceAtLeast, 60.0f, POWER_MAELSTROM },
                    { "Flame Shock",   Cond::AuraMissingOnTarget, 0.0f, 0, "Flame Shock" },
                    { "Lightning Bolt", Cond::Always }
                },
                "Wind Shear"
            },
            // Druid Balance
            {
                102, SPELLFAMILY_DRUID, CombatRole::Ranged,
                {
                    { "Starsurge", Cond::ResourceAtLeast, 50.0f, POWER_LUNAR_POWER },
                    { "Moonfire",  Cond::AuraMissingOnTarget, 0.0f, 0, "Moonfire" },
                    { "Sunfire",   Cond::AuraMissingOnTarget, 0.0f, 0, "Sunfire" },
                    { "Wrath",     Cond::Always }
                }
            },
            // Druid Restoration
            {
                105, SPELLFAMILY_DRUID, CombatRole::Healer,
                {
                    { "Rejuvenation", Cond::AuraMissingOnTarget, 0.0f, 0, "Rejuvenation" },
                    { "Regrowth",     Cond::TargetHealthPctBelow, 50.0f },
                    { "Wild Growth",  Cond::TargetHealthPctBelow, 80.0f }
                },
                nullptr, "Remove Corruption"
            },
            // Monk Mistweaver
            {
                270, SPELLFAMILY_MONK, CombatRole::Healer,
                {
                    { "Renewing Mist",  Cond::AuraMissingOnTarget, 0.0f, 0, "Renewing Mist" },
                    { "Enveloping Mist", Cond::TargetHealthPctBelow, 50.0f },
                    { "Vivify",         Cond::Always }
                },
                nullptr, "Detox"
            },
            // Hunter Beast Mastery
            {
                253, SPELLFAMILY_HUNTER, CombatRole::Ranged,
                {
                    { "Kill Command", Cond::Always },
                    { "Bestial Wrath", Cond::Always },
                    { "Cobra Shot",   Cond::Always }
                },
                "Counter Shot"
            },
            // Hunter Marksmanship
            {
                254, SPELLFAMILY_HUNTER, CombatRole::Ranged,
                {
                    { "Aimed Shot",  Cond::Always },
                    { "Arcane Shot", Cond::Always }
                },
                "Counter Shot"
            },
            // Hunter Survival
            {
                255, SPELLFAMILY_HUNTER, CombatRole::Melee,
                {
                    { "Wildfire Bomb",  Cond::Always },
                    { "Serpent Sting",  Cond::AuraMissingOnTarget, 0.0f, 0, "Serpent Sting" },
                    { "Raptor Strike",  Cond::Always }
                },
                "Counter Shot"
            },
            // Priest Discipline
            {
                256, SPELLFAMILY_PRIEST, CombatRole::Healer,
                {
                    { "Power Word: Shield", Cond::AuraMissingOnTarget, 0.0f, 0, "Atonement",
                        StepTarget::HealTarget },
                    { "Shadow Mend",        Cond::TargetHealthPctBelow, 30.0f, 0, nullptr,
                        StepTarget::HealTarget },
                    { "Smite",              Cond::Always, 0.0f, 0, nullptr,
                        StepTarget::Enemy }
                },
                nullptr, "Dispel Magic"
            },
            // Monk Brewmaster
            {
                268, SPELLFAMILY_MONK, CombatRole::Tank,
                {
                    { "Purifying Brew", Cond::AuraPresentOnSelf, 0.0f, 0, "Heavy Stagger" },
                    { "Purifying Brew", Cond::AuraPresentOnSelf, 0.0f, 0, "Moderate Stagger" },
                    { "Ironskin Brew",  Cond::AuraMissingOnSelf, 0.0f, 0, "Ironskin Brew" },
                    { "Keg Smash",      Cond::Always },
                    { "Blackout Strike", Cond::Always },
                    { "Tiger Palm",     Cond::Always }
                },
                "Spear Hand Strike", nullptr, "Provoke"
            },
            // Monk Windwalker
            {
                269, SPELLFAMILY_MONK, CombatRole::Melee,
                {
                    { "Fists of Fury",        Cond::Always },
                    { "Rising Sun Kick",      Cond::Always },
                    { "Whirling Dragon Punch", Cond::Always },
                    { "Tiger Palm",           Cond::Always }
                },
                "Spear Hand Strike"
            },
            // Warlock Demonology
            {
                266, SPELLFAMILY_WARLOCK, CombatRole::Ranged,
                {
                    { "Doom",               Cond::AuraMissingOnTarget, 0.0f, 0, "Doom" },
                    { "Call Dreadstalkers", Cond::Always },
                    { "Hand of Gul'dan",    Cond::ResourceAtLeast, 4.0f, POWER_SOUL_SHARDS },
                    { "Demonbolt",          Cond::AuraPresentOnSelf, 0.0f, 0, "Demonic Core" },
                    { "Shadow Bolt",        Cond::Always }
                }
            },
            // Shaman Enhancement
            {
                263, SPELLFAMILY_SHAMAN, CombatRole::Melee,
                {
                    { "Feral Spirit", Cond::Always },
                    { "Stormstrike",  Cond::Always },
                    { "Lava Lash",    Cond::Always },
                    { "Boulderfist",  Cond::Always }
                },
                "Wind Shear"
            },
            // Druid Feral
            {
                103, SPELLFAMILY_DRUID, CombatRole::Melee,
                {
                    { "Tiger's Fury",   Cond::Always },
                    { "Rake",           Cond::AuraMissingOnTarget, 0.0f, 0, "Rake" },
                    { "Rip",            Cond::AuraMissingOnTarget, 0.0f, 0, "Rip" },
                    { "Ferocious Bite", Cond::ResourceAtLeast, 5.0f, POWER_COMBO_POINTS },
                    { "Shred",          Cond::Always }
                },
                "Skull Bash"
            },
            // Druid Guardian
            {
                104, SPELLFAMILY_DRUID, CombatRole::Tank,
                {
                    { "Ironfur", Cond::AuraMissingOnSelf, 0.0f, 0, "Ironfur" },
                    { "Mangle",  Cond::Always },
                    { "Thrash",  Cond::AuraMissingOnTarget, 0.0f, 0, "Thrash" },
                    { "Moonfire", Cond::AuraMissingOnTarget, 0.0f, 0, "Moonfire" },
                    { "Maul",    Cond::Always }
                },
                "Skull Bash", nullptr, "Growl"
            }
        };

        // Defensive cooldowns per class/spec (7.3.5 spells; a name the server lacks is logged at startup)
        std::vector<Defensive> const Defensives =
        {
            { CLASS_WARRIOR, 71, "Die by the Sword", 35.0f, false },
            { CLASS_WARRIOR, 72, "Enraged Regeneration", 40.0f, false },
            { CLASS_WARRIOR, 73, "Shield Wall", 30.0f, false },
            { CLASS_WARRIOR, 73, "Last Stand", 35.0f, false },
            { CLASS_PALADIN, 65, "Divine Shield", 20.0f, false },
            { CLASS_PALADIN, 70, "Divine Shield", 20.0f, false },
            { CLASS_PALADIN, 66, "Lay on Hands", 15.0f, false },
            { CLASS_PALADIN, 70, "Lay on Hands", 15.0f, false },
            { CLASS_PALADIN, 66, "Ardent Defender", 30.0f, false },
            { CLASS_PALADIN, 66, "Guardian of Ancient Kings", 35.0f, false },
            { CLASS_PALADIN, 65, "Divine Protection", 50.0f, false },
            { CLASS_PALADIN, 70, "Shield of Vengeance", 50.0f, false },
            { CLASS_HUNTER, 0, "Aspect of the Turtle", 20.0f, false },
            { CLASS_HUNTER, 0, "Exhilaration", 40.0f, false },
            { CLASS_ROGUE, 0, "Cloak of Shadows", 25.0f, false },
            { CLASS_ROGUE, 259, "Evasion", 35.0f, false },
            { CLASS_ROGUE, 261, "Evasion", 35.0f, false },
            { CLASS_ROGUE, 260, "Riposte", 35.0f, false },
            { CLASS_ROGUE, 0, "Crimson Vial", 50.0f, false },
            { CLASS_PRIEST, 258, "Dispersion", 25.0f, false },
            { CLASS_PRIEST, 0, "Desperate Prayer", 35.0f, false },
            { CLASS_DEATH_KNIGHT, 251, "Icebound Fortitude", 30.0f, false },   // Blood has its own in its rotation
            { CLASS_DEATH_KNIGHT, 252, "Icebound Fortitude", 30.0f, false },
            { CLASS_SHAMAN, 0, "Astral Shift", 35.0f, false },
            { CLASS_MAGE, 0, "Ice Block", 15.0f, false },
            { CLASS_MAGE, 64, "Ice Barrier", 80.0f, false },
            { CLASS_MAGE, 63, "Blazing Barrier", 80.0f, false },
            { CLASS_MAGE, 62, "Prismatic Barrier", 80.0f, false },
            { CLASS_WARLOCK, 0, "Unending Resolve", 35.0f, false },
            { CLASS_WARLOCK, 0, "Drain Life", 40.0f, true },
            { CLASS_MONK, 268, "Fortifying Brew", 35.0f, false },
            { CLASS_MONK, 269, "Fortifying Brew", 35.0f, false },
            { CLASS_MONK, 269, "Touch of Karma", 50.0f, true },
            { CLASS_DRUID, 0, "Barkskin", 50.0f, false },
            { CLASS_DRUID, 103, "Survival Instincts", 30.0f, false },
            { CLASS_DRUID, 104, "Survival Instincts", 30.0f, false },
            { CLASS_DRUID, 104, "Frenzied Regeneration", 60.0f, false },
            { CLASS_DEMON_HUNTER, 577, "Blur", 40.0f, false },
            { CLASS_DEMON_HUNTER, 581, "Metamorphosis", 30.0f, false },
            { CLASS_DEMON_HUNTER, 581, "Fiery Brand", 40.0f, true },
        };

        std::once_flag initOnce;
        std::map<std::string, std::vector<uint32>> spellsByName;    // lower-case name -> ids, read-only after Initialize

        std::string Lower(char const* text)
        {
            std::string lower(text ? text : "");
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return lower;
        }

        std::vector<uint32> const& SpellsNamed(char const* name)
        {
            static std::vector<uint32> const none;
            auto itr = spellsByName.find(Lower(name));
            return itr != spellsByName.end() ? itr->second : none;
        }

        void BuildIndex()
        {
            std::set<std::string> wanted;
            for (SpecRotation const& rotation : Rotations)
            {
                for (RotationStep const& step : rotation.steps)
                {
                    wanted.insert(Lower(step.spell));
                    if (step.auxSpell)
                        wanted.insert(Lower(step.auxSpell));
                }
                if (rotation.interrupt)
                    wanted.insert(Lower(rotation.interrupt));
                if (rotation.dispel)
                    wanted.insert(Lower(rotation.dispel));
                if (rotation.taunt)
                    wanted.insert(Lower(rotation.taunt));
                for (Defensive const& defensive : Defensives)
                    wanted.insert(Lower(defensive.spell));
                if (rotation.tauntAlt)
                    wanted.insert(Lower(rotation.tauntAlt));
            }

            for (std::string const& name : wanted)
                spellsByName[name];

            uint32 const size = sSpellMgr->GetSpellInfoStoreSize();
            for (uint32 id = 0; id < size; ++id)
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id);
                if (!spellInfo || !spellInfo->SpellName || !*spellInfo->SpellName)
                    continue;
                auto itr = spellsByName.find(Lower(spellInfo->SpellName));
                if (itr != spellsByName.end())
                    itr->second.push_back(id);
            }

            uint32 unknown = 0;
            for (auto& pair : spellsByName)
            {
                std::sort(pair.second.rbegin(), pair.second.rend());
                if (pair.second.empty())
                {
                    ++unknown;
                    TC_LOG_ERROR("server.loading", "[mod-playerbots] Rotations: no spell named '%s' in Spell.db2, its steps are skipped.",
                        pair.first.c_str());
                }
            }
            TC_LOG_INFO("server.loading", "[mod-playerbots] Rotations: %u specs, %u spell names, %u not found.",
                uint32(Rotations.size()), uint32(spellsByName.size()), unknown);
        }
    }

    void Initialize()
    {
        std::call_once(initOnce, BuildIndex);
    }

    char const* RoleName(CombatRole role)
    {
        switch (role)
        {
            case CombatRole::Tank:   return "tank";
            case CombatRole::Healer: return "healer";
            case CombatRole::Melee:  return "melee";
            default:                 return "ranged";
        }
    }

    std::vector<Defensive const*> DefensivesFor(uint8 cls, uint32 specId)
    {
        std::vector<Defensive const*> list;
        for (Defensive const& defensive : Defensives)
            if (defensive.cls == cls && (!defensive.specId || defensive.specId == specId))
                list.push_back(&defensive);
        return list;
    }

    uint32 ClassFamily(uint8 cls)
    {
        switch (cls)
        {
            case CLASS_WARRIOR:      return SPELLFAMILY_WARRIOR;
            case CLASS_PALADIN:      return SPELLFAMILY_PALADIN;
            case CLASS_HUNTER:       return SPELLFAMILY_HUNTER;
            case CLASS_ROGUE:        return SPELLFAMILY_ROGUE;
            case CLASS_PRIEST:       return SPELLFAMILY_PRIEST;
            case CLASS_DEATH_KNIGHT: return SPELLFAMILY_DEATHKNIGHT;
            case CLASS_SHAMAN:       return SPELLFAMILY_SHAMAN;
            case CLASS_MAGE:         return SPELLFAMILY_MAGE;
            case CLASS_WARLOCK:      return SPELLFAMILY_WARLOCK;
            case CLASS_MONK:         return SPELLFAMILY_MONK;
            case CLASS_DRUID:        return SPELLFAMILY_DRUID;
            case CLASS_DEMON_HUNTER: return SPELLFAMILY_DEMON_HUNTER;
            default:                 return SPELLFAMILY_GENERIC;
        }
    }

    SpecRotation const* GetRotation(uint32 specId)
    {
        for (SpecRotation const& rotation : Rotations)
            if (rotation.specId == specId)
                return &rotation;
        return nullptr;
    }

    CombatRole RoleOf(Player const* bot)
    {
        if (SpecRotation const* rotation = GetRotation(bot->GetSpecializationId()))
            return rotation->role;

        switch (bot->GetSpecializationRole())
        {
            case ROLES_TANK:   return CombatRole::Tank;
            case ROLES_HEALER: return CombatRole::Healer;
            default:           break;
        }

        switch (bot->getClass())
        {
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_PRIEST:
            case CLASS_HUNTER:
                return CombatRole::Ranged;
            default:
                return CombatRole::Melee;
        }
    }

    uint32 KnownSpell(Player* bot, char const* name, uint32 family)
    {
        if (!name)
            return 0;

        Initialize();
        std::vector<uint32> const& ids = SpellsNamed(name);
        // the class's own spells first: two classes may both have a spell of the name
        for (int pass = 0; pass < 2; ++pass)
            for (uint32 id : ids)
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id);
                if (!spellInfo)
                    continue;
                bool const ofFamily = spellInfo->ClassOptions.SpellClassSet == family;
                if ((pass == 0) != ofFamily)
                    continue;
                if (bot->HasSpell(id))
                    return id;
            }
        return 0;
    }

    bool HasAuraNamed(Unit const* unit, char const* name, ObjectGuid const& caster)
    {
        if (!unit || !name)
            return false;

        Initialize();
        for (uint32 id : SpellsNamed(name))
            if (unit->HasAura(id, caster))
                return true;
        return false;
    }

    uint32 AuraStacksNamed(Unit const* unit, char const* name)
    {
        if (!unit || !name)
            return 0;

        Initialize();
        uint32 stacks = 0;
        for (uint32 id : SpellsNamed(name))
            if (Aura const* aura = unit->GetAura(id))
                stacks = std::max<uint32>(stacks, aura->GetStackAmount());
        return stacks;
    }

    float DisplayedPower(Player const* bot, Powers power)
    {
        float value = float(bot->GetPower(power));
        if (PowerTypeEntry const* entry = sDB2Manager.GetPowerType(uint8(power)))
            if (entry->DisplayModifier > 1)
                value /= float(entry->DisplayModifier);
        return value;
    }

    bool CheckCondition(Player* bot, Unit* target, RotationStep const& step, std::vector<Player*> const& allies)
    {
        switch (step.cond)
        {
            case Cond::Always:
                return true;
            case Cond::TargetHealthPctBelow:
                return target && target->GetHealthPct() <= step.value;
            case Cond::SelfHealthPctBelow:
                return bot->GetHealthPct() <= step.value;
            case Cond::ResourceAtLeast:
                return DisplayedPower(bot, Powers(step.power)) >= step.value;
            case Cond::AuraMissingOnSelf:
                return !HasAuraNamed(bot, step.auxSpell);
            case Cond::AuraPresentOnSelf:
                return HasAuraNamed(bot, step.auxSpell);
            case Cond::AuraStacksBelowOnSelf:
                return AuraStacksNamed(bot, step.auxSpell) < uint32(step.value);
            case Cond::AuraMissingOnTarget:
                return target && !HasAuraNamed(target, step.auxSpell, bot->GetGUID());
            case Cond::InjuredNearTarget:
                return target && BotCombat::CountInjured(allies, target, step.radius) >= uint32(step.value);
            case Cond::InjuredNearSelf:
                return BotCombat::CountInjured(allies, bot, step.radius) >= uint32(step.value);
        }
        return false;
    }

    std::string DescribeCondition(RotationStep const& step)
    {
        char buffer[160];
        switch (step.cond)
        {
            case Cond::Always:               return "always";
            case Cond::TargetHealthPctBelow: snprintf(buffer, sizeof(buffer), "target health <= %.0f%%", step.value); break;
            case Cond::SelfHealthPctBelow:   snprintf(buffer, sizeof(buffer), "own health <= %.0f%%", step.value); break;
            case Cond::ResourceAtLeast:      snprintf(buffer, sizeof(buffer), "power %u >= %.0f", step.power, step.value); break;
            case Cond::AuraMissingOnSelf:    snprintf(buffer, sizeof(buffer), "no %s on self", step.auxSpell); break;
            case Cond::AuraPresentOnSelf:    snprintf(buffer, sizeof(buffer), "%s on self", step.auxSpell); break;
            case Cond::AuraStacksBelowOnSelf: snprintf(buffer, sizeof(buffer), "fewer than %.0f %s on self", step.value, step.auxSpell); break;
            case Cond::AuraMissingOnTarget:  snprintf(buffer, sizeof(buffer), "no %s of mine on target", step.auxSpell); break;
            case Cond::InjuredNearTarget:    snprintf(buffer, sizeof(buffer), "%.0f allies below 85%% within %.0f yd of target", step.value, step.radius); break;
            case Cond::InjuredNearSelf:      snprintf(buffer, sizeof(buffer), "%.0f allies below 85%% within %.0f yd", step.value, step.radius); break;
            default:                         return "?";
        }
        return buffer;
    }
}
