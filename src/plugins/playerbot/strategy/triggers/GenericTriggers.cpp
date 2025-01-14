#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "GenericTriggers.h"
#include "../../LootObjectStack.h"
#include "../../PlayerbotAIConfig.h"

using namespace ai;

bool LowManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.lowMana;
}

bool MediumManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana;
}


bool RageAvailable::IsActive()
{
    return AI_VALUE2(uint8, "rage", "self target") >= amount;
}

bool EnergyAvailable::IsActive()
{
	return AI_VALUE2(uint8, "energy", "self target") >= amount;
}

bool ComboPointsAvailableTrigger::IsActive()
{
    return AI_VALUE2(uint8, "combo", "current target") >= amount;
}

bool LoseAggroTrigger::IsActive()
{
    return !AI_VALUE2(bool, "has aggro", "current target");
}

bool HasAggroTrigger::IsActive()
{
    return AI_VALUE2(bool, "has aggro", "current target");
}

bool PanicTrigger::IsActive()
{
    return AI_VALUE2(uint8, "health", "self target") < sPlayerbotAIConfig.criticalHealth &&
		(!AI_VALUE2(bool, "has mana", "self target") || AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.lowMana);
}

bool BuffTrigger::IsActive()
{
    Unit* target = GetTarget();
	return SpellTrigger::IsActive() &&
		!ai->HasAura(spell, target) &&
		(!AI_VALUE2(bool, "has mana", "self target") || AI_VALUE2(uint8, "mana", "self target") > sPlayerbotAIConfig.lowMana);
}

Value<Unit*>* BuffOnPartyTrigger::GetTargetValue()
{
	return context->GetValue<Unit*>("party member without aura", spell);
}

Value<Unit*>* BuffOnMainTankTrigger::GetTargetValue()
{
	return context->GetValue<Unit*>("main tank", spell);
}

Value<Unit*>* DebuffOnAttackerTrigger::GetTargetValue()
{
	return context->GetValue<Unit*>("attacker without aura", spell);
}

Value<Unit*>* DebuffFromBotOnAttackerTrigger::GetTargetValue()
{
	return context->GetValue<Unit*>("attacker without aura from bot", spell);
}

bool NoAttackersTrigger::IsActive()
{
    return !AI_VALUE(Unit*, "current target") && AI_VALUE(uint8, "attacker count") > 0;
}

bool InvalidTargetTrigger::IsActive()
{
    return AI_VALUE2(bool, "invalid target", "current target");
}

bool NoValidTargetTrigger::IsActive()
{
	return AI_VALUE(uint8, "attacker count") == 0;
}

bool NoTargetTrigger::IsActive()
{
	return !AI_VALUE(Unit*, "current target");
}

bool MyAttackerCountTrigger::IsActive()
{
    return AI_VALUE(uint8, "my attacker count") >= amount;
}

bool AoeTrigger::IsActive()
{
	Unit* current_target = AI_VALUE(Unit*, "current target");
	if (!current_target) {
		return false;
	}
	list<ObjectGuid> attackers = context->GetValue<list<ObjectGuid> >("attackers")->Get();
	int attackers_count = 0;
    for (list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || !unit->IsAlive())
            continue;

        if (unit->GetExactDist2d(current_target) <= range) {
			attackers_count++;
		}
    }
    return attackers_count >= amount;
}

bool DebuffTrigger::IsActive()
{
	return BuffTrigger::IsActive() && GetTarget() && GetTarget()->GetHealthPct() > life_bound;
}

bool DebuffFromBotTrigger::IsActive()
{
	return !ai->HasAuraFromBot(getName(), GetTarget()) && GetTarget() && GetTarget()->GetHealthPct() > life_bound;
}
bool SpellTrigger::IsActive()
{
	return GetTarget();
}

bool SpellCanBeCastTrigger::IsActive()
{
	Unit* target = GetTarget();
	return target && ai->CanCastSpell(spell, target);
}

bool RandomTrigger::IsActive()
{
	if (time(0) - lastCheck < sPlayerbotAIConfig.maxWaitForMove / 1000)
		return false;

		lastCheck = time(0);
	int k = (int)(probability / sPlayerbotAIConfig.randomChangeMultiplier);
	if (k < 1) k = 1;
	return (rand() % k) == 0;
}

bool AndTrigger::IsActive()
{
    return ls->IsActive() && rs->IsActive();
}

string AndTrigger::getName()
{
    std::string name(ls->getName());
    name = name + " and ";
    name = name + rs->getName();
    return name;
}

bool BoostTrigger::IsActive()
{
	return BuffTrigger::IsActive() && AI_VALUE(uint8, "balance") <= balance;
}

bool SnareTargetTrigger::IsActive()
{
	Unit* target = GetTarget();
	return DebuffTrigger::IsActive() && AI_VALUE2(bool, "moving", "current target") && !ai->HasAura(spell, target);
}

bool ItemCountTrigger::IsActive()
{
	return AI_VALUE2(uint8, "item count", item) < count;
}

bool InterruptSpellTrigger::IsActive()
{
	return SpellTrigger::IsActive() && ai->IsInterruptableSpellCasting(GetTarget(), getName());
}

bool HasAuraTrigger::IsActive()
{
	return ai->HasAuraWithDuration(getName(), GetTarget());
}

bool HasNotAuraTrigger::IsActive()
{
	return !ai->HasAuraWithDuration(getName(), GetTarget());
}

