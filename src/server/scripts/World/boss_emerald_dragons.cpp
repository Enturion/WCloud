/*
* Copyright (C) 2008-2011 WCloudCore <http://www.wcloudcore.org/>
* Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "Spell.h"
#include "SpellAuraEffects.h"

//
//  Emerald Dragon NPCs and IDs (kept here for reference)
//

enum EmeraldDragonNPC
{
    NPC_DREAM_FOG                   = 15224,
    DRAGON_YSONDRE                  = 14887,
    DRAGON_LETHON                   = 14888,
    DRAGON_EMERISS                  = 14889,
    DRAGON_TAERAR                   = 14890,
};

//
// Emerald Dragon Spells (used for the dragons)
//

enum EmeraldDragonSpells
{
    SPELL_TAIL_SWEEP                = 15847,    // tail sweep - slap everything behind dragon (2 seconds interval)
    SPELL_SUMMON_PLAYER             = 24776,    // teleport highest threat player in front of dragon if wandering off
    SPELL_DREAM_FOG                 = 24777,    // auraspell for Dream Fog NPC (15224)
    SPELL_SLEEP                     = 24778,    // sleep triggerspell (used for Dream Fog)
    SPELL_SEEPING_FOG_LEFT          = 24813,    // dream fog - summon left
    SPELL_SEEPING_FOG_RIGHT         = 24814,    // dream fog - summon right
    SPELL_NOXIOUS_BREATH            = 24818,
    SPELL_MARK_OF_NATURE            = 25040,    // Mark of Nature trigger (applied on target death - 15 minutes of being suspectible to Aura Of Nature)
    SPELL_MARK_OF_NATURE_AURA       = 25041,    // Mark of Nature (passive marker-test, ticks every 10 seconds from boss, triggers spellID 25042 (scripted)
    SPELL_AURA_OF_NATURE            = 25043,    // Stun for 2 minutes (used when SPELL_MARK_OF_NATURE exists on the target)
};

//
// Emerald Dragon Eventlists (shared and specials)
//

enum Events
{
    // General events for all dragons
    EVENT_SEEPING_FOG = 1,
    EVENT_NOXIOUS_BREATH,
    EVENT_TAIL_SWEEP,

    // Ysondre
    EVENT_LIGHTNING_WAVE,
    EVENT_SUMMON_DRUID_SPIRITS,

    // Lethon
    EVENT_SHADOW_BOLT_WHIRL,

    // Emeriss
    EVENT_VOLATILE_INFECTION,
    EVENT_CORRUPTION_OF_EARTH,

    // Taerar
    EVENT_ARCANE_BLAST,
    EVENT_BELLOWING_ROAR,
};

/*
 * ---
 * --- Emerald Dragons : Base AI-structure used for all the Emerald dragons
 * ---
 */

struct emerald_dragonAI : public WorldBossAI
{
    emerald_dragonAI(Creature* creature) : WorldBossAI(creature)
    {
//        me->m_CombatDistance = 12.0f;
//        me->m_SightDistance  = 60.0f;
    }

