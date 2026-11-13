/*
 * mod-playerbots: SimulationCraft APL interpreter. See BotApl.h.
 */

#include "BotApl.h"
#include "BotRotations.h"
#include "DB2Stores.h"
#include "Log.h"
#include "Pet.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>

namespace BotApl
{
namespace
{
    // ---- Terms: what an identifier of an expression reads -----------------------------------------------------

    enum class T : uint8
    {
        Const,
        Power, PowerMax, PowerDeficit, PowerPct, PowerRegen, PowerTimeToMax,
        RuneReady, RuneTimeTo, CpMaxSpend,
        BuffUp, BuffRemains, BuffStack, BuffDuration, BuffMaxStack,
        DebuffUp, DebuffRemains, DebuffStack, DebuffDuration, DebuffRefreshable, DebuffTickTime,
        ActiveDot,
        CdRemains, CdReady, CdCharges, CdChargesFractional, CdRechargeTime, CdDuration, CdFullRecharge,
        TalentEnabled,
        SpellTargets, ActiveEnemies,
        TargetHealthPct, TargetHealth, TargetHealthMax, TargetTimeToDie, TargetDistance, TargetLevel, TargetCasting,
        HealthPct, Health, HealthMax, HealthDeficit,
        Gcd, GcdRemains, Time, CastTime, ExecuteTime,
        PrevGcd, Variable, Stealthed, PetActive, Race, IncomingDamage, Level, SpellHaste,
        BurnPhase, BurnPhaseDuration,
        CdMaxCharges, DebuffCount, RtbBuffs, PetBuffUp, PetBuffRemains, FirestarterActive
    };

    struct Term
    {
        T kind = T::Const;
        bool negate = false;            // buff.x.down = !buff.x.up
        double value = 0.0;             // Const; RuneTimeTo's count; IncomingDamage's ms
        Powers power = POWER_MANA;
        std::string name;               // normalized spell / aura / variable name; "" = the action's own spell
        int n = 0;                      // prev_gcd.N
    };

    // ---- Expressions ------------------------------------------------------------------------------------------

    enum class Op : uint8 { Num, Term, Not, Neg, Abs, Floor, Ceil, Add, Sub, Mul, Div, Mod, Lt, Le, Gt, Ge, Eq, Ne, And, Or, Xor };

    struct Node
    {
        Op op = Op::Num;
        double value = 0.0;
        int term = -1;
        int a = -1;
        int b = -1;
    };

    struct Expr
    {
        std::vector<Node> nodes;
        std::vector<Term> terms;
        int root = -1;                  // -1 = empty (true)
    };

    // ---- Actions ----------------------------------------------------------------------------------------------

    enum class A : uint8 { Spell, CallList, RunList, Variable, SummonPet, ApplyPoison, StartBurn, StopBurn, Skip };

    struct Action
    {
        A kind = A::Spell;
        uint32 id = 0;                  // unique in the profile (line_cd, logs)
        std::string raw;                // the action text, for the logs
        std::string list;               // the list it belongs to ("" = main)
        std::string spell;              // normalized spell name
        std::string target;             // call/run_action_list: list name; variable: variable name
        Expr cond;
        bool self = false;              // target=self
        bool never = false;             // moving=1: the bot casts standing
        std::string op = "set";         // variable
        Expr value;
        Expr valueElse;
        Expr condition;                 // variable op=setif
        double defaultValue = 0.0;
        double lineCd = 0.0;            // seconds
        std::string sync;               // normalized spell name that must be ready
        std::string petName;            // summon_pet,name=
        std::string poison;             // apply_poison,lethal=
    };

    struct Profile
    {
        uint32 specId = 0;
        std::string name;
        std::string talents;
        std::map<std::string, std::vector<Action>> lists;     // "" = actions, "precombat", ...
        std::set<std::string> unknownTerms;
        std::set<std::string> spellNames;
    };

    std::map<uint32, Profile> profiles;                       // read-only after Initialize
    std::map<std::string, std::vector<uint32>> spellIndex;    // normalized name -> spell ids, highest first
    std::once_flag initOnce;

    // actions that are not spells, or that the bot handles elsewhere / never
    std::set<std::string> const SkippedActions =
    {
        "autoattack", "autoshot", "useitem", "useitems", "potion", "flask", "food", "augmentation", "snapshotstats",
        "bloodlust", "timewarp", "poolresource", "wait", "sequence", "strictsequence", "cancelbuff", "procsephuz",
        "pickupfragment", "heroicleap", "charge", "felrush", "vengefulretreat", "darkflight", "sprint", "dash",
        "shadowmeld", "harpoon", "rollthebones2"
    };

    // SimC action names whose spell has another name
    // SimC names for spells of another name (or several): the alias gets the ids of all of them in the spell index,
    // so actions, buffs, dots and cooldowns all find them (owner's build #28 log: Feral's thrash_cat / swipe_cat
    // and Guardian's thrash_bear read nothing, Affliction's unstable_affliction_1..5 neither)
    std::map<std::string, std::vector<std::string>> const Aliases =
    {
        { "moonfirecat", { "moonfire" } }, { "thrashcat", { "thrash" } }, { "thrashbear", { "thrash" } },
        { "swipecat", { "swipe" } }, { "swipebear", { "swipe" } },
        { "incarnation", { "incarnationkingofthejungle", "incarnationchosenofelune", "incarnationguardianofursoc", "incarnationtreeoflife" } },
        { "servicepet", { "grimoirefelguard", "grimoireimp", "grimoirefelhunter", "grimoirevoidwalker", "grimoiresuccubus" } },
        { "bladestormmh", { "bladestorm" } }, { "battlecrydeadlycalm", { "battlecry" } },
        { "unstableaffliction1", { "unstableaffliction" } }, { "unstableaffliction2", { "unstableaffliction" } },
        { "unstableaffliction3", { "unstableaffliction" } }, { "unstableaffliction4", { "unstableaffliction" } },
        { "unstableaffliction5", { "unstableaffliction" } }, { "deadlypoisondot", { "deadlypoison" } },
        { "bladedance1", { "bladedance" } }, { "eyebeamtick", { "eyebeam" } }, { "brews", { "ironskinbrew" } },
        { "greatergiftoftheox", { "giftoftheox" } }, { "bokproc", { "blackoutkick" } }
    };

    // names of set bonuses, legendaries and sim-only buffs: always absent, and not reported
    bool QuietName(std::string const& name)
    {
        static char const* const prefixes[] = { "t19", "t20", "t21", "sephuz", "sindorei", "kaelthas", "insanitydrain", "havoct2",
            "trinket", "activehavoc", "mooncd" };
        for (char const* prefix : prefixes)
            if (name.rfind(prefix, 0) == 0)
                return true;
        return false;
    }

    // sim-only terms that stay 0 without being reported (no game state behind them, or nothing a bot can do with it)
    bool QuietTerm(std::string const& id)
    {
        static char const* const exact[] = { "mantle_duration", "exsanguinated", "finality", "poisoned_bleeds", "fok_rotation",
            "ssw_refund_offset", "raw_haste_pct", "nonexecute_actors_pct", "current_insanity_drain", "cast_regen",
            "feral_spirit.remains", "target.is_add", "sim.target", "target", "new_tick_time", "darkglare_no_de", "doomguard_no_de",
            "dreadstalker_count", "dreadstalker_no_de", "dreadstalker_remaining_duration", "infernal_no_de", "pet_count",
            "service_no_de", "wild_imp_count", "wild_imp_no_de", "wild_imp_remaining_duration", "dot.virulent_plague.tick_time_remains" };
        for (char const* name : exact)
            if (id == name)
                return true;
        static char const* const prefixes[] = { "movement.", "stat.", "consumable.", "lowest_vuln_within", "time_to_sht" };
        for (char const* prefix : prefixes)
            if (id.rfind(prefix, 0) == 0)
                return true;
        static char const* const suffixes[] = { ".improved", ".extended_by_demonic", ".cost", ".travel_time", ".cast_regen", ".vuln_casts",
            ".marks_next_gcd", ".new_tick_time", ".value", ".duration_guess" };
        for (char const* suffix : suffixes)
        {
            size_t const n = std::strlen(suffix);
            if (id.size() > n && id.compare(id.size() - n, n, suffix) == 0)
                return true;
        }
        return false;
    }