bool HasAuraFromBotTrigger::IsActive()
{
	/// TODO: change HasAura to HasAuraFromBot (distinguide positive and negative? )
	return ai->HasAuraFromBot(getName(), GetTarget());
}

bool HasAuraStackTrigger::IsActive()
{
	Aura *aura = ai->GetAura(getName(), GetTarget());
	// sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "HasAuraStackTrigger::IsActive %s %d", getName(), aura ? aura->GetStackAmount() : -1);
	return ai->HasAura(getName(), GetTarget()) && aura && aura->GetStackAmount() >= stack;
}

bool TankAoeTrigger::IsActive()
{
    if (!AI_VALUE(uint8, "attacker count"))
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget)
        return true;

	if (currentTarget->GetVictim() && currentTarget->GetVictim() != AI_VALUE(Unit*, "self target") && currentTarget->GetVictim() != AI_VALUE(Unit*, "main tank")) {
		return false;
	}
	
    Unit* tankTarget = AI_VALUE(Unit*, "tank target");
    if (!tankTarget || currentTarget == tankTarget)
        return false;

	// bot->Yell("tank target: " + tankTarget->GetName() + " current target: " + currentTarget->GetName(), LANG_UNIVERSAL);
	
    return true;
}

bool IsBehindTargetTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    return target && AI_VALUE2(bool, "behind", "current target");
}

bool IsNotFacingTargetTrigger::IsActive()
{
    return !AI_VALUE2(bool, "facing", "current target");
}

bool HasCcTargetTrigger::IsActive()
{
    return AI_VALUE(uint8, "attacker count") > 2 && AI_VALUE2(Unit*, "cc target", getName()) &&
        !AI_VALUE2(Unit*, "current cc target", getName());
}

bool NoMovementTrigger::IsActive()
{
	return !AI_VALUE2(bool, "moving", "self target");
}

bool NoPossibleTargetsTrigger::IsActive()
{
    list<ObjectGuid> targets = AI_VALUE(list<ObjectGuid>, "possible targets");
    return !targets.size();
}

bool NotLeastHpTargetActiveTrigger::IsActive()
{
    Unit* leastHp = AI_VALUE(Unit*, "least hp target");
    Unit* target = AI_VALUE(Unit*, "current target");
    return leastHp && target != leastHp;
}

bool EnemyPlayerIsAttacking::IsActive()
{
    Unit* enemyPlayer = AI_VALUE(Unit*, "enemy player target");
    Unit* target = AI_VALUE(Unit*, "current target");
    return enemyPlayer && target != enemyPlayer;
}

bool IsSwimmingTrigger::IsActive()
{
    return AI_VALUE2(bool, "swimming", "self target");
}

bool HasNearestAddsTrigger::IsActive()
{
    list<ObjectGuid> targets = AI_VALUE(list<ObjectGuid>, "nearest adds");
    return targets.size();
}

bool HasItemForSpellTrigger::IsActive()
{
	string spell = getName();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    return spellId && AI_VALUE2(Item*, "item for spell", spellId);
}

bool PlayerHasNoFlag::IsActive()
{
	if (ai->GetBot()->InBattleground())
	{
		if (ai->GetBot()->GetBattlegroundTypeId() == BattlegroundTypeId::BATTLEGROUND_WS)
		{
			BattlegroundWS *bg = (BattlegroundWS*)ai->GetBot()->GetBattleground();
			if (!(bg->GetFlagState(bg->GetOtherTeam(bot->GetTeam())) == BG_WS_FLAG_STATE_ON_PLAYER))
				return false;
			if (bot->GetGUID() == bg->GetFlagPickerGUID(bg->GetOtherTeam(bot->GetTeam()) == ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE)) //flag-Carrier, bring it home
			{
				return false;
			}
		}
	}
	return true;
}

bool PlayerIsInBattleground::IsActive()
{
	return ai->GetBot()->InBattleground();
}

bool PlayerIsInBattlegroundWithoutFlag::IsActive()
{
	if (ai->GetBot()->InBattleground())
	{
		if (ai->GetBot()->GetBattlegroundTypeId() == BattlegroundTypeId::BATTLEGROUND_WS)
		{
			BattlegroundWS *bg = (BattlegroundWS*)ai->GetBot()->GetBattleground();
			if (!(bg->GetFlagState(bg->GetOtherTeam(bot->GetTeam())) == BG_WS_FLAG_STATE_ON_PLAYER))
				return true;
			if (bot->GetGUID() == bg->GetFlagPickerGUID(bg->GetOtherTeam(bot->GetTeam()) == ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE)) //flag-Carrier, bring it home
			{
				return false;
			}
		}
		return true;
	}
	return true;
}

bool TargetChangedTrigger::IsActive()
{
    Unit* oldTarget = context->GetValue<Unit*>("old target")->Get();
    Unit* target = context->GetValue<Unit*>("current target")->Get();
    return target && oldTarget != target;
}

Value<Unit*>* InterruptEnemyHealerTrigger::GetTargetValue()
{
    return context->GetValue<Unit*>("enemy healer target", spell);
}

bool AvoidAOESpellTrigger::IsActive()
{
	Aura* target = AI_VALUE(Aura*, "aoe aura to avoid");
	// bot->Yell("AURA AOE 检测触发:" + string(target->GetSpellInfo()->SpellName[0]), LANG_UNIVERSAL);
	return target;
}