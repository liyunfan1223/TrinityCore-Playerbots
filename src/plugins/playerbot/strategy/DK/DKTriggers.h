#pragma once
#include "../triggers/GenericTriggers.h"

namespace ai
{
    
    BUFF_TRIGGER(HornOfWinterTrigger, "horn of winter", "horn of winter")
	BUFF_TRIGGER(BoneShieldTrigger, "bone shield", "bone shield")
	BUFF_TRIGGER(ImprovedIcyTalonsTrigger, "improved icy talons", "improved icy talons")
	DEBUFF_FROM_BOT_TRIGGER(PlagueStrikeDebuffTrigger, "blood plague", "plague strike")
	DEBUFF_FROM_BOT_TRIGGER(IcyTouchDebuffTrigger, "frost fever", "icy touch")

	class PlagueStrikeDebuffOnAttackerTrigger : public DebuffFromBotOnAttackerTrigger
	{
	public:
		PlagueStrikeDebuffOnAttackerTrigger(PlayerbotAI* ai) : DebuffFromBotOnAttackerTrigger(ai, "blood plague") {}
	};
	
	class IcyTouchDebuffOnAttackerTrigger : public DebuffFromBotOnAttackerTrigger
	{
	public:
		IcyTouchDebuffOnAttackerTrigger(PlayerbotAI* ai) : DebuffFromBotOnAttackerTrigger(ai, "frost fever") {}
	};

    class DKPresenceTrigger : public BuffTrigger {
    public:
        DKPresenceTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "blood presence") {}
        virtual bool IsActive();
    };

	class BloodTapTrigger : public BuffTrigger {
	public:
		BloodTapTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "blood tap") {}
	};

	class RaiseDeadTrigger : public BuffTrigger {
	public:
		RaiseDeadTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "raise dead") {}
	};


	class RuneStrikeTrigger : public SpellCanBeCastTrigger {
	public:
		RuneStrikeTrigger(PlayerbotAI* ai) : SpellCanBeCastTrigger(ai, "rune strike") {}
	};

	class DeathCoilTrigger : public SpellCanBeCastTrigger {
	public:
		DeathCoilTrigger(PlayerbotAI* ai) : SpellCanBeCastTrigger(ai, "death coil") {}
	};

	// class PestilenceTrigger : public DebuffTrigger {
	// public:
	// 	PestilenceTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "pestilence") {}
	// };

	class BloodStrikeTrigger : public DebuffFromBotTrigger {
	public:
		BloodStrikeTrigger(PlayerbotAI* ai) : DebuffFromBotTrigger(ai, "blood strike") {}
	};


	class HowlingBlastTrigger : public DebuffTrigger {
	public:
		HowlingBlastTrigger(PlayerbotAI* ai) : DebuffTrigger(ai, "howling blast") {}
	};

    class MindFreezeInterruptSpellTrigger : public InterruptSpellTrigger
    {
    public:
		MindFreezeInterruptSpellTrigger(PlayerbotAI* ai) : InterruptSpellTrigger(ai, "mind freeze") {}
    };

	class StrangulateInterruptSpellTrigger : public InterruptSpellTrigger
	{
	public:
		StrangulateInterruptSpellTrigger(PlayerbotAI* ai) : InterruptSpellTrigger(ai, "strangulate") {}
	};

    class KillingMachineTrigger : public BoostTrigger
    {
    public:
		KillingMachineTrigger(PlayerbotAI* ai) : BoostTrigger(ai, "killing machine") {}
    };

    class MindFreezeOnEnemyHealerTrigger : public InterruptEnemyHealerTrigger
    {
    public:
		MindFreezeOnEnemyHealerTrigger(PlayerbotAI* ai) : InterruptEnemyHealerTrigger(ai, "mind freeze") {}
    };

	class ChainsOfIceSnareTrigger : public SnareTargetTrigger
	{
	public:
		ChainsOfIceSnareTrigger(PlayerbotAI* ai) : SnareTargetTrigger(ai, "chains of ice") {}
	};

	class PestilenceTrigger : public SpellTrigger
	{
	public:
		PestilenceTrigger(PlayerbotAI* ai) : SpellTrigger(ai, "pestilence") {}
		virtual string GetTargetName() { return "current target"; }
		virtual bool IsActive() {
			if (!SpellTrigger::IsActive()) {
				return false;
			}
			Aura *blood_plague = ai->GetAuraWithDuration("blood plague", GetTarget(), true);
			Aura *frost_fever = ai->GetAuraWithDuration("frost fever", GetTarget(), true);
			if ((blood_plague && blood_plague->GetDuration() <= 3000) ||
			    (frost_fever && frost_fever->GetDuration() <= 3000)) {
					return true;
				}
			return false;
		}
	};

	class StrangulateOnEnemyHealerTrigger : public InterruptEnemyHealerTrigger
	{
	public:
		StrangulateOnEnemyHealerTrigger(PlayerbotAI* ai) : InterruptEnemyHealerTrigger(ai, "strangulate") {}
	};
}