    void Reset()
    {
        _Reset();
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE|UNIT_FLAG_NON_ATTACKABLE);
        me->SetReactState(REACT_AGGRESSIVE);
        DoCast(me, SPELL_MARK_OF_NATURE_AURA, true);
        events.ScheduleEvent(EVENT_TAIL_SWEEP, 4000);
        events.ScheduleEvent(EVENT_NOXIOUS_BREATH, urand(7500, 15000));
        events.ScheduleEvent(EVENT_SEEPING_FOG, urand(12500, 20000));
    }

    // Target killed during encounter, mark them as suspectible for Aura Of Nature
    void KilledUnit(Unit* who)
    {
        who->CastSpell(who, SPELL_MARK_OF_NATURE, true);
    }

    // Execute and reschedule base events shared between all Emerald Dragons
    void ExecuteEvent(uint32 const eventId)
    {
        switch (eventId)
        {
            case EVENT_SEEPING_FOG:
                // Seeping Fog appears only as "pairs", and only ONE pair at any given time!
                // Despawntime is 2 minutes, so reschedule it for new cast after 2 minutes + a minor "random time" (30 seconds at max)
                DoCast(me, SPELL_SEEPING_FOG_LEFT, true);
                DoCast(me, SPELL_SEEPING_FOG_RIGHT, true);
                events.ScheduleEvent(EVENT_SEEPING_FOG, urand(120000, 150000));
                break;
            case EVENT_NOXIOUS_BREATH:
                // Noxious Breath is cast on random intervals, no less than 7.5 seconds between
                DoCast(me, SPELL_NOXIOUS_BREATH);
                events.ScheduleEvent(EVENT_NOXIOUS_BREATH, urand(7500, 15000));
                break;
            case EVENT_TAIL_SWEEP:
                // Tail Sweep is cast every two seconds, no matter what goes on in front of the dragon
                DoCast(me, SPELL_TAIL_SWEEP);
                events.ScheduleEvent(EVENT_TAIL_SWEEP, 2000);
                break;
        }
    }

    void UpdateAI(uint32 const diff)
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STAT_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
            ExecuteEvent(eventId);

        if (Unit* target = SelectTarget(SELECT_TARGET_TOPAGGRO, 0, -50.0f, true))
            DoCast(target, SPELL_SUMMON_PLAYER);

        DoMeleeAttackIfReady();
    }
};

/*
 * --- NPC: Dream Fog
 */

class npc_dream_fog : public CreatureScript
{
    public:
        npc_dream_fog() : CreatureScript("npc_dream_fog") { }

        struct npc_dream_fogAI : public ScriptedAI
        {
            npc_dream_fogAI(Creature* creature) : ScriptedAI(creature)
            {
            }

            void Reset()
            {
                _roamTimer = 0;
            }

            void UpdateAI(uint32 const diff)
            {
                if (!UpdateVictim())
                    return;

                if (!_roamTimer)
                {
                    // Chase target, but don't attack - otherwise just roam around
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true))
                    {
                        _roamTimer = urand(15000, 30000);
                        me->GetMotionMaster()->Clear(false);
                        me->GetMotionMaster()->MoveChase(target, 0.2f);
                    }
                    else
                    {
                        _roamTimer = 2500;
                        me->GetMotionMaster()->Clear(false);
                        me->GetMotionMaster()->MoveRandom(25.0f);
                    }
                    // Seeping fog movement is slow enough for a player to be able to walk backwards and still outpace it
                    me->AddUnitMovementFlag(MOVEMENTFLAG_WALKING);
                    me->SetSpeed(MOVE_WALK, 0.75f);
                }
                else
                    _roamTimer -= diff;
            }

        private:
            uint32 _roamTimer;
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new npc_dream_fogAI(creature);
        }
};

/*
 * --- Spell: Dream Fog
 */

class DreamFogTargetSelector
{
    public:
        DreamFogTargetSelector() { }

        bool operator()(Unit* unit)
        {
            return unit->HasAura(SPELL_SLEEP);
        }
};

class spell_dream_fog_sleep : public SpellScriptLoader
{
    public:
        spell_dream_fog_sleep() : SpellScriptLoader("spell_dream_fog_sleep") { }

        class spell_dream_fog_sleep_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_dream_fog_sleep_SpellScript);

            void FilterTargets(std::list<Unit*>& unitList)
            {
                unitList.remove_if (DreamFogTargetSelector());
            }

            void Register()
            {
                OnUnitTargetSelect += SpellUnitTargetFn(spell_dream_fog_sleep_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
            }
        };

        SpellScript* GetSpellScript() const
        {
            return new spell_dream_fog_sleep_SpellScript();
        }
};

/*
 * --- Spell: Mark of Nature
 */

class MarkOfNatureTargetSelector
{
    public:
        MarkOfNatureTargetSelector() { }

        bool operator()(Unit* unit)
        {
            // return anyone that isn't tagged or already under the influence of Aura of Nature
            return !(unit->HasAura(SPELL_MARK_OF_NATURE) && !unit->HasAura(SPELL_AURA_OF_NATURE));
        }
};

class spell_mark_of_nature : public SpellScriptLoader
{
    public:
        spell_mark_of_nature() : SpellScriptLoader("spell_mark_of_nature") { }