    std::map<std::string, Powers> const PowerNames =
    {
        { "mana", POWER_MANA }, { "rage", POWER_RAGE }, { "focus", POWER_FOCUS }, { "energy", POWER_ENERGY },
        { "combo_points", POWER_COMBO_POINTS }, { "runic_power", POWER_RUNIC_POWER }, { "soul_shard", POWER_SOUL_SHARDS },
        { "astral_power", POWER_LUNAR_POWER }, { "holy_power", POWER_HOLY_POWER }, { "maelstrom", POWER_MAELSTROM },
        { "chi", POWER_CHI }, { "insanity", POWER_INSANITY }, { "arcane_charges", POWER_ARCANE_CHARGES },
        { "fury", POWER_FURY }, { "pain", POWER_PAIN }
    };

    std::map<std::string, uint8> const RaceNames =
    {
        { "human", RACE_HUMAN }, { "orc", RACE_ORC }, { "dwarf", RACE_DWARF }, { "night_elf", RACE_NIGHTELF },
        { "undead", RACE_UNDEAD_PLAYER }, { "tauren", RACE_TAUREN }, { "gnome", RACE_GNOME }, { "troll", RACE_TROLL },
        { "goblin", RACE_GOBLIN }, { "blood_elf", RACE_BLOODELF }, { "draenei", RACE_DRAENEI }, { "worgen", RACE_WORGEN },
        { "pandaren", RACE_PANDAREN_NEUTRAL }
    };

    // ---- Parsing ----------------------------------------------------------------------------------------------

    std::vector<std::string> Split(std::string const& text, char by)
    {
        std::vector<std::string> parts;
        std::string part;
        for (char c : text)
        {
            if (c == by)
            {
                parts.push_back(part);
                part.clear();
            }
            else
                part += c;
        }
        parts.push_back(part);
        return parts;
    }

    bool IsNumber(std::string const& text)
    {
        if (text.empty())
            return false;
        char* end = nullptr;
        std::strtod(text.c_str(), &end);
        return end && *end == '\0';
    }

