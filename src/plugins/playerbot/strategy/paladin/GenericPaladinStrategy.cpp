#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "GenericPaladinStrategy.h"
#include "GenericPaladinStrategyActionNodeFactory.h"

using namespace ai;


GenericPaladinStrategy::GenericPaladinStrategy(PlayerbotAI* ai) : CombatStrategy(ai)
{
    actionNodeFactories.Add(new GenericPaladinStrategyActionNodeFactory());
}

void GenericPaladinStrategy::InitTriggers(std::list<TriggerNode*> &triggers)
{
    CombatStrategy::InitTriggers(triggers);

	triggers.push_back(new TriggerNode(
		"hammer of justice interrupt",
		NextAction::array(0, new NextAction("hammer of justice", ACTION_INTERRUPT), NULL)));

	triggers.push_back(new TriggerNode(
		"hammer of justice on enemy healer",
		NextAction::array(0, new NextAction("hammer of justice on enemy healer", ACTION_INTERRUPT), NULL)));

	triggers.push_back(new TriggerNode(
		"critical health",
		NextAction::array(0, new NextAction("lay on hands", ACTION_EMERGENCY), NULL)));

	triggers.push_back(new TriggerNode(
		"party member critical health",
		NextAction::array(0, new NextAction("lay on hands on party", ACTION_EMERGENCY), NULL)));

	// triggers.push_back(new TriggerNode(
	// 	"target critical health",
	// 	NextAction::array(0, new NextAction("hammer of wrath", ACTION_HIGH + 1), NULL)));

	triggers.push_back(new TriggerNode(
        "medium mana",
		NextAction::array(0, new NextAction("divine plea", ACTION_HIGH), NULL)));
}

void PaladinCureStrategy::InitTriggers(std::list<TriggerNode*> &triggers)
{
    // triggers.push_back(new TriggerNode(
    //     "cleanse cure disease",
    //     NextAction::array(0, new NextAction("cleanse disease", ACTION_EMERGENCY + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "cleanse party member cure disease",
        NextAction::array(0, new NextAction("cleanse disease on party", ACTION_DISPEL + 1), NULL)));

    // triggers.push_back(new TriggerNode(
    //     "cleanse cure poison",
    //     NextAction::array(0, new NextAction("cleanse poison", ACTION_DISPEL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "cleanse party member cure poison",
        NextAction::array(0, new NextAction("cleanse poison on party", ACTION_EMERGENCY + 1), NULL)));

	// triggers.push_back(new TriggerNode(
	// 	"cleanse cure magic",
	// 	NextAction::array(0, new NextAction("cleanse magic", ACTION_DISPEL + 2), NULL)));

	triggers.push_back(new TriggerNode(
		"cleanse party member cure magic",
		NextAction::array(0, new NextAction("cleanse magic on party", ACTION_EMERGENCY + 1), NULL)));
}