        class spell_mark_of_nature_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_mark_of_nature_SpellScript);

            bool Validate(SpellInfo const* /*spellInfo*/)
            {
                if (!sSpellMgr->GetSpellInfo(SPELL_MARK_OF_NATURE))
                    return false;
                if (!sSpellMgr->GetSpellInfo(SPELL_AURA_OF_NATURE))
                    return false;
                return true;
            }

            void FilterTargets(std::list<Unit*>& unitList)
            {
                unitList.remove_if (MarkOfNatureTargetSelector());
            }

            void HandleEffect(SpellEffIndex effIndex)
            {
                PreventHitDefaultEffect(effIndex);

                if (GetHitUnit())
                    GetHitUnit()->CastSpell(GetHitUnit(), SPELL_AURA_OF_NATURE, true);
            }

            void Register()
            {
                OnUnitTargetSelect += SpellUnitTargetFn(spell_mark_of_nature_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
                OnEffectHitTarget += SpellEffectFn(spell_mark_of_nature_SpellScript::HandleEffect, EFFECT_0, SPELL_EFFECT_APPLY_AURA);
            }
        };

        SpellScript* GetSpellScript() const
        {
            return new spell_mark_of_nature_SpellScript();
        }
};

/*
 * ---
 * --- Dragonspecific scripts and handling: YSONDRE
 * ---
 */

enum YsondreNPC
{
    NPC_DEMENTED_DRUID              = 15260,
};

enum YsondreTexts
{
    SAY_YSONDRE_AGGRO               = 0,
    SAY_YSONDRE_SUMMON_DRUIDS       = 1,
};

enum YsondreSpells
{
    SPELL_LIGHTNING_WAVE            = 24819,
    SPELL_SUMMON_DRUID_SPIRITS      = 24795,
};

class boss_ysondre : public CreatureScript
{
    public:
        boss_ysondre() : CreatureScript("boss_ysondre") { }

        struct boss_ysondreAI : public emerald_dragonAI
        {
            boss_ysondreAI(Creature* creature) : emerald_dragonAI(creature)
            {
            }

            void Reset()
            {
                _stage = 1;
                _Reset();
                emerald_dragonAI::Reset();
                events.ScheduleEvent(EVENT_LIGHTNING_WAVE, 12000);
            }

            void EnterCombat(Unit* who)
            {
                Talk(SAY_YSONDRE_AGGRO);
                WorldBossAI::EnterCombat(who);
            }

            // Summon druid spirits on 75%, 50% and 25% health
            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/)
            {
                if (!HealthAbovePct(100 - 25 * _stage))
                {
                    Talk(SAY_YSONDRE_SUMMON_DRUIDS);

                    for (uint8 i = 0 ; i < 10 ; ++i)
                        DoCast(me, SPELL_SUMMON_DRUID_SPIRITS, true);
                    ++_stage;
                }
            }

            void ExecuteEvent(uint32 const eventId)
            {
                switch (eventId)
                {
                    case EVENT_LIGHTNING_WAVE:
                        DoCastVictim(SPELL_LIGHTNING_WAVE);
                        events.ScheduleEvent(EVENT_LIGHTNING_WAVE, urand(10000, 20000));
                        break;
                    default:
                        emerald_dragonAI::ExecuteEvent(eventId);
                        break;
                }
            }

        private:
            uint8   _stage;
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new boss_ysondreAI(creature);
        }
};

/*
 * ---
 * --- Dragonspecific scripts and handling: LETHON
 * ---
 *
 * TODO:
 * - NPC helper for spirit shades(?)
 *   - Spirit shade NPC moves towards Lethon and heals him if close enough (each shade heals for 15000 HP)
 * - Spell: Shadow bolt whirl casts needs custom handling (spellscript)
 */

enum LethonTexts
{
    SAY_LETHON_AGGRO                = 0,
    SAY_LETHON_DRAW_SPIRIT          = 1,
};

enum LethonSpells
{
    SPELL_DRAW_SPIRIT               = 24811,
    SPELL_SHADOW_BOLT_WHIRL         = 24834,
    SPELL_SPIRIT_SHADE_VISUAL       = 24908,
};

class boss_lethon : public CreatureScript
{
    public:
        boss_lethon() : CreatureScript("boss_lethon") { }