    // an identifier of an expression as a Term; unknown identifiers are 0 and reported
    Term MakeTerm(std::string const& id, Profile& profile)
    {
        Term t;
        std::vector<std::string> p = Split(id, '.');
        auto field = [&](size_t i) -> std::string { return i < p.size() ? p[i] : std::string(); };
        auto unknown = [&]()
        {
            if (!QuietTerm(id))
                profile.unknownTerms.insert(id);
            t.kind = T::Const;
            t.value = 0.0;
        };
        auto spell = [&](std::string const& n)
        {
            std::string s = Normalize(n);
            if (!QuietName(s))
                profile.spellNames.insert(s);
            return s;
        };
        if (QuietTerm(id))
        {
            t.kind = T::Const;
            return t;
        }
        // attack_haste: the melee haste multiplier
        if (id == "attack_haste")
        {
            t.kind = T::SpellHaste;
            return t;
        }
        // the target's health while above 90% (Fire's Firestarter)
        if (id == "firestarter.active")
        {
            t.kind = T::FirestarterActive;
            return t;
        }
        // Outlaw: how many Roll the Bones buffs are up
        if (id == "rtb_buffs")
        {
            t.kind = T::RtbBuffs;
            return t;
        }
        // Affliction: how many Unstable Afflictions of the bot are on the target
        if (id == "buff.active_uas.stack" || id == "active_uas")
        {
            t.kind = T::DebuffCount;
            t.name = spell("unstable_affliction");
            return t;
        }
        // Brewmaster: stagger level auras on the bot
        if (id == "stagger.heavy" || id == "stagger.moderate" || id == "stagger.light")
        {
            t.kind = T::BuffUp;
            t.name = spell(id.substr(8) + "_stagger");
            return t;
        }
        // Unholy: Death and Decay / Defile under the bot (their buff)
        if (id == "death_and_decay.ticking" || id == "defile.ticking")
        {
            t.kind = T::BuffUp;
            t.name = spell(id.substr(0, id.find('.')));
            return t;
        }
        if (id == "time_to_die")
        {
            t.kind = T::TargetTimeToDie;
            return t;
        }
        if (id == "max_charges")
        {
            t.kind = T::CdMaxCharges;
            return t;
        }
        if (id == "cooldown_react")
        {
            t.kind = T::CdReady;
            return t;
        }
        if (id == "pmultiplier" || id == "persistent_multiplier")
        {
            t.kind = T::Const;
            t.value = 1.0;
            return t;
        }
        // pet.<name>.buff.<aura>.up / down / remains: the bot's pet's aura
        if (p.size() >= 5 && p[0] == "pet" && p[2] == "buff")
        {
            t.name = spell(p[3]);
            if (p[4] == "remains")
                t.kind = T::PetBuffRemains;
            else
            {
                t.kind = T::PetBuffUp;
                t.negate = p[4] == "down";
            }
            return t;
        }
        // prev_off_gcd.X (no count)
        if (p.size() == 2 && p[0] == "prev_off_gcd")
        {
            t.kind = T::PrevGcd;
            t.n = 1;
            t.name = spell(p[1]);
            return t;
        }

        std::string const& head = p[0];

        // resources: energy, energy.deficit, rune.time_to_3, ...
        auto power = PowerNames.find(head);
        if (power != PowerNames.end())
        {
            t.power = power->second;
            std::string const f = field(1);
            if (f.empty())                                      t.kind = T::Power;
            else if (f == "max")                                t.kind = T::PowerMax;
            else if (f == "deficit")                            t.kind = T::PowerDeficit;
            else if (f == "pct")                                t.kind = T::PowerPct;
            else if (f == "regen")                              t.kind = T::PowerRegen;
            else if (f == "time_to_max")                        t.kind = T::PowerTimeToMax;
            else
                unknown();
            return t;
        }
        if (head == "rune")
        {
            std::string const f = field(1);
            if (f.empty())
                t.kind = T::RuneReady;
            else if (f.rfind("time_to_", 0) == 0)
            {
                t.kind = T::RuneTimeTo;
                t.value = std::atof(f.c_str() + 8);
            }
            else
                unknown();
            return t;
        }
        if (head == "cp_max_spend")
        {
            t.kind = T::CpMaxSpend;
            return t;
        }

        if (head == "buff" && p.size() >= 2)
        {
            // Arcane charges are a power on Legion, not an aura
            if (p[1] == "arcane_charge")
            {
                t.power = POWER_ARCANE_CHARGES;
                t.kind = field(2) == "max_stack" ? T::PowerMax : T::Power;
                return t;
            }
            t.name = spell(p[1]);
            std::string const f = field(2);
            if (f.empty() || f == "up")                         t.kind = T::BuffUp;
            else if (f == "down")                               { t.kind = T::BuffUp; t.negate = true; }
            else if (f == "remains")                            t.kind = T::BuffRemains;
            else if (f == "stack" || f == "react")              t.kind = T::BuffStack;
            else if (f == "duration")                           t.kind = T::BuffDuration;
            else if (f == "max_stack")                          t.kind = T::BuffMaxStack;
            else
                unknown();
            return t;
        }

        // debuff.x / dot.x / target.debuff.x / target.dot.x: the bot's own aura on the target
        size_t at = 0;
        if (head == "target" && p.size() >= 3 && (p[1] == "debuff" || p[1] == "dot"))
            at = 1;
        if ((p[at] == "debuff" || p[at] == "dot") && p.size() >= at + 2)
        {
            if (p[at + 1] == "casting")
            {
                t.kind = T::TargetCasting;
                return t;
            }
            t.name = spell(p[at + 1]);
            std::string const f = field(at + 2);
            if (f.empty() || f == "up" || f == "ticking" || f == "react")   t.kind = T::DebuffUp;
            else if (f == "down")                               { t.kind = T::DebuffUp; t.negate = true; }
            else if (f == "remains")                            t.kind = T::DebuffRemains;
            else if (f == "stack")                              t.kind = T::DebuffStack;
            else if (f == "duration")                           t.kind = T::DebuffDuration;
            else if (f == "refreshable")                        t.kind = T::DebuffRefreshable;
            else if (f == "tick_time")                          t.kind = T::DebuffTickTime;
            else if (f == "pmultiplier" || f == "persistent_multiplier") { t.kind = T::Const; t.value = 1.0; }
            else if (f == "max_stacks" || f == "max_stack")     t.kind = T::BuffMaxStack;
            else
                unknown();
            return t;
        }

        if (head == "active_dot" && p.size() >= 2)
        {
            t.kind = T::ActiveDot;
            t.name = spell(p[1]);
            return t;
        }

        if ((head == "cooldown" || head == "action") && p.size() >= 3)
        {
            t.name = spell(p[1]);
            std::string const f = p[2];
            if (f == "remains" || f == "remains_expected")      t.kind = T::CdRemains;
            else if (f == "up" || f == "ready")                 t.kind = T::CdReady;
            else if (f == "charges")                            t.kind = T::CdCharges;
            else if (f == "charges_fractional")                 t.kind = T::CdChargesFractional;
            else if (f == "recharge_time")                      t.kind = T::CdRechargeTime;
            else if (f == "duration")                           t.kind = T::CdDuration;
            else if (f == "full_recharge_time")                 t.kind = T::CdFullRecharge;
            else if (f == "max_charges")                        t.kind = T::CdMaxCharges;
            else if (f == "usable")                             t.kind = T::CdReady;
            else if (f == "usable_in" || f == "adjusted_remains") t.kind = T::CdRemains;
            else if (f == "cooldown")                           t.kind = T::CdDuration;
            else if (f == "cast_time")                          t.kind = T::CastTime;
            else if (f == "execute_time")                       t.kind = T::ExecuteTime;
            else if (f == "in_flight" || f == "in_flight_to_target") { t.kind = T::Const; t.value = 0.0; }
            else
                unknown();
            return t;
        }

        if (head == "talent" && p.size() >= 2)
        {
            t.kind = T::TalentEnabled;
            t.name = Normalize(p[1]);
            return t;
        }

        // owner's decision: artifacts count as learned (they come later); gear, sets, trinkets as absent
        if (head == "artifact")
        {
            t.kind = T::Const;
            t.value = 1.0;
            return t;
        }
        if (head == "equipped" || head == "set_bonus" || head == "trinket" || head == "azerite" || head == "legendary")
        {
            t.kind = T::Const;
            return t;
        }
        if (head == "raid_event")
        {
            t.kind = T::Const;
            t.value = field(2) == "in" ? 10000.0 : 0.0;     // never comes
            return t;
        }

        if (head == "spell_targets")
        {
            t.kind = T::SpellTargets;
            if (p.size() >= 2)
                t.name = spell(p[1]);
            return t;
        }
        if (head == "active_enemies")                         { t.kind = T::ActiveEnemies; return t; }
        if (head == "desired_targets")                        { t.kind = T::Const; t.value = 1.0; return t; }

        if (head == "target")
        {
            std::string const f = field(1);
            std::string const g = field(2);
            if (f == "health" && g == "pct")                    t.kind = T::TargetHealthPct;
            else if (f == "pct_health")                         t.kind = T::TargetHealthPct;
            else if (f == "health" && g == "max")               t.kind = T::TargetHealthMax;
            else if (f == "health" && g.empty())                t.kind = T::TargetHealth;
            else if (f == "time_to_die")                        t.kind = T::TargetTimeToDie;
            else if (f.rfind("time_to_pct_", 0) == 0)           t.kind = T::TargetTimeToDie;
            else if (f == "distance")                           t.kind = T::TargetDistance;
            else if (f == "level")                              t.kind = T::TargetLevel;
            else
                unknown();
            return t;
        }

        if (head == "health")
        {
            std::string const f = field(1);
            if (f == "pct")                                     t.kind = T::HealthPct;
            else if (f == "max")                                t.kind = T::HealthMax;
            else if (f == "deficit")                            t.kind = T::HealthDeficit;
            else if (f.empty())                                 t.kind = T::Health;
            else
                unknown();
            return t;
        }

        if (head == "gcd")
        {
            t.kind = field(1) == "remains" ? T::GcdRemains : T::Gcd;
            return t;
        }
        if (head == "time")                                   { t.kind = T::Time; return t; }
        if (head == "level")                                  { t.kind = T::Level; return t; }
        if (head == "spell_haste")                            { t.kind = T::SpellHaste; return t; }
        if (head == "burn_phase")                             { t.kind = T::BurnPhase; return t; }
        if (head == "burn_phase_duration")                    { t.kind = T::BurnPhaseDuration; return t; }
        if (head == "moving")                                 { t.kind = T::Const; return t; }

        // the action's own spell
        if (p.size() == 1)
        {
            if (head == "cast_time")                            { t.kind = T::CastTime; return t; }
            if (head == "execute_time")                         { t.kind = T::ExecuteTime; return t; }
            if (head == "remains")                              { t.kind = T::DebuffRemains; return t; }
            if (head == "ticking")                              { t.kind = T::DebuffUp; return t; }
            if (head == "refreshable")                          { t.kind = T::DebuffRefreshable; return t; }
            if (head == "duration")                             { t.kind = T::DebuffDuration; return t; }
            if (head == "tick_time")                            { t.kind = T::DebuffTickTime; return t; }
            if (head == "charges")                              { t.kind = T::CdCharges; return t; }
            if (head == "charges_fractional")                   { t.kind = T::CdChargesFractional; return t; }
            if (head == "recharge_time")                        { t.kind = T::CdRechargeTime; return t; }
            if (head == "full_recharge_time")                   { t.kind = T::CdFullRecharge; return t; }
            if (head == "cooldown")                             { t.kind = T::CdRemains; return t; }
            if (head == "in_flight" || head == "travel_time")   { t.kind = T::Const; return t; }
        }

        if ((head == "prev_gcd" || head == "prev_off_gcd") && p.size() >= 3)
        {
            t.kind = T::PrevGcd;
            t.n = std::max(1, std::atoi(p[1].c_str()));
            t.name = spell(p[2]);
            return t;
        }
        if (head == "prev" && p.size() >= 2)
        {
            t.kind = T::PrevGcd;
            t.n = 1;
            t.name = spell(p[1]);
            return t;
        }

        if (head == "variable" && p.size() >= 2)
        {
            t.kind = T::Variable;
            t.name = p[1];
            return t;
        }
        if (head == "stealthed")                              { t.kind = T::Stealthed; return t; }
        if (head == "pet" && field(2) == "active")            { t.kind = T::PetActive; return t; }
        if (head == "race" && p.size() >= 2)
        {
            t.kind = T::Race;
            auto race = RaceNames.find(p[1]);
            t.value = race != RaceNames.end() ? race->second : 0;
            return t;
        }
        if (head.rfind("incoming_damage_", 0) == 0)
        {
            t.kind = T::IncomingDamage;
            std::string const span = head.substr(16);
            double amount = std::atof(span.c_str());
            if (span.size() >= 2 && span.compare(span.size() - 2, 2, "ms") == 0)
                t.value = amount;
            else
                t.value = amount * 1000.0;
            return t;
        }

        unknown();
        return t;
    }

    // SimC precedence: | lowest, then ^, &, comparisons, + -, * % %% (% = divide, %% = modulo), unary ! - @
    class Parser
    {
    public:
        Parser(std::string const& text, Expr& expr, Profile& profile) : _s(text), _e(expr), _profile(profile) { }

        int Parse()
        {
            int const root = Or();
            Skip();
            return root;
        }

    private:
        void Skip() { while (_i < _s.size() && _s[_i] == ' ') ++_i; }
        bool Eat(char const* op)
        {
            Skip();
            size_t const n = std::strlen(op);
            if (_s.compare(_i, n, op) == 0)
            {
                _i += n;
                return true;
            }
            return false;
        }
        int Add(Op op, int a, int b = -1, double value = 0.0, int term = -1)
        {
            Node node;
            node.op = op;
            node.a = a;
            node.b = b;
            node.value = value;
            node.term = term;
            _e.nodes.push_back(node);
            return int(_e.nodes.size()) - 1;
        }