        struct boss_lethonAI : public emerald_dragonAI
        {
            boss_lethonAI(Creature* creature) : emerald_dragonAI(creature)
            {
            }

            void Reset()
            {
                _stage = 1;
                _Reset();
                emerald_dragonAI::Reset();
                events.ScheduleEvent(EVENT_SHADOW_BOLT_WHIRL, 10000);
            }

            void EnterCombat(Unit* who)
            {
                Talk(SAY_LETHON_AGGRO);
                WorldBossAI::EnterCombat(who);
            }

            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/)
            {
                if (!HealthAbovePct(100 - 25 * _stage))
                {
                    Talk(SAY_LETHON_DRAW_SPIRIT);
                    DoCast(me, SPELL_DRAW_SPIRIT);
                    ++_stage;
                }
            }

            void ExecuteEvent(uint32 const eventId)
            {
                switch (eventId)
                {
                    case EVENT_SHADOW_BOLT_WHIRL:
                        DoCast(me, SPELL_SHADOW_BOLT_WHIRL, true);
                        events.ScheduleEvent(EVENT_SHADOW_BOLT_WHIRL, urand(15000, 30000));
                        break;
                    default:
                        emerald_dragonAI::ExecuteEvent(eventId);
                        break;
                }
            }

        private:
            uint8   _stage;
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new boss_lethonAI(creature);
        }
};

/*
 * ---
 * --- Dragonspecific scripts and handling: EMERISS
 * ---
 */

enum EmerissTexts
{
    SAY_EMERISS_AGGRO               = 0,
    SAY_EMERISS_CAST_CORRUPTION     = 1,
};

enum EmerissSpells
{
    SPELL_PUTRID_MUSHROOM           = 24904,
    SPELL_CORRUPTION_OF_EARTH       = 24910,
    SPELL_VOLATILE_INFECTION        = 24928,
};

class boss_emeriss : public CreatureScript
{
    public:
        boss_emeriss() : CreatureScript("boss_emeriss") { }

        struct boss_emerissAI : public emerald_dragonAI
        {
            boss_emerissAI(Creature* creature) : emerald_dragonAI(creature)
            {
            }

            void Reset()
            {
                _stage = 1;
                _Reset();
                emerald_dragonAI::Reset();
                events.ScheduleEvent(EVENT_VOLATILE_INFECTION, 12000);
            }

            void KilledUnit(Unit* who)
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    DoCast(who, SPELL_PUTRID_MUSHROOM, true);
                emerald_dragonAI::KilledUnit(who);
            }

            void EnterCombat(Unit* who)
            {
                Talk(SAY_EMERISS_AGGRO);
                WorldBossAI::EnterCombat(who);
            }

            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/)
            {
                if (!HealthAbovePct(100 - 25 * _stage))
                {
                    Talk(SAY_EMERISS_CAST_CORRUPTION);
                    DoCast(me, SPELL_CORRUPTION_OF_EARTH, true);
                    ++_stage;
                }
            }

            void ExecuteEvent(uint32 const eventId)
            {
                switch (eventId)
                {
                    case EVENT_VOLATILE_INFECTION:
                        DoCastVictim(SPELL_VOLATILE_INFECTION);
                        events.ScheduleEvent(EVENT_VOLATILE_INFECTION, 120000);
                        break;
                    default:
                        emerald_dragonAI::ExecuteEvent(eventId);
                        break;
                }
            }

        private:
            uint8   _stage;
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new boss_emerissAI(creature);
        }
};

/*
 * ---
 * --- Dragonspecific scripts and handling: TAERAR
 * ---
 */

enum TaerarTexts
{
    SAY_TAERAR_AGGRO                = 0,
    SAY_TAERAR_SUMMON_SHADES        = 1,
};

enum TaerarSpells
{
    SPELL_BELLOWING_ROAR            = 22686,
    SPELL_SHADE                     = 24313,
    SPELL_SUMMON_SHADE_1            = 24841,
    SPELL_SUMMON_SHADE_2            = 24842,
    SPELL_SUMMON_SHADE_3            = 24843,
    SPELL_ARCANE_BLAST              = 24857,
};

uint32 const TaerarShadeSpells[] =
{
    SPELL_SUMMON_SHADE_1, SPELL_SUMMON_SHADE_2, SPELL_SUMMON_SHADE_3
};

class boss_taerar : public CreatureScript
{
    public:
        boss_taerar() : CreatureScript("boss_taerar") { }

        struct boss_taerarAI : public emerald_dragonAI
        {
            boss_taerarAI(Creature* creature) : emerald_dragonAI(creature)
            {
                _stage = 1;
                _shades = 0;
                _banished = false;
                _banishedTimer = 0;
            }

            void Reset()
            {
                me->RemoveAurasDueToSpell(SPELL_SHADE);
                _stage = 1;

                _shades = 0;
                _banished = false;
                _banishedTimer = 0;

                _Reset();
                emerald_dragonAI::Reset();
                events.ScheduleEvent(EVENT_ARCANE_BLAST, 12000);
                events.ScheduleEvent(EVENT_BELLOWING_ROAR, 30000);
            }

            void EnterCombat(Unit* who)
            {
                Talk(SAY_TAERAR_AGGRO);
                emerald_dragonAI::EnterCombat(who);
            }

            void SummonedCreatureDies(Creature* /*summon*/, Unit* /*killer*/)
            {
                --_shades;
            }

            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/)
            {
                // At 75, 50 or 25 percent health, we need to activate the shades and go "banished"
                // Note: _stage holds the amount of times they have been summoned
                if (!_banished && !HealthAbovePct(100 - 25 * _stage))
                {
                    _banished = true;
                    _banishedTimer = 60000;

                    me->InterruptNonMeleeSpells(false);
                    DoStopAttack();

                    Talk(SAY_TAERAR_SUMMON_SHADES);

                    uint32 count = sizeof(TaerarShadeSpells) / sizeof(uint32);
                    for (uint32 i = 0; i < count; ++i)
                        DoCastVictim(TaerarShadeSpells[i], true);
                    _shades += count;

                    DoCast(SPELL_SHADE);
                    me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE|UNIT_FLAG_NON_ATTACKABLE);
                    me->SetReactState(REACT_PASSIVE);

                    ++_stage;
                }
            }

            void ExecuteEvent(uint32 const eventId)
            {
                switch (eventId)
                {
                    case EVENT_ARCANE_BLAST:
                        DoCast(SPELL_ARCANE_BLAST);
                        events.ScheduleEvent(EVENT_ARCANE_BLAST, urand(7000, 12000));
                        break;
                    case EVENT_BELLOWING_ROAR:
                        DoCast(SPELL_BELLOWING_ROAR);
                        events.ScheduleEvent(EVENT_BELLOWING_ROAR, urand(20000, 30000));
                        break;
                    default:
                        emerald_dragonAI::ExecuteEvent(eventId);
                        break;
                }
            }

            void UpdateAI(uint32 const diff)
            {
                if (!me->isInCombat())
                    return;

                if (_banished)
                {
                    // If all three shades are dead, OR it has taken too long, end the current event and get Taerar back into business
                    if (_banishedTimer <= diff || !_shades)
                    {
                        _banished = false;

                        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE|UNIT_FLAG_NON_ATTACKABLE);
                        me->RemoveAurasDueToSpell(SPELL_SHADE);
                        me->SetReactState(REACT_AGGRESSIVE);
                    }
                    // _banishtimer has not expired, and we still have active shades:
                    else
                        _banishedTimer -= diff;

                    // Update the events before we return (handled under emerald_dragonAI::UpdateAI(diff); if we're not inside this check)
                    events.Update(diff);

                    return;
                }

                emerald_dragonAI::UpdateAI(diff);
            }

        private:
            bool    _banished;                              // used for shades activation testing
            uint32  _banishedTimer;                         // counter for banishment timeout
            uint8   _shades;                                // keep track of how many shades are dead
            uint8   _stage;                                 // check which "shade phase" we're at (75-50-25 percentage counters)
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new boss_taerarAI(creature);
        }
};

void AddSC_emerald_dragons()
{
    // helper NPC scripts
    new npc_dream_fog();

    // dragon spellscripts
    new spell_dream_fog_sleep();
    new spell_mark_of_nature();

    // dragons
    new boss_ysondre();
    new boss_taerar();
    new boss_emeriss();
    new boss_lethon();
};