        int Or()
        {
            int a = Xor();
            while (true)
            {
                Skip();
                if (_i < _s.size() && _s[_i] == '|')
                {
                    ++_i;
                    a = Add(Op::Or, a, Xor());
                }
                else
                    return a;
            }
        }
        int Xor()
        {
            int a = And();
            while (Eat("^"))
                a = Add(Op::Xor, a, And());
            return a;
        }
        int And()
        {
            int a = Cmp();
            while (true)
            {
                Skip();
                if (_i < _s.size() && _s[_i] == '&')
                {
                    ++_i;
                    a = Add(Op::And, a, Cmp());
                }
                else
                    return a;
            }
        }
        int Cmp()
        {
            int a = Sum();
            while (true)
            {
                if (Eat("<="))      a = Add(Op::Le, a, Sum());
                else if (Eat(">=")) a = Add(Op::Ge, a, Sum());
                else if (Eat("!=")) a = Add(Op::Ne, a, Sum());
                else if (Eat("<"))  a = Add(Op::Lt, a, Sum());
                else if (Eat(">"))  a = Add(Op::Gt, a, Sum());
                else if (Eat("="))  a = Add(Op::Eq, a, Sum());
                else
                    return a;
            }
        }
        int Sum()
        {
            int a = Product();
            while (true)
            {
                if (Eat("+"))       a = Add(Op::Add, a, Product());
                else if (Eat("-"))  a = Add(Op::Sub, a, Product());
                else
                    return a;
            }
        }
        int Product()
        {
            int a = Unary();
            while (true)
            {
                if (Eat("*"))       a = Add(Op::Mul, a, Unary());
                else if (Eat("%%")) a = Add(Op::Mod, a, Unary());
                else if (Eat("%"))  a = Add(Op::Div, a, Unary());
                else
                    return a;
            }
        }
        int Unary()
        {
            if (Eat("!")) return Add(Op::Not, Unary());
            if (Eat("-")) return Add(Op::Neg, Unary());
            if (Eat("+")) return Unary();
            if (Eat("@")) return Add(Op::Abs, Unary());
            return Primary();
        }
        int Primary()
        {
            Skip();
            if (Eat("("))
            {
                int const inner = Or();
                Eat(")");
                return inner;
            }
            size_t const start = _i;
            while (_i < _s.size() && (std::isalnum((unsigned char)_s[_i]) || _s[_i] == '_' || _s[_i] == '.'))
                ++_i;
            std::string const word = _s.substr(start, _i - start);
            if (word.empty())
            {
                if (_i < _s.size())
                    ++_i;                                   // a character it does not know: skip it
                return Add(Op::Num, -1, -1, 0.0);
            }
            if ((word == "floor" || word == "ceil") && Eat("("))
            {
                int const inner = Or();
                Eat(")");
                return Add(word == "floor" ? Op::Floor : Op::Ceil, inner);
            }
            if (IsNumber(word))
                return Add(Op::Num, -1, -1, std::atof(word.c_str()));
            _e.terms.push_back(MakeTerm(word, _profile));
            return Add(Op::Term, -1, -1, 0.0, int(_e.terms.size()) - 1);
        }

        std::string const& _s;
        Expr& _e;
        Profile& _profile;
        size_t _i = 0;
    };

    void ParseExpr(std::string const& text, Expr& expr, Profile& profile)
    {
        expr = Expr();
        if (text.empty())
            return;
        Parser parser(text, expr, profile);
        expr.root = parser.Parse();
    }

    // one `actions[.list](+)=a,opt=v/b,...` line
    void ParseLine(std::string line, Profile& profile, uint32& nextId)
    {
        size_t const eq = line.find('=');
        if (eq == std::string::npos)
            return;
        std::string lhs = line.substr(0, eq);
        std::string body = line.substr(eq + 1);
        if (!lhs.empty() && lhs.back() == '+')
            lhs.pop_back();
        std::string list = lhs.size() > 8 ? lhs.substr(8) : std::string();     // "actions.<list>"

        for (std::string text : Split(body, '/'))
        {
            if (text.empty())
                continue;
            std::vector<std::string> opts = Split(text, ',');
            Action action;
            action.id = nextId++;
            action.raw = text;
            action.list = list;
            std::string const name = opts[0];
            std::string normalized = Normalize(name);
            if (QuietName(normalized))
                normalized = "useitem";                     // a legendary / trinket action: skipped

            std::map<std::string, std::string> o;
            for (size_t i = 1; i < opts.size(); ++i)
            {
                size_t const at = opts[i].find('=');
                if (at != std::string::npos)
                    o[opts[i].substr(0, at)] = opts[i].substr(at + 1);
            }

            if (name == "call_action_list")         action.kind = A::CallList;
            else if (name == "run_action_list")     action.kind = A::RunList;
            else if (name == "variable")            action.kind = A::Variable;
            else if (name == "summon_pet")          action.kind = A::SummonPet;
            else if (name == "apply_poison")        action.kind = A::ApplyPoison;
            else if (name == "start_burn_phase")    action.kind = A::StartBurn;
            else if (name == "stop_burn_phase")     action.kind = A::StopBurn;
            else if (SkippedActions.count(normalized))
                action.kind = A::Skip;
            else
            {
                action.kind = A::Spell;
                action.spell = normalized;
                profile.spellNames.insert(normalized);
            }

            std::string cond = o["if"];
            // target_if=<expr> is a condition; target_if=min:/max:/first: picks a target, which bots do not
            auto targetIf = o.find("target_if");
            if (targetIf != o.end() && targetIf->second.find(':') == std::string::npos)
                cond = cond.empty() ? targetIf->second : "(" + cond + ")&(" + targetIf->second + ")";
            ParseExpr(cond, action.cond, profile);

            action.target = o["name"];
            action.self = o["target"] == "self";
            action.never = o["moving"] == "1";
            if (o.count("op"))
                action.op = o["op"];
            ParseExpr(o["value"], action.value, profile);
            ParseExpr(o["value_else"], action.valueElse, profile);
            ParseExpr(o["condition"], action.condition, profile);
            action.defaultValue = std::atof(o["default"].c_str());
            action.lineCd = std::atof(o["line_cd"].c_str());
            if (o.count("sync"))
            {
                action.sync = Normalize(o["sync"]);
                profile.spellNames.insert(action.sync);
            }
            action.petName = o["name"];
            action.poison = o.count("lethal") ? o["lethal"] : std::string("deadly");
            profile.lists[list].push_back(action);
        }
    }

    void ParseProfile(Profile& profile, std::vector<std::string> const& lines)
    {
        uint32 nextId = 1;
        for (std::string const& raw : lines)
        {
            std::string line = raw;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.rfind("talents=", 0) == 0)
                profile.talents = line.substr(8);
            else if (line.rfind("actions", 0) == 0)
                ParseLine(line, profile, nextId);
        }
    }

    // ---- Folder overrides --------------------------------------------------------------------------------------

    std::map<std::string, uint8> const ClassNames =
    {
        { "warrior", CLASS_WARRIOR }, { "paladin", CLASS_PALADIN }, { "hunter", CLASS_HUNTER }, { "rogue", CLASS_ROGUE },
        { "priest", CLASS_PRIEST }, { "deathknight", CLASS_DEATH_KNIGHT }, { "shaman", CLASS_SHAMAN }, { "mage", CLASS_MAGE },
        { "warlock", CLASS_WARLOCK }, { "monk", CLASS_MONK }, { "druid", CLASS_DRUID }, { "demonhunter", CLASS_DEMON_HUNTER }
    };

    // spec_id=N (apl/*.simc), or a SimC profile's class line + spec=name
    uint32 SpecOfFile(std::vector<std::string> const& lines)
    {
        uint8 cls = 0;
        std::string specName;
        for (std::string const& line : lines)
        {
            if (line.rfind("spec_id=", 0) == 0)
                return uint32(std::atoi(line.c_str() + 8));
            size_t const eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string const key = line.substr(0, eq);
            auto c = ClassNames.find(Normalize(key));
            if (c != ClassNames.end() && !cls)
                cls = c->second;
            if (key == "spec")
                specName = Normalize(line.substr(eq + 1));
        }
        if (!cls || specName.empty())
            return 0;
        for (ChrSpecializationEntry const* spec : sChrSpecializationStore)
            if (spec->ClassID == cls && spec->Name && Normalize(spec->Name->Str[LOCALE_enUS]) == specName)
                return spec->ID;
        return 0;
    }

    void BuildSpellIndex()
    {
        std::set<std::string> wanted;
        for (auto const& pair : profiles)
            wanted.insert(pair.second.spellNames.begin(), pair.second.spellNames.end());
        for (auto const& alias : Aliases)
            if (wanted.count(alias.first))
                wanted.insert(alias.second.begin(), alias.second.end());
        for (std::string const& name : wanted)
            spellIndex[name];

        uint32 const size = sSpellMgr->GetSpellInfoStoreSize();
        for (uint32 id = 0; id < size; ++id)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id);
            if (!spellInfo || !spellInfo->SpellName || !*spellInfo->SpellName)
                continue;
            auto itr = spellIndex.find(Normalize(spellInfo->SpellName));
            if (itr != spellIndex.end())
                itr->second.push_back(id);
        }
        for (auto const& alias : Aliases)
        {
            auto itr = spellIndex.find(alias.first);
            if (itr == spellIndex.end())
                continue;
            for (std::string const& target : alias.second)
            {
                auto source = spellIndex.find(target);
                if (source != spellIndex.end())
                    itr->second.insert(itr->second.end(), source->second.begin(), source->second.end());
            }
        }
        for (auto& pair : spellIndex)
        {
            std::sort(pair.second.rbegin(), pair.second.rend());
            pair.second.erase(std::unique(pair.second.begin(), pair.second.end()), pair.second.end());
        }
    }

    std::vector<uint32> const& SpellIds(std::string const& name)
    {
        static std::vector<uint32> const none;
        auto itr = spellIndex.find(name);
        return itr != spellIndex.end() ? itr->second : none;
    }

    // ---- Evaluation --------------------------------------------------------------------------------------------

    struct Context
    {
        Player* bot = nullptr;
        Unit* target = nullptr;
        State* state = nullptr;
        Action const* action = nullptr;
        Profile const* profile = nullptr;
        uint32 family = 0;
        uint32 now = 0;
        bool enemiesBuilt = false;
        std::vector<Unit*> enemies;                 // hostile units in combat within 40 yd of the bot
    };

    uint32 KnownSpellId(Context& ctx, std::string const& name)
    {
        State& state = *ctx.state;
        auto cached = state.known.find(name);
        if (cached != state.known.end())
            return cached->second;

        uint32 found = 0;
        std::vector<uint32> const& ids = SpellIds(name);
        for (int pass = 0; pass < 2 && !found; ++pass)
            for (uint32 id : ids)
            {
                SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
                if (!info || (pass == 0) != (info->ClassOptions.SpellClassSet == ctx.family))
                    continue;
                if (ctx.bot->HasSpell(id))
                {
                    found = id;
                    break;
                }
            }
        state.known[name] = found;
        return found;
    }

    // a known spell id, else any spell of the name (for cooldown / duration data), 0 if none
    uint32 AnySpellId(Context& ctx, std::string const& name)
    {
        if (uint32 id = KnownSpellId(ctx, name))
            return id;
        std::vector<uint32> const& ids = SpellIds(name);
        return ids.empty() ? 0 : ids.front();
    }

    std::string const& OwnSpell(Context& ctx, Term const& t)
    {
        static std::string const none;
        if (!t.name.empty())
            return t.name;
        return ctx.action ? ctx.action->spell : none;
    }

    // the aura of that name on unit (from caster when given; passive auras skipped: talents share names with
    // their procs), the one with the most time left
    Aura* FindAura(Unit* unit, std::string const& name, ObjectGuid const& caster)
    {
        if (!unit || name.empty())
            return nullptr;
        Aura* best = nullptr;
        for (uint32 id : SpellIds(name))
            if (Aura* aura = unit->GetAura(id, caster))
            {
                if (aura->GetSpellInfo()->IsPassive())
                    continue;
                if (!best || aura->GetDuration() < 0 || (best->GetDuration() >= 0 && aura->GetDuration() > best->GetDuration()))
                    best = aura;
            }
        return best;
    }

    double Seconds(int32 ms)
    {
        return ms < 0 ? 9999.0 : ms / 1000.0;
    }

    double MaxDisplayed(Player* bot, Powers power)
    {
        double value = bot->GetMaxPower(power);
        if (PowerTypeEntry const* entry = sDB2Manager.GetPowerType(uint8(power)))
            if (entry->DisplayModifier > 1)
                value /= double(entry->DisplayModifier);
        return value;
    }

    double Regen(Player* bot, Powers power)
    {
        uint32 const index = bot->GetPowerIndex(power);
        if (index < MAX_POWERS_PER_CLASS)
        {
            float const regen = bot->GetFloatValue(UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER + index);
            if (regen > 0.0f)
                return regen;
        }
        return power == POWER_ENERGY ? 10.0 : power == POWER_FOCUS ? 5.0 : 0.0;
    }

    double GcdSeconds(Player* bot)
    {
        // energy users have a fixed 1 s global cooldown, the others 1.5 s shortened by haste
        uint8 const cls = bot->getClass();
        if (cls == CLASS_ROGUE || (cls == CLASS_MONK && bot->GetSpecializationId() != 270) ||
            (cls == CLASS_DRUID && bot->GetSpecializationId() == 103))
            return 1.0;
        float haste = bot->GetFloatValue(UNIT_FIELD_MOD_HASTE);
        if (haste <= 0.0f)
            haste = 1.0f;
        return std::max(0.75, 1.5 * haste);
    }

    void BuildEnemies(Context& ctx)
    {
        if (ctx.enemiesBuilt)
            return;
        ctx.enemiesBuilt = true;
        std::list<Creature*> creatures;
        ctx.bot->GetCreatureListInGrid(creatures, 40.0f);
        for (Creature* creature : creatures)
            if (creature->IsAlive() && creature->isInCombat() && ctx.bot->IsValidAttackTarget(creature) && ctx.bot->IsHostileTo(creature))
                ctx.enemies.push_back(creature);
        if (ctx.target && std::find(ctx.enemies.begin(), ctx.enemies.end(), ctx.target) == ctx.enemies.end())
            ctx.enemies.push_back(ctx.target);
    }

    double Eval(Context& ctx, Expr const& e, int index);

    double TermValue(Context& ctx, Term const& t)
    {
        Player* bot = ctx.bot;
        Unit* target = ctx.target;
        State& state = *ctx.state;
        switch (t.kind)
        {
            case T::Const: return t.value;
            case T::Power: return BotRotations::DisplayedPower(bot, t.power);
            case T::PowerMax: return MaxDisplayed(bot, t.power);
            case T::PowerDeficit: return MaxDisplayed(bot, t.power) - BotRotations::DisplayedPower(bot, t.power);
            case T::PowerPct:
            {
                double const max = MaxDisplayed(bot, t.power);
                return max > 0.0 ? 100.0 * BotRotations::DisplayedPower(bot, t.power) / max : 0.0;
            }
            case T::PowerRegen: return Regen(bot, t.power);
            case T::PowerTimeToMax:
            {
                double const regen = Regen(bot, t.power);
                double const deficit = MaxDisplayed(bot, t.power) - BotRotations::DisplayedPower(bot, t.power);
                return regen > 0.0 ? deficit / regen : (deficit > 0.0 ? 9999.0 : 0.0);
            }
            case T::RuneReady:
            case T::RuneTimeTo:
            {
                std::vector<uint32> cooldowns;
                for (uint8 i = 0; i < MAX_RUNES; ++i)
                    cooldowns.push_back(bot->GetRuneCooldown(i));
                std::sort(cooldowns.begin(), cooldowns.end());
                if (t.kind == T::RuneReady)
                    return double(std::count(cooldowns.begin(), cooldowns.end(), 0u));
                size_t const n = size_t(std::max(1.0, std::min(double(MAX_RUNES), t.value)));
                return cooldowns[n - 1] / 1000.0;
            }
            case T::CpMaxSpend: return MaxDisplayed(bot, POWER_COMBO_POINTS);

            case T::BuffUp:
            {
                bool const up = FindAura(bot, t.name, ObjectGuid::Empty) != nullptr;
                return (up != t.negate) ? 1.0 : 0.0;
            }
            case T::BuffRemains:
            {
                Aura* aura = FindAura(bot, t.name, ObjectGuid::Empty);
                return aura ? Seconds(aura->GetDuration()) : 0.0;
            }
            case T::BuffStack:
            {
                Aura* aura = FindAura(bot, t.name, ObjectGuid::Empty);
                return aura ? std::max<double>({ 1.0, double(aura->GetStackAmount()), double(aura->GetCharges()) }) : 0.0;
            }
            case T::BuffDuration:
            {
                if (Aura* aura = FindAura(bot, t.name, ObjectGuid::Empty))
                    return Seconds(aura->GetMaxDuration());
                uint32 const id = AnySpellId(ctx, t.name);
                SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
                return info ? Seconds(info->GetMaxDuration()) : 0.0;
            }
            case T::BuffMaxStack:
            {
                uint32 const id = AnySpellId(ctx, t.name);
                SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
                return info ? double(info->GetAuraOptions(bot->GetMap()->GetDifficultyID())->CumulativeAura) : 0.0;
            }

            case T::DebuffUp:
            {
                bool const up = FindAura(target, OwnSpell(ctx, t), bot->GetGUID()) != nullptr;
                return (up != t.negate) ? 1.0 : 0.0;
            }
            case T::DebuffRemains:
            {
                Aura* aura = FindAura(target, OwnSpell(ctx, t), bot->GetGUID());
                return aura ? Seconds(aura->GetDuration()) : 0.0;
            }
            case T::DebuffStack:
            {
                Aura* aura = FindAura(target, OwnSpell(ctx, t), bot->GetGUID());
                return aura ? std::max<double>(1.0, aura->GetStackAmount()) : 0.0;
            }
            case T::DebuffDuration:
            {
                if (Aura* aura = FindAura(target, OwnSpell(ctx, t), bot->GetGUID()))
                    return Seconds(aura->GetMaxDuration());
                uint32 const id = AnySpellId(ctx, OwnSpell(ctx, t));
                SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
                return info ? Seconds(info->GetMaxDuration()) : 0.0;
            }
            case T::DebuffRefreshable:
            {
                // SimC's pandemic rule: less than 30% of the duration left
                Aura* aura = FindAura(target, OwnSpell(ctx, t), bot->GetGUID());
                if (!aura)
                    return 1.0;
                if (aura->GetMaxDuration() <= 0)
                    return 0.0;
                return aura->GetDuration() < 0.3 * aura->GetMaxDuration() ? 1.0 : 0.0;
            }
            case T::DebuffTickTime: return 2.0;
            case T::ActiveDot:
            {
                BuildEnemies(ctx);
                double count = 0.0;
                for (Unit* enemy : ctx.enemies)
                    if (FindAura(enemy, t.name, bot->GetGUID()))
                        count += 1.0;
                return count;
            }

            case T::CdRemains:
            case T::CdReady:
            case T::CdCharges:
            case T::CdChargesFractional:
            case T::CdRechargeTime:
            case T::CdDuration:
            case T::CdFullRecharge:
            {
                std::string const& name = OwnSpell(ctx, t);
                uint32 const id = AnySpellId(ctx, name);
                SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
                if (!info)
                    return t.kind == T::CdReady ? 0.0 : 0.0;
                SpellCategoryEntry const* charges = sSpellCategoryStore.LookupEntry(info->Categories.ChargeCategory);
                double const delay = bot->GetSpellCooldownDelay(id);
                double const recharge = charges ? charges->ChargeRecoveryTime / 1000.0 : info->Cooldowns.RecoveryTime / 1000.0;
                double const current = charges ? double(bot->GetChargesForSpell(info)) : (delay > 0.0 ? 0.0 : 1.0);
                switch (t.kind)
                {
                    case T::CdRemains: return charges ? (current > 0.0 ? 0.0 : recharge) : delay;
                    case T::CdReady: return (charges ? current > 0.0 : delay <= 0.0) && KnownSpellId(ctx, name) ? 1.0 : 0.0;
                    case T::CdCharges: return current;
                    case T::CdChargesFractional: return current;
                    case T::CdRechargeTime: return recharge;
                    case T::CdDuration: return std::max(recharge, info->Cooldowns.RecoveryTime / 1000.0);
                    default: return charges ? (charges->MaxCharges - current) * recharge : delay;
                }
            }

            case T::TalentEnabled:
            {
                uint8 const group = bot->GetActiveTalentGroup();
                for (auto const& known : *bot->GetTalentMap(group))
                    if (bot->HasTalent(known.first, group))
                        if (TalentEntry const* talent = sTalentStore.LookupEntry(known.first))
                            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(talent->SpellID))
                                if (info->SpellName && Normalize(info->SpellName) == t.name)
                                    return 1.0;
                return 0.0;
            }

            case T::SpellTargets:
            {
                BuildEnemies(ctx);
                Unit* center = target ? target : static_cast<Unit*>(bot);
                double count = 0.0;
                for (Unit* enemy : ctx.enemies)
                    if (enemy == center || center->GetDistance(enemy) <= 8.0f)
                        count += 1.0;
                return std::max(1.0, count);
            }
            case T::ActiveEnemies:
                BuildEnemies(ctx);
                return std::max<double>(1.0, double(ctx.enemies.size()));

            case T::TargetHealthPct: return target ? target->GetHealthPct() : 0.0;
            case T::TargetHealth: return target ? double(target->GetHealth()) : 0.0;
            case T::TargetHealthMax: return target ? double(target->GetMaxHealth()) : 0.0;
            case T::TargetTimeToDie:
            {
                if (!target || state.ttdTarget != target->GetGUID() || !state.ttdStart)
                    return 300.0;
                double const seconds = getMSTimeDiff(state.ttdStart, ctx.now) / 1000.0;
                double const lost = double(state.ttdHealth) - double(target->GetHealth());
                if (seconds < 2.0 || lost <= 0.0)
                    return 300.0;
                return double(target->GetHealth()) / (lost / seconds);
            }
            case T::TargetDistance: return target ? bot->GetDistance(target) : 0.0;
            case T::TargetLevel: return target ? double(target->getLevel()) : 0.0;
            case T::TargetCasting: return target && BotCombat::GetInterruptibleCast(target) ? 1.0 : 0.0;

            case T::HealthPct: return bot->GetHealthPct();
            case T::Health: return double(bot->GetHealth());
            case T::HealthMax: return double(bot->GetMaxHealth());
            case T::HealthDeficit: return double(bot->GetMaxHealth() - bot->GetHealth());

            case T::Gcd: return GcdSeconds(bot);
            case T::GcdRemains: return 0.0;
            case T::Time: return state.inCombat ? getMSTimeDiff(state.combatStart, ctx.now) / 1000.0 : 0.0;
            case T::CastTime:
            case T::ExecuteTime:
            {
                uint32 const id = AnySpellId(ctx, OwnSpell(ctx, t));
                SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
                double const cast = info ? info->CalcCastTime(bot) / 1000.0 : 0.0;
                return t.kind == T::ExecuteTime ? std::max(cast, GcdSeconds(bot)) : cast;
            }
            case T::PrevGcd:
                return size_t(t.n) <= state.lastCasts.size() && state.lastCasts[t.n - 1] == t.name ? 1.0 : 0.0;
            case T::Variable:
            {
                auto itr = state.variables.find(t.name);
                return itr != state.variables.end() ? itr->second : 0.0;
            }
            case T::Stealthed:
            {
                static char const* const auras[] = { "stealth", "vanish", "shadowdance", "subterfuge", "prowl" };
                for (char const* aura : auras)
                    if (FindAura(bot, aura, ObjectGuid::Empty))
                        return 1.0;
                return 0.0;
            }
            case T::PetActive: return bot->GetPet() ? 1.0 : 0.0;
            case T::Race: return bot->getRace() == uint8(t.value) ? 1.0 : 0.0;
            case T::IncomingDamage:
            {
                double sum = 0.0;
                for (auto const& entry : state.damage)
                    if (getMSTimeDiff(entry.first, ctx.now) <= t.value)
                        sum += entry.second;
                return sum;
            }
            case T::Level: return bot->getLevel();
            case T::SpellHaste:
            {
                float const haste = bot->GetFloatValue(UNIT_FIELD_MOD_SPELL_HASTE);
                return haste > 0.0f ? haste : 1.0;
            }
            case T::CdMaxCharges:
            {
                uint32 const id = AnySpellId(ctx, OwnSpell(ctx, t));
                SpellInfo const* info = id ? sSpellMgr->GetSpellInfo(id) : nullptr;
                SpellCategoryEntry const* charges = info ? sSpellCategoryStore.LookupEntry(info->Categories.ChargeCategory) : nullptr;
                return charges ? double(charges->MaxCharges) : 1.0;
            }
            case T::DebuffCount:
            {
                if (!target)
                    return 0.0;
                double count = 0.0;
                for (uint32 id : SpellIds(t.name))
                    if (Aura* aura = target->GetAura(id, bot->GetGUID()))
                        if (!aura->GetSpellInfo()->IsPassive())
                            count += 1.0;
                return count;
            }
            case T::RtbBuffs:
            {
                static char const* const buffs[] = { "broadside", "buriedtreasure", "grandmelee", "jollyroger", "sharkinfestedwaters", "truebearing" };
                double count = 0.0;
                for (char const* buff : buffs)
                    if (FindAura(bot, buff, ObjectGuid::Empty))
                        count += 1.0;
                return count;
            }
            case T::PetBuffUp:
            {
                bool const up = bot->GetPet() && FindAura(bot->GetPet(), t.name, ObjectGuid::Empty);
                return (up != t.negate) ? 1.0 : 0.0;
            }
            case T::PetBuffRemains:
            {
                Aura* aura = bot->GetPet() ? FindAura(bot->GetPet(), t.name, ObjectGuid::Empty) : nullptr;
                return aura ? Seconds(aura->GetDuration()) : 0.0;
            }
            case T::FirestarterActive: return target && target->GetHealthPct() > 90.0f ? 1.0 : 0.0;
            case T::BurnPhase: return state.burnPhase ? 1.0 : 0.0;
            case T::BurnPhaseDuration: return state.burnPhase ? getMSTimeDiff(state.burnStart, ctx.now) / 1000.0 : 0.0;
        }
        return 0.0;
    }

    double Eval(Context& ctx, Expr const& e, int index)
    {
        if (index < 0)
            return 1.0;
        Node const& n = e.nodes[index];
        switch (n.op)
        {
            case Op::Num: return n.value;
            case Op::Term: return TermValue(ctx, e.terms[n.term]);
            case Op::Not: return Eval(ctx, e, n.a) == 0.0 ? 1.0 : 0.0;
            case Op::Neg: return -Eval(ctx, e, n.a);
            case Op::Abs: return std::fabs(Eval(ctx, e, n.a));
            case Op::Floor: return std::floor(Eval(ctx, e, n.a));
            case Op::Ceil: return std::ceil(Eval(ctx, e, n.a));
            case Op::And: return Eval(ctx, e, n.a) != 0.0 && Eval(ctx, e, n.b) != 0.0 ? 1.0 : 0.0;
            case Op::Or: return Eval(ctx, e, n.a) != 0.0 || Eval(ctx, e, n.b) != 0.0 ? 1.0 : 0.0;
            case Op::Xor: return (Eval(ctx, e, n.a) != 0.0) != (Eval(ctx, e, n.b) != 0.0) ? 1.0 : 0.0;
            default: break;
        }
        double const a = Eval(ctx, e, n.a);
        double const b = Eval(ctx, e, n.b);
        switch (n.op)
        {
            case Op::Add: return a + b;
            case Op::Sub: return a - b;
            case Op::Mul: return a * b;
            case Op::Div: return b != 0.0 ? a / b : 0.0;
            case Op::Mod: return b != 0.0 ? std::fmod(a, b) : 0.0;
            case Op::Lt: return a < b ? 1.0 : 0.0;
            case Op::Le: return a <= b ? 1.0 : 0.0;
            case Op::Gt: return a > b ? 1.0 : 0.0;
            case Op::Ge: return a >= b ? 1.0 : 0.0;
            case Op::Eq: return std::fabs(a - b) < 1e-6 ? 1.0 : 0.0;
            case Op::Ne: return std::fabs(a - b) >= 1e-6 ? 1.0 : 0.0;
            default: return 0.0;
        }
    }

    bool True(Context& ctx, Expr const& e)
    {
        return e.root < 0 || Eval(ctx, e, e.root) != 0.0;
    }

    void Track(Player* bot, Unit* target, State& state, uint32 now)
    {
        // known spells change with levels and talents: forget the cache every 10 s
        if (getMSTimeDiff(state.knownCheckedAt, now) > 10000)
        {
            state.known.clear();
            state.knownCheckedAt = now;
        }

        bool const inCombat = bot->isInCombat();
        if (inCombat && !state.inCombat)
        {
            state.combatStart = now;
            state.variables.clear();                        // SimC starts every fight with fresh variables
            state.burnPhase = false;
        }
        state.inCombat = inCombat;

        if (target && target->GetGUID() != state.ttdTarget)
        {
            state.ttdTarget = target->GetGUID();
            state.ttdStart = now;
            state.ttdHealth = target->GetHealth();
        }

        uint64 const health = bot->GetHealth();
        if (state.lastHealth > health)
            state.damage.emplace_back(now, uint32(state.lastHealth - health));
        state.lastHealth = health;
        while (!state.damage.empty() && getMSTimeDiff(state.damage.front().first, now) > 10000)
            state.damage.pop_front();
    }

    bool RunList(Context& ctx, std::string const& name, int depth, BotCombat::SpellThrottle& throttle, std::string const& interrupt,
        std::string& fired, bool precombat);

    bool CastAction(Context& ctx, Action const& action, BotCombat::SpellThrottle& throttle, std::string const& interrupt,
        std::string& fired, bool precombat)
    {
        Player* bot = ctx.bot;
        std::string spellName = action.spell;
        if (action.kind == A::SummonPet)
        {
            if (bot->GetPet())
                return false;
            if (bot->getClass() == CLASS_HUNTER)
                spellName = "callpet1";
            else if (!action.petName.empty())
                spellName = Normalize("summon_" + action.petName);
            else
                return false;
        }
        else if (action.kind == A::ApplyPoison)
            spellName = Normalize(action.poison + "_poison");

        if (spellName.empty() || spellName == interrupt)
            return false;
        uint32 const id = KnownSpellId(ctx, spellName);
        if (!id)
            return false;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
        if (!info)
            return false;

        // out of combat: only buffs and summons on the bot itself, and only when not already up
        bool const onSelf = action.self || info->IsPositive() || !info->NeedsExplicitUnitTarget();
        if (precombat)
        {
            // no stealth while following (it slows the bot down); the rest only on the bot itself
            if (spellName == "stealth" || spellName == "prowl")
                return false;
            if (!info->IsPositive() || info->NeedsExplicitUnitTarget())
                return false;
            if (action.kind != A::SummonPet && FindAura(bot, spellName, ObjectGuid::Empty))
                return false;
            // long cooldowns (Metamorphosis, Pillar of Frost...) belong to a pull, not to walking around
            if (info->Cooldowns.RecoveryTime >= 60000 || info->Cooldowns.CategoryRecoveryTime >= 60000)
                return false;
            // a buff whose aura has another name (Totem Mastery: the totems' buffs) would be recast every check
            auto cast = ctx.state->precombatCasts.find(id);
            if (cast != ctx.state->precombatCasts.end())
            {
                int32 const duration = info->GetMaxDuration();
                uint32 const wait = duration > 0 ? uint32(duration) : 120000;
                if (getMSTimeDiff(cast->second, ctx.now) < wait)
                    return false;
            }
        }

        if (action.lineCd > 0.0)
        {
            auto itr = ctx.state->lineCooldowns.find(action.id);
            if (itr != ctx.state->lineCooldowns.end() && getMSTimeDiff(itr->second, ctx.now) < action.lineCd * 1000.0)
                return false;
        }
        if (!action.sync.empty())
        {
            Term sync;
            sync.kind = T::CdReady;
            sync.name = action.sync;
            if (TermValue(ctx, sync) == 0.0)
                return false;
        }

        Unit* castTarget = onSelf ? static_cast<Unit*>(bot) : ctx.target;
        if (!castTarget)
            return false;

        BotCombat::CastOutcome const outcome = BotCombat::BotCast(bot, castTarget, id, throttle, 500);
        if (!outcome)
            return false;

        ctx.state->lastCasts.push_front(spellName);
        if (ctx.state->lastCasts.size() > 3)
            ctx.state->lastCasts.pop_back();
        if (action.lineCd > 0.0)
            ctx.state->lineCooldowns[action.id] = ctx.now;
        if (precombat)
            ctx.state->precombatCasts[id] = ctx.now;
        fired = std::string(info->SpellName ? info->SpellName : "?") + " (" + std::to_string(id) + "), " +
            (action.list.empty() ? std::string("actions") : action.list) + ": " + action.raw;
        return true;
    }

    bool RunList(Context& ctx, std::string const& name, int depth, BotCombat::SpellThrottle& throttle, std::string const& interrupt,
        std::string& fired, bool precombat)
    {
        if (depth > 10)
            return false;
        auto list = ctx.profile->lists.find(name);
        if (list == ctx.profile->lists.end())
            return false;

        for (Action const& action : list->second)
        {
            if (action.kind == A::Skip || action.never)
                continue;
            ctx.action = &action;

            switch (action.kind)
            {
                case A::CallList:
                    if (!precombat && True(ctx, action.cond) && RunList(ctx, action.target, depth + 1, throttle, interrupt, fired, false))
                        return true;
                    break;
                case A::RunList:
                    if (!precombat && True(ctx, action.cond))
                        return RunList(ctx, action.target, depth + 1, throttle, interrupt, fired, false);
                    break;
                case A::Variable:
                {
                    if (!True(ctx, action.cond))
                        break;
                    double& variable = ctx.state->variables[action.target];
                    std::string const& op = action.op;
                    double const value = action.value.root >= 0 ? Eval(ctx, action.value, action.value.root) : 0.0;
                    if (op == "set")                    variable = value;
                    else if (op == "add")               variable += value;
                    else if (op == "sub")               variable -= value;
                    else if (op == "mul")               variable *= value;
                    else if (op == "div")               variable = value != 0.0 ? variable / value : 0.0;
                    else if (op == "min")               variable = std::min(variable, value);
                    else if (op == "max")               variable = std::max(variable, value);
                    else if (op == "reset")             variable = action.defaultValue;
                    else if (op == "floor")             variable = std::floor(variable);
                    else if (op == "ceil")              variable = std::ceil(variable);
                    else if (op == "setif")
                        variable = True(ctx, action.condition) ? value :
                            (action.valueElse.root >= 0 ? Eval(ctx, action.valueElse, action.valueElse.root) : 0.0);
                    break;
                }
                case A::StartBurn:
                    if (True(ctx, action.cond) && !ctx.state->burnPhase)
                    {
                        ctx.state->burnPhase = true;
                        ctx.state->burnStart = ctx.now;
                    }
                    break;
                case A::StopBurn:
                    if (True(ctx, action.cond))
                        ctx.state->burnPhase = false;
                    break;
                case A::Spell:
                case A::SummonPet:
                case A::ApplyPoison:
                    if (True(ctx, action.cond) && CastAction(ctx, action, throttle, interrupt, fired, precombat))
                        return true;
                    break;
                default:
                    break;
            }
        }
        return false;
    }

    std::string ListUnknownSpells(Profile const& profile)
    {
        std::string text;
        uint32 count = 0;
        for (std::string const& name : profile.spellNames)
            if (SpellIds(name).empty())
            {
                if (++count <= 12)
                    text += (text.empty() ? "" : ", ") + name;
            }
        if (count > 12)
            text += ", ... (" + std::to_string(count) + ")";
        return text;
    }

    void Load(std::string directory)
    {
        for (BuiltInProfile const* builtIn = BuiltInProfiles; builtIn->specId; ++builtIn)
        {
            Profile& profile = profiles[builtIn->specId];
            profile.specId = builtIn->specId;
            profile.name = builtIn->name;
            std::vector<std::string> lines;
            lines.push_back(std::string("talents=") + builtIn->talents);
            for (char const* const* line = builtIn->lines; *line; ++line)
                lines.push_back(*line);
            ParseProfile(profile, lines);
        }

        uint32 overrides = 0;
        std::error_code error;
        if (!directory.empty() && std::filesystem::is_directory(directory, error))
        {
            for (auto const& entry : std::filesystem::directory_iterator(directory, error))
            {
                if (entry.path().extension() != ".simc")
                    continue;
                std::ifstream file(entry.path());
                std::vector<std::string> lines;
                std::string line;
                while (std::getline(file, line))
                {
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                        line.pop_back();
                    lines.push_back(line);
                }
                uint32 const spec = SpecOfFile(lines);
                if (!spec)
                {
                    TC_LOG_ERROR("server.loading", "[mod-playerbots] Rotation file %s: no spec_id= or class / spec= line, skipped.",
                        entry.path().filename().string().c_str());
                    continue;
                }
                Profile profile;
                profile.specId = spec;
                profile.name = entry.path().stem().string();
                ParseProfile(profile, lines);
                profiles[spec] = profile;
                ++overrides;
            }
        }

        BuildSpellIndex();

        for (auto const& pair : profiles)
        {
            Profile const& profile = pair.second;
            uint32 actions = 0;
            for (auto const& list : profile.lists)
                actions += uint32(list.second.size());
            TC_LOG_INFO("server.loading", "[mod-playerbots] Rotation %s (spec %u): %u lists, %u actions, talents %s.",
                profile.name.c_str(), profile.specId, uint32(profile.lists.size()), actions, profile.talents.c_str());
            std::string const spells = ListUnknownSpells(profile);
            if (!spells.empty())
                TC_LOG_INFO("server.loading", "[mod-playerbots] Rotation %s: no spell of these names (skipped): %s.",
                    profile.name.c_str(), spells.c_str());
            if (!profile.unknownTerms.empty())
            {
                std::string terms;
                uint32 count = 0;
                for (std::string const& term : profile.unknownTerms)
                    if (++count <= 15)
                        terms += (terms.empty() ? "" : ", ") + term;
                if (count > 15)
                    terms += ", ... (" + std::to_string(count) + ")";
                TC_LOG_INFO("server.loading", "[mod-playerbots] Rotation %s: terms read as 0: %s.", profile.name.c_str(), terms.c_str());
            }
        }
        TC_LOG_INFO("server.loading", "[mod-playerbots] Rotations: %u SimC profiles (%u from %s).", uint32(profiles.size()), overrides,
            directory.empty() ? "no folder" : directory.c_str());
    }
}

std::string Normalize(std::string const& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
        if (std::isalnum((unsigned char)c))
            out += char(std::tolower((unsigned char)c));
    return out;
}

void Initialize(std::string const& directory)
{
    std::call_once(initOnce, Load, directory);
}

bool HasProfile(uint32 specId)
{
    return profiles.count(specId) != 0;
}

char const* TalentString(uint32 specId)
{
    auto itr = profiles.find(specId);
    return itr != profiles.end() && !itr->second.talents.empty() ? itr->second.talents.c_str() : nullptr;
}

bool Run(Player* bot, Unit* target, State& state, BotCombat::SpellThrottle& throttle, std::string const& interruptSpell,
    std::string& fired)
{
    auto itr = profiles.find(bot->GetSpecializationId());
    if (itr == profiles.end())
        return false;

    Context ctx;
    ctx.bot = bot;
    ctx.target = target;
    ctx.state = &state;
    ctx.profile = &itr->second;
    ctx.family = BotRotations::ClassFamily(bot->getClass());
    ctx.now = getMSTime();
    Track(bot, target, state, ctx.now);

    if (!target)
        return RunList(ctx, "precombat", 0, throttle, interruptSpell, fired, true);
    return RunList(ctx, "", 0, throttle, interruptSpell, fired, false);
}
}
