#include "../pchdef.h"
#include "PlayerbotMgr.h"
#include "playerbot.h"

#include "AiFactory.h"

#include "../Grids/Notifiers/GridNotifiers.h"
#include "../Grids/Notifiers/GridNotifiersImpl.h"
#include "../Grids/Cells/CellImpl.h"
#include "strategy/values/LastMovementValue.h"
#include "strategy/actions/LogLevelAction.h"
#include "strategy/values/LastSpellCastValue.h"
#include "LootObjectStack.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotAI.h"
#include "PlayerbotFactory.h"
#include "PlayerbotSecurity.h"
#include "../Groups/Group.h"
#include "../Entities/Pet/Pet.h"
#include "../Spells/Auras/SpellAuraEffects.h"

using namespace ai;
using namespace std;

vector<string>& split(const string &s, char delim, vector<string> &elems);
vector<string> split(const string &s, char delim);
uint64 extractGuid(WorldPacket& packet);
std::string &trim(std::string &s);

uint32 PlayerbotChatHandler::extractQuestId(string str)
{
    char* source = (char*)str.c_str();
    char* cId = extractKeyFromLink(source,"Hquest");
    return cId ? atol(cId) : 0;
}

void PacketHandlingHelper::AddHandler(uint16 opcode, string handler)
{
    handlers[opcode] = handler;
}

void PacketHandlingHelper::Handle(ExternalEventHelper &helper)
{
    while (!queue.empty())
    {
        helper.HandlePacket(handlers, queue.top());
        queue.pop();
    }
}

void PacketHandlingHelper::AddPacket(const WorldPacket& packet)
{
	if (handlers.find(packet.GetOpcode()) != handlers.end())
        queue.push(WorldPacket(packet));
}


std::vector<std::string> PlayerbotAI::dispel_whitelist = {
    "mutating injection",
    "frostbolt",
};

PlayerbotAI::PlayerbotAI() : PlayerbotAIBase(), bot(NULL), aiObjectContext(NULL),
    currentEngine(NULL), chatHelper(this), chatFilter(this), accountId(0), security(NULL), master(NULL)
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
        engines[i] = NULL;
}

PlayerbotAI::PlayerbotAI(Player* bot) :
    PlayerbotAIBase(), chatHelper(this), chatFilter(this), security(bot), master(NULL)
{
	this->bot = bot;

	accountId = sObjectMgr->GetPlayerAccountIdByGUID(bot->GetGUID());

    aiObjectContext = AiFactory::createAiObjectContext(bot, this);

    engines[BOT_STATE_COMBAT] = AiFactory::createCombatEngine(bot, this, aiObjectContext);
    engines[BOT_STATE_NON_COMBAT] = AiFactory::createNonCombatEngine(bot, this, aiObjectContext);
    engines[BOT_STATE_DEAD] = AiFactory::createDeadEngine(bot, this, aiObjectContext);
    currentEngine = engines[BOT_STATE_NON_COMBAT];
    currentState = BOT_STATE_NON_COMBAT;

    masterIncomingPacketHandlers.AddHandler(CMSG_GAMEOBJ_REPORT_USE, "use game object");
    masterIncomingPacketHandlers.AddHandler(CMSG_AREATRIGGER, "area trigger");
    masterIncomingPacketHandlers.AddHandler(CMSG_GAMEOBJ_USE, "use game object");
    masterIncomingPacketHandlers.AddHandler(CMSG_LOOT_ROLL, "loot roll");
    masterIncomingPacketHandlers.AddHandler(CMSG_GOSSIP_HELLO, "gossip hello");
    masterIncomingPacketHandlers.AddHandler(CMSG_QUESTGIVER_HELLO, "gossip hello");
    masterIncomingPacketHandlers.AddHandler(CMSG_QUESTGIVER_COMPLETE_QUEST, "complete quest");
    masterIncomingPacketHandlers.AddHandler(CMSG_QUESTGIVER_ACCEPT_QUEST, "accept quest");
    masterIncomingPacketHandlers.AddHandler(CMSG_ACTIVATETAXI, "activate taxi");
    masterIncomingPacketHandlers.AddHandler(CMSG_ACTIVATETAXIEXPRESS, "activate taxi");
    masterIncomingPacketHandlers.AddHandler(CMSG_MOVE_SPLINE_DONE, "taxi done");
    masterIncomingPacketHandlers.AddHandler(CMSG_GROUP_UNINVITE_GUID, "uninvite");
    masterIncomingPacketHandlers.AddHandler(CMSG_PUSHQUESTTOPARTY, "quest share");
    masterIncomingPacketHandlers.AddHandler(CMSG_GUILD_INVITE, "guild invite");
    masterIncomingPacketHandlers.AddHandler(CMSG_LFG_TELEPORT, "lfg teleport");

    botOutgoingPacketHandlers.AddHandler(SMSG_GROUP_INVITE, "group invite");
    botOutgoingPacketHandlers.AddHandler(BUY_ERR_NOT_ENOUGHT_MONEY, "not enough money");
    botOutgoingPacketHandlers.AddHandler(BUY_ERR_REPUTATION_REQUIRE, "not enough reputation");
    botOutgoingPacketHandlers.AddHandler(SMSG_GROUP_SET_LEADER, "group set leader");
    botOutgoingPacketHandlers.AddHandler(SMSG_FORCE_RUN_SPEED_CHANGE, "check mount state");
    botOutgoingPacketHandlers.AddHandler(SMSG_RESURRECT_REQUEST, "resurrect request");
    botOutgoingPacketHandlers.AddHandler(SMSG_INVENTORY_CHANGE_FAILURE, "cannot equip");
    botOutgoingPacketHandlers.AddHandler(SMSG_TRADE_STATUS, "trade status");
    botOutgoingPacketHandlers.AddHandler(SMSG_LOOT_RESPONSE, "loot response");
    botOutgoingPacketHandlers.AddHandler(SMSG_QUESTUPDATE_ADD_KILL, "quest objective completed");
    botOutgoingPacketHandlers.AddHandler(SMSG_ITEM_PUSH_RESULT, "item push result");
    botOutgoingPacketHandlers.AddHandler(SMSG_PARTY_COMMAND_RESULT, "party command");
    botOutgoingPacketHandlers.AddHandler(SMSG_CAST_FAILED, "cast failed");
    botOutgoingPacketHandlers.AddHandler(SMSG_DUEL_REQUESTED, "duel requested");
    botOutgoingPacketHandlers.AddHandler(SMSG_LFG_ROLE_CHOSEN, "lfg role check");
    botOutgoingPacketHandlers.AddHandler(SMSG_LFG_PROPOSAL_UPDATE, "lfg proposal");
	botOutgoingPacketHandlers.AddHandler(SMSG_BATTLEFIELD_STATUS, "bg status");

    masterOutgoingPacketHandlers.AddHandler(SMSG_PARTY_COMMAND_RESULT, "party command");
    masterOutgoingPacketHandlers.AddHandler(MSG_RAID_READY_CHECK, "ready check");
    masterOutgoingPacketHandlers.AddHandler(MSG_RAID_READY_CHECK_FINISHED, "ready check finished");
}

PlayerbotAI::~PlayerbotAI()
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
    {
        if (engines[i])
            delete engines[i];
    }

    if (aiObjectContext)
        delete aiObjectContext;
}

void PlayerbotAI::UpdateAI(uint32 elapsed)
{
    if (bot->IsBeingTeleported())
        return;

	//DEBUG
/*	engines[BOT_STATE_COMBAT]->testMode = bot->InBattleground();
	engines[BOT_STATE_NON_COMBAT]->testMode = bot->InBattleground();
	engines[BOT_STATE_COMBAT]->testPrefix = bot->GetName();
	engines[BOT_STATE_NON_COMBAT]->testPrefix = bot->GetName();*/

	//EOD

    if (nextAICheckDelay > sPlayerbotAIConfig.maxWaitForMove && bot->IsInCombat() 
        && !bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) && !bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
    {
        nextAICheckDelay = sPlayerbotAIConfig.maxWaitForMove;
    }

    PlayerbotAIBase::UpdateAI(elapsed);
}

void PlayerbotAI::UpdateAIInternal(uint32 elapsed)
{
    ExternalEventHelper helper(aiObjectContext);
    while (!chatCommands.empty())
    {
        ChatCommandHolder holder = chatCommands.top();
        string command = holder.GetCommand();
        Player* owner = holder.GetOwner();
        if (!helper.ParseChatCommand(command, owner) && holder.GetType() == CHAT_MSG_WHISPER)
        {
            ostringstream out; out << "Unknown command " << command;
            TellMaster(out);
            helper.ParseChatCommand("help");
        }
        chatCommands.pop();
    }

    botOutgoingPacketHandlers.Handle(helper);
    masterIncomingPacketHandlers.Handle(helper);
    masterOutgoingPacketHandlers.Handle(helper);

	DoNextAction();
}

void PlayerbotAI::HandleTeleportAck()
{
	bot->GetMotionMaster()->Clear(true);
	if (bot->IsBeingTeleportedNear())
	{
		WorldPacket p = WorldPacket(MSG_MOVE_TELEPORT_ACK, 8 + 4 + 4);
		p.appendPackGUID(bot->GetGUID());
		p << (uint32) 0; // supposed to be flags? not used currently
		p << (uint32) time(0); // time - not currently used
		bot->GetSession()->HandleMoveTeleportAck(p);
	}
	else if (bot->IsBeingTeleportedFar())
	{
	    WorldPacket p;
		bot->GetSession()->HandleMoveWorldportAckOpcode(p);
		SetNextCheckDelay(1000);
	}
}

void PlayerbotAI::Reset()
{
    if (bot->IsFlying())
        return;

    currentEngine = engines[BOT_STATE_NON_COMBAT];
    nextAICheckDelay = 0;

    aiObjectContext->GetValue<Unit*>("old target")->Set(NULL);
    aiObjectContext->GetValue<Unit*>("current target")->Set(NULL);
    aiObjectContext->GetValue<LootObject>("loot target")->Set(LootObject());
    aiObjectContext->GetValue<uint32>("lfg proposal")->Set(0);

    LastSpellCast & lastSpell = aiObjectContext->GetValue<LastSpellCast& >("last spell cast")->Get();
    lastSpell.Reset();

    LastMovement & lastMovement = aiObjectContext->GetValue<LastMovement& >("last movement")->Get();
    lastMovement.Set(NULL);

    bot->GetMotionMaster()->Clear();
    bot->m_taxi.ClearTaxiDestinations();
    InterruptSpell();

    for (int i = 0 ; i < BOT_STATE_MAX; i++)
    {
        engines[i]->Init();
    }
}

void PlayerbotAI::HandleCommand(uint32 type, const string& text, Player& fromPlayer)
{
    if (!GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, type != CHAT_MSG_WHISPER, &fromPlayer))
        return;

    if (type == CHAT_MSG_ADDON)
        return;

    string filtered = text;
    if (!sPlayerbotAIConfig.commandPrefix.empty())
    {
        if (filtered.find(sPlayerbotAIConfig.commandPrefix) != 0)
            return;

        filtered = filtered.substr(sPlayerbotAIConfig.commandPrefix.size());
    }

    filtered = chatFilter.Filter(trim((string&)filtered));
    if (filtered.empty())
        return;

    if (filtered.find("who") != 0 && !GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, type != CHAT_MSG_WHISPER, &fromPlayer))
        return;

    if (type == CHAT_MSG_RAID_WARNING && filtered.find(bot->GetName()) != string::npos && filtered.find("award") == string::npos)
    {
        ChatCommandHolder cmd("warning", &fromPlayer, type);
        chatCommands.push(cmd);
        return;
    }

    if (filtered.size() > 2 && filtered.substr(0, 2) == "d " || filtered.size() > 3 && filtered.substr(0, 3) == "do ")
    {
        std::string action = filtered.substr(filtered.find(" ") + 1);
        DoSpecificAction(action);
    }
    else if (filtered == "reset")
    {
        Reset();
    }
    else
    {
        ChatCommandHolder cmd(filtered, &fromPlayer, type);
        chatCommands.push(cmd);
    }
}

void PlayerbotAI::HandleBotOutgoingPacket(const WorldPacket& packet)
{
    switch (packet.GetOpcode())
    {
    case SMSG_MOVE_SET_CAN_FLY:
        {
            WorldPacket p(packet);
            uint64 guid;
            p.readPackGUID(guid);
            if (guid != bot->GetGUID())
                return;

            bot->m_movementInfo.SetMovementFlags((MovementFlags)(MOVEMENTFLAG_FLYING|MOVEMENTFLAG_CAN_FLY));
            return;
        }
    case SMSG_MOVE_UNSET_CAN_FLY:
        {
            WorldPacket p(packet);
            uint64 guid;
            p.readPackGUID(guid);
            if (guid != bot->GetGUID())
                return;
            bot->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_FLYING);
            return;
        }
    case SMSG_CAST_FAILED:
        {
            WorldPacket p(packet);
            p.rpos(0);
            uint8 castCount, result;
            uint32 spellId;
            p >> castCount >> spellId >> result;
            if (result != SPELL_CAST_OK)
            {
                SpellInterrupted(spellId);
                botOutgoingPacketHandlers.AddPacket(packet);
            }
            return;
        }
    case SMSG_SPELL_FAILURE:
        {
            WorldPacket p(packet);
            p.rpos(0);
            uint64 casterGuid;
            p.readPackGUID(casterGuid);
            if (casterGuid != bot->GetGUID())
                return;

            uint8 castCount;
            uint32 spellId;
            p >> castCount;
            p >> spellId;
            SpellInterrupted(spellId);
            return;
        }
    case SMSG_SPELL_DELAYED:
        {
            WorldPacket p(packet);
            p.rpos(0);
            uint64 casterGuid;
            p.readPackGUID(casterGuid);

            if (casterGuid != bot->GetGUID())
                return;

            uint32 delaytime;
            p >> delaytime;
            if (delaytime <= 1000)
                IncreaseNextCheckDelay(delaytime);
            return;
        }
    default:
        botOutgoingPacketHandlers.AddPacket(packet);
    }
}

void PlayerbotAI::SpellInterrupted(uint32 spellid)
{
    LastSpellCast& lastSpell = aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get();
    if (lastSpell.id != spellid)
        return;

    lastSpell.Reset();

    time_t now = time(0);
    if (now <= lastSpell.time)
        return;

    uint32 castTimeSpent = 1000 * (now - lastSpell.time);

    int32 globalCooldown = CalculateGlobalCooldown(lastSpell.id);
    if (castTimeSpent < globalCooldown)
        SetNextCheckDelay(globalCooldown - castTimeSpent);
    else
        SetNextCheckDelay(0);

    lastSpell.id = 0;
}

int32 PlayerbotAI::CalculateGlobalCooldown(uint32 spellid)
{
    if (!spellid)
        return 0;

    if (bot->GetSpellHistory()->HasCooldown(spellid))
        return sPlayerbotAIConfig.globalCoolDown;

    return sPlayerbotAIConfig.reactDelay;
}

void PlayerbotAI::HandleMasterIncomingPacket(const WorldPacket& packet)
{
    masterIncomingPacketHandlers.AddPacket(packet);
}

void PlayerbotAI::HandleMasterOutgoingPacket(const WorldPacket& packet)
{
    masterOutgoingPacketHandlers.AddPacket(packet);
}

void PlayerbotAI::ChangeEngine(BotState type)
{
    Engine* engine = engines[type];

    if (currentEngine != engine)
    {
        currentEngine = engine;
        currentState = type;
        ReInitCurrentEngine();

        switch (type)
        {
        case BOT_STATE_COMBAT:
            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "=== %s COMBAT ===", bot->GetName().c_str());
            break;
        case BOT_STATE_NON_COMBAT:
            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "=== %s NON-COMBAT ===", bot->GetName().c_str());
            break;
        case BOT_STATE_DEAD:
            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "=== %s DEAD ===", bot->GetName().c_str());
            break;
        }
    }
}

void PlayerbotAI::DoNextAction()
{
    if (bot->IsBeingTeleported() || (GetMaster() && GetMaster()->IsBeingTeleported()))
        return;

    currentEngine->DoNextAction(NULL);

    if (bot->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
    {
        bot->m_movementInfo.SetMovementFlags((MovementFlags)(MOVEMENTFLAG_FLYING|MOVEMENTFLAG_CAN_FLY));

        // TODO
        //WorldPacket packet(CMSG_MOVE_SET_FLY);
        //packet.appendPackGUID(bot->GetGUID());
        //packet << bot->m_movementInfo;
        bot->SetMover(bot);
        //bot->GetSession()->HandleMovementOpcodes(packet);
    }

    Player* master = GetMaster();
    if (bot->IsMounted() && bot->IsFlying())
    {
        bot->m_movementInfo.SetMovementFlags((MovementFlags)(MOVEMENTFLAG_FLYING|MOVEMENTFLAG_CAN_FLY));

        bot->SetSpeed(MOVE_FLIGHT, 1.0f);
        bot->SetSpeed(MOVE_RUN, 1.0f);

        if (master)
        {
            bot->SetSpeed(MOVE_FLIGHT, master->GetSpeedRate(MOVE_FLIGHT));
            bot->SetSpeed(MOVE_RUN, master->GetSpeedRate(MOVE_FLIGHT));
        }

    }

    if (currentEngine != engines[BOT_STATE_DEAD] && !bot->IsAlive())
        ChangeEngine(BOT_STATE_DEAD);

    if (currentEngine == engines[BOT_STATE_DEAD] && bot->IsAlive())
        ChangeEngine(BOT_STATE_NON_COMBAT);

    Group *group = bot->GetGroup();

	if (!master && group &&!bot->InBattleground())
	{
		for (GroupReference *gref = group->GetFirstMember(); gref; gref = gref->next())
		{
			Player* member = gref->GetSource();
			PlayerbotAI* ai = bot->GetPlayerbotAI();
			if (member && member->IsInWorld() && !member->GetPlayerbotAI() && (!master || master->GetPlayerbotAI()))
			{
				ai->SetMaster(member);
				ai->ResetStrategies();
				ai->TellMaster("Hello");
				break;
			}
		}
	}
}

void PlayerbotAI::ReInitCurrentEngine()
{
    InterruptSpell();
    currentEngine->Init();
}

void PlayerbotAI::ChangeStrategy(string names, BotState type)
{
    Engine* e = engines[type];
    if (!e)
        return;

    e->ChangeStrategy(names);
}

void PlayerbotAI::DoSpecificAction(string name)
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
    {
        ostringstream out;
        ActionResult res = engines[i]->ExecuteAction(name);
        switch (res)
        {
        case ACTION_RESULT_UNKNOWN:
            continue;
        case ACTION_RESULT_OK:
            out << name << ": done";
            TellMaster(out);
            PlaySound(TEXT_EMOTE_NOD);
            return;
        case ACTION_RESULT_IMPOSSIBLE:
            out << name << ": impossible";
            TellMaster(out);
            PlaySound(TEXT_EMOTE_NO);
            return;
        case ACTION_RESULT_USELESS:
            out << name << ": useless";
            TellMaster(out);
            PlaySound(TEXT_EMOTE_NO);
            return;
        case ACTION_RESULT_FAILED:
            out << name << ": failed";
            TellMaster(out);
            return;
        }
    }
    ostringstream out;
    out << name << ": unknown action";
    TellMaster(out);
}

bool PlayerbotAI::PlaySound(uint32 emote)
{
    if (EmotesTextSoundEntry const* soundEntry = FindTextSoundEmoteFor(emote, bot->getRace(), bot->getGender()))
    {
        bot->PlayDistanceSound(soundEntry->SoundId);
        return true;
    }

    return false;
}

//thesawolf - emotion responses
void PlayerbotAI::ReceiveEmote(Player* player, uint32 emote)
{
    // thesawolf - lets clear any running emotes first
    bot->HandleEmoteCommand(EMOTE_ONESHOT_NONE);
    switch (emote)
    {
        case TEXT_EMOTE_BONK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
            break;
        case TEXT_EMOTE_SALUTE:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
            break;
        case TEXT_EMOTE_WAIT:
            //SetBotCommandState(COMMAND_STAY);
            bot->Say("Fine.. I'll stay right here..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BECKON:
        case TEXT_EMOTE_FOLLOW:
            //SetBotCommandState(COMMAND_FOLLOW, true);
            bot->Say("Wherever you go, I'll follow..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_WAVE:
        case TEXT_EMOTE_GREET:
        case TEXT_EMOTE_HAIL:
        case TEXT_EMOTE_HELLO:
        case TEXT_EMOTE_WELCOME:
        case TEXT_EMOTE_INTRODUCE:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
            bot->Say("Hey there!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_DANCE:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_DANCE);
            bot->Say("Shake what your mama gave you!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_FLIRT:
        case TEXT_EMOTE_KISS:
        case TEXT_EMOTE_HUG:
        case TEXT_EMOTE_BLUSH:
        case TEXT_EMOTE_SMILE:
        case TEXT_EMOTE_LOVE:
        case TEXT_EMOTE_HOLDHAND:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_SHY);
            bot->Say("Awwwww...", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_FLEX:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_APPLAUD);
            bot->Say("Hercules! Hercules!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_ANGRY:
        case TEXT_EMOTE_FACEPALM:
        case TEXT_EMOTE_GLARE:
        case TEXT_EMOTE_BLAME:
        case TEXT_EMOTE_FAIL:
        case TEXT_EMOTE_REGRET:
        case TEXT_EMOTE_SCOLD:
        case TEXT_EMOTE_CROSSARMS:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
            bot->Say("Did I do thaaaaat?", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_FART:
        case TEXT_EMOTE_BURP:
        case TEXT_EMOTE_GASP:
        case TEXT_EMOTE_NOSEPICK:
        case TEXT_EMOTE_SNIFF:
        case TEXT_EMOTE_STINK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
            bot->Say("Wasn't me! Just sayin'..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_JOKE:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
            bot->Say("Oh.. was I not supposed to laugh so soon?", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_CHICKEN:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_RUDE);
            bot->Say("We'll see who's chicken soon enough!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_APOLOGIZE:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
            bot->Say("You damn right you're sorry!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_APPLAUD:
        case TEXT_EMOTE_CLAP:
        case TEXT_EMOTE_CONGRATULATE:
        case TEXT_EMOTE_HAPPY:
        case TEXT_EMOTE_GOLFCLAP:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
            bot->Say("Thank you.. Thank you.. I'm here all week.", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BEG:
        case TEXT_EMOTE_GROVEL:
        case TEXT_EMOTE_PLEAD:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
            bot->Say("Beg all you want.. I have nothing for you.", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BITE:
        case TEXT_EMOTE_POKE:
        case TEXT_EMOTE_SCRATCH:
        case TEXT_EMOTE_PINCH:
        case TEXT_EMOTE_PUNCH:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
            bot->Yell("OUCH! Dammit, that hurt!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BORED:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
            bot->Say("My job description doesn't include entertaining you..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BOW:
        case TEXT_EMOTE_CURTSEY:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
            break;
        case TEXT_EMOTE_BRB:
        case TEXT_EMOTE_SIT:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_EAT);
            bot->Say("Looks like time for an AFK break..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_AGREE:
        case TEXT_EMOTE_NOD:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
            bot->Say("At least SOMEONE agrees with me!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_AMAZE:
        case TEXT_EMOTE_COWER:
        case TEXT_EMOTE_CRINGE:
        case TEXT_EMOTE_EYE:
        case TEXT_EMOTE_KNEEL:
        case TEXT_EMOTE_PEER:
        case TEXT_EMOTE_SURRENDER:
        case TEXT_EMOTE_PRAISE:
        case TEXT_EMOTE_SCARED:
        case TEXT_EMOTE_COMMEND:
        case TEXT_EMOTE_AWE:
        case TEXT_EMOTE_JEALOUS:
        case TEXT_EMOTE_PROUD:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_FLEX);
            bot->Say("Yes, Yes. I know I'm amazing..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BLEED:
        case TEXT_EMOTE_MOURN:
        case TEXT_EMOTE_FLOP:
        case TEXT_EMOTE_FAINT:
        case TEXT_EMOTE_PULSE:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
            bot->Yell("MEDIC! Stat!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BLINK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_KICK);
            bot->Say("What? You got something in your eye?", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BOUNCE:
        case TEXT_EMOTE_BARK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
            bot->Say("Who's a good doggy? You're a good doggy!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BYE:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
            bot->Say("Umm.... wait! Where are you going?!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_CACKLE:
        case TEXT_EMOTE_LAUGH:
        case TEXT_EMOTE_CHUCKLE:
        case TEXT_EMOTE_GIGGLE:
        case TEXT_EMOTE_GUFFAW:
        case TEXT_EMOTE_ROFL:
        case TEXT_EMOTE_SNICKER:
        case TEXT_EMOTE_SNORT:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
            bot->Say("Wait... what are we laughing at again?", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_CONFUSED:
        case TEXT_EMOTE_CURIOUS:
        case TEXT_EMOTE_FIDGET:
        case TEXT_EMOTE_FROWN:
        case TEXT_EMOTE_SHRUG:
        case TEXT_EMOTE_SIGH:
        case TEXT_EMOTE_STARE:
        case TEXT_EMOTE_TAP:
        case TEXT_EMOTE_SURPRISED:
        case TEXT_EMOTE_WHINE:
        case TEXT_EMOTE_BOGGLE:
        case TEXT_EMOTE_LOST:
        case TEXT_EMOTE_PONDER:
        case TEXT_EMOTE_SNUB:
        case TEXT_EMOTE_SERIOUS:
        case TEXT_EMOTE_EYEBROW:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
            bot->Say("Don't look at  me.. I just work here", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_COUGH:
        case TEXT_EMOTE_DROOL:
        case TEXT_EMOTE_SPIT:
        case TEXT_EMOTE_LICK:
        case TEXT_EMOTE_BREATH:
        case TEXT_EMOTE_SNEEZE:
        case TEXT_EMOTE_SWEAT:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
            bot->Say("Ewww! Keep your nasty germs over there!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_CRY:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
            bot->Say("Don't you start crying or it'll make me start crying!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_CRACK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
            bot->Say("It's clobbering time!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_EAT:
        case TEXT_EMOTE_DRINK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_EAT);
            bot->Say("I hope you brought enough for the whole class...", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_GLOAT:
        case TEXT_EMOTE_MOCK:
        case TEXT_EMOTE_TEASE:
        case TEXT_EMOTE_EMBARRASS:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
            bot->Say("Doesn't mean you need to be an ass about it..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_HUNGRY:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_EAT);
            bot->Say("What? You want some of this?", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_LAYDOWN:
        case TEXT_EMOTE_TIRED:
        case TEXT_EMOTE_YAWN:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
            bot->Say("Is it break time already?", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_MOAN:
        case TEXT_EMOTE_MOON:
        case TEXT_EMOTE_SEXY:
        case TEXT_EMOTE_SHAKE:
        case TEXT_EMOTE_WHISTLE:
        case TEXT_EMOTE_CUDDLE:
        case TEXT_EMOTE_PURR:
        case TEXT_EMOTE_SHIMMY:
        case TEXT_EMOTE_SMIRK:
        case TEXT_EMOTE_WINK:
        case TEXT_EMOTE_CHARM:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
            bot->Say("Keep it in your pants, boss..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_NO:
        case TEXT_EMOTE_VETO:
        case TEXT_EMOTE_DISAGREE:
        case TEXT_EMOTE_DOUBT:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
            bot->Say("Aww.... why not?!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_PANIC:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
            bot->Say("Now is NOT the time to panic!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_POINT:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
            bot->Say("What?! I can do that TOO!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_RUDE:
        case TEXT_EMOTE_RASP:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_RUDE);
            bot->Say("Right back at you, bub!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_ROAR:
        case TEXT_EMOTE_THREATEN:
        case TEXT_EMOTE_CALM:
        case TEXT_EMOTE_DUCK:
        case TEXT_EMOTE_TAUNT:
        case TEXT_EMOTE_PITY:
        case TEXT_EMOTE_GROWL:
        case TEXT_EMOTE_TRAIN:
        case TEXT_EMOTE_INCOMING:
        case TEXT_EMOTE_CHARGE:
        case TEXT_EMOTE_FLEE:
        case TEXT_EMOTE_ATTACKMYTARGET:
        case TEXT_EMOTE_OPENFIRE:
        case TEXT_EMOTE_ENCOURAGE:
        case TEXT_EMOTE_ENEMY:
        case TEXT_EMOTE_CHALLENGE:
        case TEXT_EMOTE_REVENGE:
        case TEXT_EMOTE_SHAKEFIST:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
            bot->Yell("RAWR!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_TALK:
        case TEXT_EMOTE_TALKEX:
        case TEXT_EMOTE_TALKQ:
        case TEXT_EMOTE_LISTEN:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
            bot->Say("Blah Blah Blah Yakety Smackety..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_THANK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
            bot->Say("You are quite welcome!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_VICTORY:
        case TEXT_EMOTE_CHEER:
        case TEXT_EMOTE_TOAST:
        case TEXT_EMOTE_HIGHFIVE:
        case TEXT_EMOTE_DING:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);
            bot->Say("Yay!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_COLD:
        case TEXT_EMOTE_SHIVER:
        case TEXT_EMOTE_THIRSTY:
        case TEXT_EMOTE_OOM:
        case TEXT_EMOTE_HEALME:
        case TEXT_EMOTE_POUT:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
            bot->Say("And what exactly am I supposed to do about that?", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_COMFORT:
        case TEXT_EMOTE_SOOTHE:
        case TEXT_EMOTE_PAT:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
            bot->Say("Thanks...", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_INSULT:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
            bot->Say("You hurt my feelings..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_JK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
            bot->Say("You.....", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_RAISE:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
            bot->Say("Yes.. you.. at the back of the class..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_READY:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
            bot->Say("Ready here, too!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_SHOO:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_KICK);
            bot->Say("Shoo yourself!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_SLAP:
        case TEXT_EMOTE_SMACK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
            bot->Say("What did I do to deserve that?", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_STAND:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_NONE);
            bot->Say("What? Break time's over? Fine..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_TICKLE:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
            bot->Say("Hey! Stop that!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_VIOLIN:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
            bot->Say("Har Har.. very funny..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_HELPME:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
            bot->Yell("Quick! Someone HELP!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_GOODLUCK:
        case TEXT_EMOTE_LUCK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
            bot->Say("Thanks... I'll need it..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BRANDISH:
        case TEXT_EMOTE_MERCY:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_BEG);
            bot->Say("Please don't kill me!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_BADFEELING:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
            bot->Say("I'm just waiting for the ominous music now...", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_MAP:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
            bot->Say("Noooooooo.. you just couldn't ask for directions, huh?", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_IDEA:
        case TEXT_EMOTE_THINK:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
            bot->Say("Oh boy.. another genius idea...", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_OFFER:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
            bot->Say("No thanks.. I had some back at the last village", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_PET:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
            bot->Say("Do I look like a dog to you?!", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_ROLLEYES:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
            bot->Say("Keep doing that and I'll roll those eyes right out of your head..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_SING:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_APPLAUD);
            bot->Say("Lovely... just lovely..", LANG_UNIVERSAL);
            break;
        case TEXT_EMOTE_COVEREARS:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
            bot->Yell("You think that's going to help you?!", LANG_UNIVERSAL);
            break;
        default:
            bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
            bot->Say("Mmmmmkaaaaaay...", LANG_UNIVERSAL);
            break;
    }                                    
    return;
}

bool PlayerbotAI::ContainsStrategy(StrategyType type)
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
    {
        if (engines[i]->ContainsStrategy(type))
            return true;
    }
    return false;
}

bool PlayerbotAI::HasStrategy(string name, BotState type)
{
    return engines[type]->HasStrategy(name);
}

void PlayerbotAI::ResetStrategies()
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
        engines[i]->removeAllStrategies();

    AiFactory::AddDefaultCombatStrategies(bot, this, engines[BOT_STATE_COMBAT]);
    AiFactory::AddDefaultNonCombatStrategies(bot, this, engines[BOT_STATE_NON_COMBAT]);
    AiFactory::AddDefaultDeadStrategies(bot, this, engines[BOT_STATE_DEAD]);
}

bool PlayerbotAI::IsRanged(Player* player)
{
    PlayerbotAI* botAi = player->GetPlayerbotAI();
    if (botAi)
        return botAi->ContainsStrategy(STRATEGY_TYPE_RANGED);

    int tab = AiFactory::GetPlayerSpecTab(player);
    switch (player->getClass())
    {
    case CLASS_DEATH_KNIGHT:
    case CLASS_WARRIOR:
    case CLASS_ROGUE:
        return false;
        break;
    case CLASS_DRUID:
        if (tab == 1) {
            return false;
        }
        break;
    case CLASS_PALADIN:
        if (tab != 0) {
            return false;
        }
        break;
    case CLASS_SHAMAN:
        if (tab == 1) {
            return false;
        }
        break;
    }
    return true;
}

bool PlayerbotAI::IsRangedDps(Player* player)
{
    return IsRanged(player) && IsDps(player);
}

bool PlayerbotAI::IsRangedDpsAssistantOfIndex(Player* player, int index)
{
    Group* group = bot->GetGroup();
    if (!group) {
        return false;
    }
    Group::MemberSlotList const& slots = group->GetMemberSlots();
    int counter = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (group->IsAssistant(member->GetGUID()) && IsRangedDps(member)) {
            if (index == counter) {
                return player == member;
            }
            counter++;
        }
    }
    // not enough
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (!group->IsAssistant(member->GetGUID()) && IsRangedDps(member)) {
            if (index == counter) {
                return player == member;
            }
            counter++;
        }
    }
    return false;
}

int32 PlayerbotAI::GetGroupSlotIndex(Player* player)
{
    Group* group = bot->GetGroup();
    if (!group) {
        return -1;
    }
    Group::MemberSlotList const& slots = group->GetMemberSlots();
    int counter = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (player == member) {
            return counter;
        }
        counter++;
    }
    return 0;
}

int32 PlayerbotAI::GetRangedIndex(Player* player)
{
    if (!IsRanged(player)) {
        return -1;
    }
    Group* group = bot->GetGroup();
    if (!group) {
        return -1;
    }
    Group::MemberSlotList const& slots = group->GetMemberSlots();
    int counter = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (player == member) {
            return counter;
        }
        if (IsRanged(member)) {
            counter++;
        }
    }
    return 0;
}

int32 PlayerbotAI::GetClassIndex(Player* player, uint8_t cls)
{
    if (player->getClass() != cls) {
        return -1;
    }
    Group* group = bot->GetGroup();
    if (!group) {
        return -1;
    }
    Group::MemberSlotList const& slots = group->GetMemberSlots();
    int counter = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (player == member) {
            return counter;
        }
        if (member->getClass() == cls) {
            counter++;
        }
    }
    return 0;
}
int32 PlayerbotAI::GetRangedDpsIndex(Player* player)
{
    if (!IsRangedDps(player)) {
        return -1;
    }
    Group* group = bot->GetGroup();
    if (!group) {
        return -1;
    }
    Group::MemberSlotList const& slots = group->GetMemberSlots();
    int counter = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (player == member) {
            return counter;
        }
        if (IsRangedDps(member)) {
            counter++;
        }
    }
    return 0;
}

int32 PlayerbotAI::GetMeleeIndex(Player* player)
{
    if (IsRanged(player)) {
        return -1;
    }
    Group* group = bot->GetGroup();
    if (!group) {
        return -1;
    }
    Group::MemberSlotList const& slots = group->GetMemberSlots();
    int counter = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (player == member) {
            return counter;
        }
        if (!IsRanged(member)) {
            counter++;
        }
    }
    return 0;
}

bool PlayerbotAI::IsMainTank(Player* player) {
    Group* group = bot->GetGroup();
    if (!group) {
        return false;
    }
    uint64 mainTank = 0;
    Group::MemberSlotList const& slots = group->GetMemberSlots();
    for (Group::member_citerator itr = slots.begin(); itr != slots.end(); ++itr) {
        if (itr->flags & MEMBER_FLAG_MAINTANK)
            mainTank = itr->guid.GetRawValue();
    }
    if (mainTank != 0) {
        return player->GetGUID() == mainTank;
    }
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (IsTank(member)) {
            return player == member;
        }
    }
    return false;
}

bool PlayerbotAI::IsTank(Player* player)
{
    PlayerbotAI* botAi = player->GetPlayerbotAI();
    if (botAi)
        return botAi->ContainsStrategy(STRATEGY_TYPE_TANK);

    int tab = AiFactory::GetPlayerSpecTab(player);
    switch (player->getClass())
    {
        case CLASS_DEATH_KNIGHT:
            if (tab == 0) {
                return true;
            }
            break;
        case CLASS_PALADIN:
            if (tab == 1) {
                return true;
            }
            break;
        case CLASS_WARRIOR:
            if (tab == 2) {
                return true;
            }
            break;
        case CLASS_DRUID:
            if (tab == 1 && HasAnyAuraOf(player, "bear form", "dire bear form", "thick hide", NULL)) {
                return true;
            }
            break;
    }
    return false;
}

bool PlayerbotAI::IsAssistTank(Player* player) 
{
    return IsTank(player) && !IsMainTank(player);
}

bool PlayerbotAI::IsAssistTankOfIndex(Player* player, int index)
{
    Group* group = player->GetGroup();
    if (!group) {
        return false;
    }
    Group::MemberSlotList const& slots = group->GetMemberSlots();
    int counter = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (group->IsAssistant(member->GetGUID()) && IsAssistTank(member)) {
            if (index == counter) {
                return player == member;
            }
            counter++;
        }
    }
    // not enough assistant, auto assign
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (!group->IsAssistant(member->GetGUID()) && IsAssistTank(member)) {
            if (index == counter) {
                return player == member;
            }
            counter++;
        }
    }
    return false;
}

bool PlayerbotAI::IsHeal(Player* player)
{
    PlayerbotAI* botAi = player->GetPlayerbotAI();
    if (botAi)
        return botAi->ContainsStrategy(STRATEGY_TYPE_HEAL);

    int tab = AiFactory::GetPlayerSpecTab(player);
    switch (player->getClass())
    {
    case CLASS_PRIEST:
        if (tab == PRIEST_TAB_DISIPLINE || tab == PRIEST_TAB_HOLY) {
            return true;
        }
        break;
    case CLASS_DRUID:
        if (tab == DRUID_TAB_RESTORATION) {
            return true;
        }
        break;
    case CLASS_SHAMAN:
        if (tab == SHAMAN_TAB_RESTORATION) {
            return true;
        }
        break;
    case CLASS_PALADIN:
        if (tab == PALADIN_TAB_HOLY) {
            return true;
        }
        break;
    }
    return false;
}

bool PlayerbotAI::IsHealAssistantOfIndex(Player* player, int index)
{
    Group* group = bot->GetGroup();
    if (!group) {
        return false;
    }
    Group::MemberSlotList const& slots = group->GetMemberSlots();
    int counter = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (group->IsAssistant(member->GetGUID()) && IsHeal(member)) {
            if (index == counter) {
                return player == member;
            }
            counter++;
        }
    }
    // not enough
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next()) {
        Player* member = ref->GetSource();
        if (!group->IsAssistant(member->GetGUID()) && IsHeal(member)) {
            if (index == counter) {
                return player == member;
            }
            counter++;
        }
    }
    return false;
}

bool PlayerbotAI::IsDps(Player* player)
{
    PlayerbotAI* botAi = player->GetPlayerbotAI();
    if (botAi)
        return botAi->ContainsStrategy(STRATEGY_TYPE_DPS);

    int tab = AiFactory::GetPlayerSpecTab(player);
    switch (player->getClass())
    {
    case CLASS_MAGE:
    case CLASS_WARLOCK:
    case CLASS_HUNTER:
    case CLASS_ROGUE:
        return true;
    case CLASS_PRIEST:
        if (tab == PRIEST_TAB_SHADOW) {
            return true;
        }
        break;
    case CLASS_DRUID:
        if (tab == DRUID_TAB_BALANCE) {
            return true;
        }
        break;
    case CLASS_SHAMAN:
        if (tab != SHAMAN_TAB_RESTORATION) {
            return true;
        }
        break;
    case CLASS_PALADIN:
        if (tab == PALADIN_TAB_RETRIBUTION) {
            return true;
        }
        break;
    case CLASS_DEATH_KNIGHT:
        if (tab != DEATHKNIGT_TAB_BLOOD) {
            return true;
        }
        break;
    case CLASS_WARRIOR:
        if (tab != WARRIOR_TAB_PROTECTION) {
            return true;
        }
        break;
    }
    return false;
}

namespace MaNGOS
{

    class UnitByGuidInRangeCheck
    {
    public:
        UnitByGuidInRangeCheck(WorldObject const* obj, ObjectGuid guid, float range) : i_obj(obj), i_range(range), i_guid(guid) {}
        WorldObject const& GetFocusObject() const { return *i_obj; }
        bool operator()(Unit* u)
        {
            return u->GetGUID() == i_guid && i_obj->IsWithinDistInMap(u, i_range);
        }
    private:
        WorldObject const* i_obj;
        float i_range;
        ObjectGuid i_guid;
    };

    class GameObjectByGuidInRangeCheck
    {
    public:
        GameObjectByGuidInRangeCheck(WorldObject const* obj, ObjectGuid guid, float range) : i_obj(obj), i_range(range), i_guid(guid) {}
        WorldObject const& GetFocusObject() const { return *i_obj; }
        bool operator()(GameObject* u)
        {
            if (u && i_obj->IsWithinDistInMap(u, i_range) && u->isSpawned() && u->GetGOInfo() && u->GetGUID() == i_guid)
                return true;

            return false;
        }
    private:
        WorldObject const* i_obj;
        float i_range;
        ObjectGuid i_guid;
    };

};


Unit* PlayerbotAI::GetUnit(ObjectGuid guid)
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return ObjectAccessor::GetUnit(*bot, guid);
}


Creature* PlayerbotAI::GetCreature(ObjectGuid guid)
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return map->GetCreature(guid);
}

GameObject* PlayerbotAI::GetGameObject(ObjectGuid guid)
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return map->GetGameObject(guid);
}

bool PlayerbotAI::TellMasterNoFacing(string text, PlayerbotSecurityLevel securityLevel)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    if (!GetSecurity()->CheckLevelFor(securityLevel, true, master))
        return false;

    if (sPlayerbotAIConfig.whisperDistance && !bot->GetGroup() && sRandomPlayerbotMgr.IsRandomBot(bot) &&
            master->GetSession()->GetSecurity() < SEC_GAMEMASTER &&
            (bot->GetMapId() != master->GetMapId() || bot->GetDistance(master) > sPlayerbotAIConfig.whisperDistance))
        return false;

    bot->Whisper(text, LANG_UNIVERSAL, master);
    return true;
}

bool PlayerbotAI::TellMaster(string text, PlayerbotSecurityLevel securityLevel)
{
    if (!TellMasterNoFacing(text, securityLevel))
        return false;

    if (!bot->isMoving() && !bot->IsInCombat() && bot->GetMapId() == master->GetMapId())
    {
        if (!bot->isInFront(master, M_PI / 2))
            bot->SetFacingTo(bot->GetAngle(master));

        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
    }

    return true;
}

bool IsRealAura(Player* bot, Aura const* aura, Unit* unit)
{
    if (!aura)
        return false;

    if (!unit->IsHostileTo(bot))
        return true;

    uint32 stacks = aura->GetStackAmount();
    if (stacks >= aura->GetSpellInfo()->StackAmount)
        return true;

    if (aura->GetCaster() == bot || aura->GetSpellInfo()->IsPositive() || aura->IsArea())
        return true;

    return false;
}

bool PlayerbotAI::HasAura(string name, Unit* unit)
{
    if (!unit)
        return false;

    wstring wnamepart;
    if (!Utf8toWStr(name, wnamepart))
        return 0;

    wstrToLower(wnamepart);

    Unit::AuraApplicationMap& map = unit->GetAppliedAuras();
    // if (bot->GetGroup()) {
    //     for (Unit::AuraApplicationMap::iterator i = map.begin(); i != map.end(); ++i)
    //     {
    //         Aura const* aura  = i->second->GetBase();
    //         if (!aura)
    //             continue;
    //         const string auraName = aura->GetSpellInfo()->SpellName[0];
    //         sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "PlayerbotAI::HasAura %s %s %s", auraName.c_str(), name.c_str(), unit->GetName());
    //     }
    // }
    for (Unit::AuraApplicationMap::iterator i = map.begin(); i != map.end(); ++i)
    {
        Aura const* aura  = i->second->GetBase();
        if (!aura)
            continue;

        const string auraName = aura->GetSpellInfo()->SpellName[0];
        if (auraName.empty() || auraName.length() != wnamepart.length() || !Utf8FitTo(auraName, wnamepart))
            continue;

        if (IsRealAura(bot, aura, unit))
            return true;
    }

    return false;
}

bool PlayerbotAI::HasAuraWithDuration(string name, Unit* unit)
{
    if (!unit)
        return false;

    wstring wnamepart;
    if (!Utf8toWStr(name, wnamepart))
        return 0;

    wstrToLower(wnamepart);

    Unit::AuraApplicationMap& map = unit->GetAppliedAuras();
    for (Unit::AuraApplicationMap::iterator i = map.begin(); i != map.end(); ++i)
    {
        Aura const* aura  = i->second->GetBase();
        if (!aura)
            continue;
        if (aura->GetDuration() <= 0) {
            continue;
        }
        const string auraName = aura->GetSpellInfo()->SpellName[0];
        if (auraName.empty() || auraName.length() != wnamepart.length() || !Utf8FitTo(auraName, wnamepart))
            continue;

        if (IsRealAura(bot, aura, unit))
            return true;
    }

    return false;
}

bool PlayerbotAI::HasAuraFromBot(string name, Unit* unit)
{
    if (!unit)
        return false;

    wstring wnamepart;
    if (!Utf8toWStr(name, wnamepart))
        return 0;

    wstrToLower(wnamepart);

    Unit::AuraApplicationMap& map = unit->GetAppliedAuras();
    for (Unit::AuraApplicationMap::iterator i = map.begin(); i != map.end(); ++i)
    {
        Aura const* aura  = i->second->GetBase();
        if (!aura)
            continue;
        const string auraName = aura->GetSpellInfo()->SpellName[0];
        if (auraName.empty() || auraName.length() != wnamepart.length() || !Utf8FitTo(auraName, wnamepart))
            continue;
        if (aura->GetCaster() != bot) {
            continue;
        }

        if (IsRealAura(bot, aura, unit))
            return true;
    }

    return false;
}

Aura* PlayerbotAI::GetAura(string name, Unit* unit)
{
    if (!unit)
        return nullptr;

    wstring wnamepart;
    if (!Utf8toWStr(name, wnamepart))
        return nullptr;

    wstrToLower(wnamepart);

    Unit::AuraApplicationMap& map = unit->GetAppliedAuras();
    for (Unit::AuraApplicationMap::iterator i = map.begin(); i != map.end(); ++i)
    {
        Aura * aura  = i->second->GetBase();
        if (!aura)
            continue;

        const string auraName = aura->GetSpellInfo()->SpellName[0];
        if (auraName.empty() || auraName.length() != wnamepart.length() || !Utf8FitTo(auraName, wnamepart))
            continue;

        if (IsRealAura(bot, aura, unit))
            return aura;
    }

    return nullptr;
}

Aura* PlayerbotAI::GetAuraWithDuration(string name, Unit* unit, bool from_bot)
{
    if (!unit)
        return nullptr;

    wstring wnamepart;
    if (!Utf8toWStr(name, wnamepart))
        return nullptr;

    wstrToLower(wnamepart);

    Unit::AuraApplicationMap& map = unit->GetAppliedAuras();
    for (Unit::AuraApplicationMap::iterator i = map.begin(); i != map.end(); ++i)
    {
        Aura * aura  = i->second->GetBase();
        if (!aura)
            continue;
        if (aura->GetDuration() <= 0) {
            continue;
        }
        if (from_bot && aura->GetCaster() != bot) {
            continue;
        }
        const string auraName = aura->GetSpellInfo()->SpellName[0];
        if (auraName.empty() || auraName.length() != wnamepart.length() || !Utf8FitTo(auraName, wnamepart))
            continue;

        if (IsRealAura(bot, aura, unit))
            return aura;
    }
    return nullptr;
}

bool PlayerbotAI::HasAura(uint32 spellId, const Unit* unit)
{
    if (!spellId || !unit)
        return false;
    
    for (uint32 effect = EFFECT_0; effect <= EFFECT_2; effect++)
    {
        Aura* aura = ((Unit*)unit)->GetAura(spellId);

        if (IsRealAura(bot, aura, (Unit*)unit))
            return true;
    }

    return false;
}

bool PlayerbotAI::HasAnyAuraOf(Unit* player, ...)
{
    if (!player)
        return false;

    va_list vl;
    va_start(vl, player);

    const char* cur;
    do {
        cur = va_arg(vl, const char*);
        if (cur && HasAura(cur, player)) {
            va_end(vl);
            return true;
        }
    }
    while (cur);

    va_end(vl);
    return false;
}

bool PlayerbotAI::CanCastSpell(string name, Unit* target)
{
    return CanCastSpell(aiObjectContext->GetValue<uint32>("spell id", name)->Get(), target);
}

bool PlayerbotAI::CanCastSpell(uint32 spellid, Unit* target, bool checkHasSpell)
{
    if (!spellid)
        return false;
    if (!target)
        target = bot;
    if (!sPlayerbotAIConfig.logInGroupOnly || bot->GetGroup())
        sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "CanCastSpell() target name: %s, spellid: %d, bot name: %s", 
            target->GetName(), spellid, bot->GetName());
    if (checkHasSpell && !bot->HasSpell(spellid))
        return false;

    if (bot->GetSpellHistory()->HasCooldown(spellid))
        return false;

    if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) != NULL) {
        sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "CanCastSpell() target name: %s, spellid: %d, bot name: %s, failed because has current channeled spell", 
            target->GetName(), spellid, bot->GetName());
        return false;
    }
    SpellInfo const *spellInfo = sSpellMgr->GetSpellInfo(spellid );
    if (!spellInfo)
        return false;
    bool positiveSpell = spellInfo->IsPositive();
    // if (positiveSpell && bot->IsHostileTo(target))
    //     return false;
    // if (!positiveSpell && bot->IsFriendlyTo(target))
    //     return false;
    if (target->IsImmunedToSpell(spellInfo))
        return false;
    if (bot != target && bot->GetDistance(target) > sPlayerbotAIConfig.sightDistance)
        return false;
    Unit* oldSel = bot->GetSelectedUnit();
    bot->SetSelection(target->GetGUID());
    Spell *spell = new Spell(bot, spellInfo, TRIGGERED_NONE);

    spell->m_targets.SetUnitTarget(target);
    spell->m_CastItem = aiObjectContext->GetValue<Item*>("item for spell", spellid)->Get();
    spell->m_targets.SetItemTarget(spell->m_CastItem);
    SpellCastResult result = spell->CheckCast(false);
    if (!sPlayerbotAIConfig.logInGroupOnly || bot->GetGroup())
        sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "CanCastSpell() target name: %s, spellid: %d, bot name: %s, result: %d", 
            target->GetName(), spellid, bot->GetName(), result);
    delete spell;
	if (oldSel)
		bot->SetSelection(oldSel->GetGUID());
    switch (result)
    {
    case SPELL_FAILED_NOT_INFRONT:
    case SPELL_FAILED_NOT_STANDING:
    case SPELL_FAILED_UNIT_NOT_INFRONT:
    case SPELL_FAILED_SUCCESS:
    case SPELL_FAILED_MOVING:
    case SPELL_FAILED_TRY_AGAIN:
    case SPELL_FAILED_NOT_IDLE:
    case SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW:
    case SPELL_FAILED_SUMMON_PENDING:
    case SPELL_FAILED_BAD_IMPLICIT_TARGETS:
    case SPELL_FAILED_BAD_TARGETS:
    case SPELL_CAST_OK:
    case SPELL_FAILED_ITEM_NOT_FOUND:
        return true;
    default:
        return false;
    }
}


bool PlayerbotAI::CastSpell(string name, Unit* target)
{
    bool result = CastSpell(aiObjectContext->GetValue<uint32>("spell id", name)->Get(), target);
    if (result)
    {
        aiObjectContext->GetValue<time_t>("last spell cast time", name)->Set(time(0));
    }

    return result;
}

bool PlayerbotAI::CastSpell(uint32 spellId, Unit* target)
{   
    if (!spellId)
        return false;

    if (!target)
        target = bot;

    if (!sPlayerbotAIConfig.logInGroupOnly || bot->GetGroup())
        sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "CastSpell() target name: %s, spellid: %d, bot name: %s", 
            target->GetName(), spellId, bot->GetName());
            
    Pet* pet = bot->GetPet();
    const SpellInfo* const pSpellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (pet && pet->HasSpell(spellId))
    {
        pet->GetCharmInfo()->SetSpellAutocast(pSpellInfo, true);
        pet->GetCharmInfo()->ToggleCreatureAutocast(pSpellInfo, true);
        TellMaster("My pet will auto-cast this spell");
        return true;
    }

    aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get().Set(spellId, target->GetGUID(), time(0));
    aiObjectContext->GetValue<LastMovement&>("last movement")->Get().Set(NULL);

    MotionMaster &mm = *bot->GetMotionMaster();

    if (bot->IsFlying()) {
        if (!sPlayerbotAIConfig.logInGroupOnly || bot->GetGroup())
            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "CastSpell() failed because flying");
        return false;
    }

    bot->ClearUnitState( UNIT_STATE_ALL_STATE_SUPPORTED );

    Unit* oldSel = bot->GetSelectedUnit();
    bot->SetSelection(target->GetGUID());

    Spell *spell = new Spell(bot, pSpellInfo, TRIGGERED_NONE);
    if (bot->isMoving() && spell->GetCastTime())
    {
        if (!sPlayerbotAIConfig.logInGroupOnly || bot->GetGroup())
            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "CastSpell() failed because is moving and spell get cast time.");
        delete spell;
        return false;
    }

    SpellCastTargets targets;
    WorldObject* faceTo = target;

    if (pSpellInfo->Targets & TARGET_FLAG_SOURCE_LOCATION ||
            pSpellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
    {
        targets.SetDst(target->GetPosition());
    }
	else if (pSpellInfo->Targets & TARGET_FLAG_ITEM)
	{
		spell->m_CastItem = aiObjectContext->GetValue<Item*>("item for spell", spellId)->Get();
		targets.SetItemTarget(spell->m_CastItem);
	}
    else
    {
        targets.SetUnitTarget(target);
    }

    if (pSpellInfo->Effects[0].Effect == SPELL_EFFECT_OPEN_LOCK ||
        pSpellInfo->Effects[0].Effect == SPELL_EFFECT_SKINNING)
    {
        LootObject loot = *aiObjectContext->GetValue<LootObject>("loot target");
        if (!loot.IsLootPossible(bot))
        {
            delete spell;
            if (!sPlayerbotAIConfig.logInGroupOnly || bot->GetGroup())
                sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "CastSpell() failed because is loot possible");
            return false;
        }

        GameObject* go = GetGameObject(loot.guid);
        if (go && go->isSpawned())
        {
            WorldPacket* const packetgouse = new WorldPacket(CMSG_GAMEOBJ_REPORT_USE, 8);
            *packetgouse << loot.guid;
            bot->GetSession()->QueuePacket(packetgouse);
            targets.SetGOTarget(go);
            faceTo = go;
        }
        else
        {
            Unit* creature = GetUnit(loot.guid);
            if (creature)
            {
                targets.SetUnitTarget(creature);
                faceTo = creature;
            }
        }
    }


    if (!bot->isInFront(faceTo, M_PI / 2))
    {
        bot->SetFacingTo(bot->GetAngle(faceTo));
        delete spell;
        SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
        if (!sPlayerbotAIConfig.logInGroupOnly || bot->GetGroup())
            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "CastSpell() failed because is not in front");
        return true;
    }
	if (spell->GetCastTime()>0)
		bot->GetMotionMaster()->MovementExpired();
	spell->prepare(&targets);
	WaitForSpellCast(spell);
	if (oldSel)
		bot->SetSelection(oldSel->GetGUID());
	LastSpellCast& lastSpell = aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get();
    if (lastSpell.id != spellId) {
        if (!sPlayerbotAIConfig.logInGroupOnly || bot->GetGroup())
            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "CastSpell() failed because lastSpell.id != spellId");
    }
	return lastSpell.id == spellId;
}

void PlayerbotAI::WaitForSpellCast(Spell *spell)
{
    const SpellInfo* const pSpellInfo = spell->GetSpellInfo();

    float castTime = spell->GetCastTime();
    // if (pSpellInfo->IsChanneled())
    // {
    //     int32 duration = pSpellInfo->GetDuration();
    //     // mod spell duration (for haste and aura)
    //     bot->ApplySpellMod(pSpellInfo->Id, SPELLMOD_DURATION, duration);
    //     bot->ModSpellDurationTime(pSpellInfo, duration, spell);
    //     sLog->outMessage("playerbot", LOG_LEVEL_DEBUG, "PlayerbotAI::WaitForSpellCast %.4f %d %s", castTime, duration, pSpellInfo->SpellName[0]);
    //     if (duration > 2000) {
    //         duration = 2000;
    //     }
    //     if (duration > 0)
    //         castTime += duration;
    // }

    int32 ceiled_castTime = ceil(castTime);
    uint32 globalCooldown = CalculateGlobalCooldown(pSpellInfo->Id);
    if (ceiled_castTime < globalCooldown)
        ceiled_castTime = globalCooldown;
    SetNextCheckDelay(ceiled_castTime + sPlayerbotAIConfig.reactDelay);
}

void PlayerbotAI::InterruptSpell()
{
    for (int type = CURRENT_MELEE_SPELL; type < CURRENT_CHANNELED_SPELL; type++)
    {
        Spell* spell = bot->GetCurrentSpell((CurrentSpellTypes)type);
        if (!spell)
            continue;

        if (spell->m_spellInfo->IsPositive())
            continue;

        bot->InterruptSpell((CurrentSpellTypes)type);

        WorldPacket data(SMSG_SPELL_FAILURE, 8 + 1 + 4 + 1);
        data.appendPackGUID(bot->GetGUID());
        data << uint8(1);
        data << uint32(spell->m_spellInfo->Id);
        data << uint8(0);
        bot->SendMessageToSet(&data, true);

        data.Initialize(SMSG_SPELL_FAILED_OTHER, 8 + 1 + 4 + 1);
        data.appendPackGUID(bot->GetGUID());
        data << uint8(1);
        data << uint32(spell->m_spellInfo->Id);
        data << uint8(0);
        bot->SendMessageToSet(&data, true);

        SpellInterrupted(spell->m_spellInfo->Id);
    }
}


void PlayerbotAI::RemoveAura(string name)
{
    uint32 spellid = aiObjectContext->GetValue<uint32>("spell id", name)->Get();
    if (spellid && HasAura(spellid, bot))
        bot->RemoveAurasDueToSpell(spellid);
}

bool PlayerbotAI::IsInterruptableSpellCasting(Unit* target, string spell)
{
    uint32 spellid = aiObjectContext->GetValue<uint32>("spell id", spell)->Get();
    if (!spellid || !target->IsNonMeleeSpellCast(true))
        return false;

    SpellInfo const *spellInfo = sSpellMgr->GetSpellInfo(spellid );
    if (!spellInfo)
        return false;

    if (target->IsImmunedToSpell(spellInfo))
        return false;

    for (uint32 i = EFFECT_0; i <= EFFECT_2; i++)
    {
        if ((spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT) && spellInfo->PreventionType == SPELL_PREVENTION_TYPE_SILENCE)
            return true;

        if ((spellInfo->Effects[i].Effect == SPELL_EFFECT_REMOVE_AURA || spellInfo->Effects[i].Effect == SPELL_EFFECT_INTERRUPT_CAST) &&
                !target->IsImmunedToSpellEffect(spellInfo, i))
            return true;
    }

    return false;
}

bool PlayerbotAI::HasAuraToDispel(Unit* target, uint32 dispelType)
{
    for (uint32 type = SPELL_AURA_NONE; type < TOTAL_AURAS; ++type)
    {
        Unit::AuraEffectList const& auras = target->GetAuraEffectsByType((AuraType)type);
        for (Unit::AuraEffectList::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        {
            const AuraEffect *const aura = *itr;
			const SpellInfo* entry = aura->GetSpellInfo();
            uint32 spellId = entry->Id;

            bool isPositiveSpell = entry->IsPositive();
            if (isPositiveSpell && bot->IsFriendlyTo(target))
                continue;
            if (entry->GetDuration() <= 0)
                continue;
            if (!isPositiveSpell && bot->IsHostileTo(target))
                continue;

            if (canDispel(entry, dispelType)) {
                return true;
            }
        }
    }
    return false;
}


#ifndef WIN32
inline int strcmpi(const char* s1, const char* s2)
{
    for (; *s1 && *s2 && (toupper(*s1) == toupper(*s2)); ++s1, ++s2);
    return *s1 - *s2;
}
#endif

bool PlayerbotAI::canDispel(const SpellInfo* entry, uint32 dispelType)
{
    if (entry->Dispel != dispelType)
        return false;
    
    if (!entry->SpellName[0]) {
        return true;
    }

    for (string &str : dispel_whitelist) {
        if (strcmpi((const char*)entry->SpellName[0], str.c_str()) == 0) {
            return false;
        }
    }
    
    return strcmpi((const char*)entry->SpellName[0], "demon skin") &&
        strcmpi((const char*)entry->SpellName[0], "mage armor") &&
        strcmpi((const char*)entry->SpellName[0], "frost armor") &&
        strcmpi((const char*)entry->SpellName[0], "wavering will") &&
        strcmpi((const char*)entry->SpellName[0], "chilled");
        // &&
        // strcmpi((const char*)entry->SpellName[0], "frost fever") &&
        // strcmpi((const char*)entry->SpellName[0], "blood plague"));
}

bool IsAlliance(uint8 race)
{
    return race == RACE_HUMAN || race == RACE_DWARF || race == RACE_NIGHTELF ||
            race == RACE_GNOME || race == RACE_DRAENEI;
}

bool PlayerbotAI::IsOpposing(Player* player)
{
    return IsOpposing(player->getRace(), bot->getRace());
}

bool PlayerbotAI::IsOpposing(uint8 race1, uint8 race2)
{
    return (IsAlliance(race1) && !IsAlliance(race2)) || (!IsAlliance(race1) && IsAlliance(race2));
}

void PlayerbotAI::RemoveShapeshift()
{
    RemoveAura("bear form");
    RemoveAura("dire bear form");
    RemoveAura("moonkin form");
    RemoveAura("travel form");
    RemoveAura("cat form");
    RemoveAura("flight form");
    RemoveAura("swift flight form");
    RemoveAura("aquatic form");
    RemoveAura("ghost wolf");
    // RemoveAura("tree of life");
}

uint32 PlayerbotAI::GetEquipGearScore(Player* player, bool withBags, bool withBank)
{
    std::vector<uint32> gearScore(EQUIPMENT_SLOT_END);
    uint32 twoHandScore = 0;

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            _fillGearScoreData(player, item, &gearScore, twoHandScore);
    }

    if (withBags)
    {
        // check inventory
        for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                _fillGearScoreData(player, item, &gearScore, twoHandScore);
        }

        // check bags
        for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            if (Bag* pBag = (Bag*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    if (Item* item2 = pBag->GetItemByPos(j))
                        _fillGearScoreData(player, item2, &gearScore, twoHandScore);
                }
            }
        }
    }

    if (withBank)
    {
        for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                _fillGearScoreData(player, item, &gearScore, twoHandScore);
        }

        for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                if (item->IsBag())
                {
                    Bag* bag = (Bag*)item;
                    for (uint8 j = 0; j < bag->GetBagSize(); ++j)
                    {
                        if (Item* item2 = bag->GetItemByPos(j))
                            _fillGearScoreData(player, item2, &gearScore, twoHandScore);
                    }
                }
            }
        }
    }

    uint8 count = EQUIPMENT_SLOT_END - 2;   // ignore body and tabard slots
    uint32 sum = 0;

    // check if 2h hand is higher level than main hand + off hand
    if (gearScore[EQUIPMENT_SLOT_MAINHAND] + gearScore[EQUIPMENT_SLOT_OFFHAND] < twoHandScore * 2)
    {
        gearScore[EQUIPMENT_SLOT_OFFHAND] = 0;  // off hand is ignored in calculations if 2h weapon has higher score
        --count;
        gearScore[EQUIPMENT_SLOT_MAINHAND] = twoHandScore;
    }

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
       sum += gearScore[i];
    }

    if (count)
    {
        uint32 res = uint32(sum / count);
        return res;
    }
    else
        return 0;
}

void PlayerbotAI::_fillGearScoreData(Player *player, Item* item, std::vector<uint32>* gearScore, uint32& twoHandScore)
{
    if (!item)
        return;

    if (player->CanUseItem(item->GetTemplate()) != EQUIP_ERR_OK)
        return;

    uint8 type   = item->GetTemplate()->InventoryType;
    uint32 level = item->GetTemplate()->ItemLevel;

    switch (type)
    {
        case INVTYPE_2HWEAPON:
            twoHandScore = std::max(twoHandScore, level);
            break;
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND:
            (*gearScore)[SLOT_MAIN_HAND] = std::max((*gearScore)[SLOT_MAIN_HAND], level);
            break;
        case INVTYPE_SHIELD:
        case INVTYPE_WEAPONOFFHAND:
            (*gearScore)[EQUIPMENT_SLOT_OFFHAND] = std::max((*gearScore)[EQUIPMENT_SLOT_OFFHAND], level);
            break;
        case INVTYPE_THROWN:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_RANGED:
        case INVTYPE_QUIVER:
        case INVTYPE_RELIC:
            (*gearScore)[EQUIPMENT_SLOT_RANGED] = std::max((*gearScore)[EQUIPMENT_SLOT_RANGED], level);
            break;
        case INVTYPE_HEAD:
            (*gearScore)[EQUIPMENT_SLOT_HEAD] = std::max((*gearScore)[EQUIPMENT_SLOT_HEAD], level);
            break;
        case INVTYPE_NECK:
            (*gearScore)[EQUIPMENT_SLOT_NECK] = std::max((*gearScore)[EQUIPMENT_SLOT_NECK], level);
            break;
        case INVTYPE_SHOULDERS:
            (*gearScore)[EQUIPMENT_SLOT_SHOULDERS] = std::max((*gearScore)[EQUIPMENT_SLOT_SHOULDERS], level);
            break;
        case INVTYPE_BODY:
            (*gearScore)[EQUIPMENT_SLOT_BODY] = std::max((*gearScore)[EQUIPMENT_SLOT_BODY], level);
            break;
        case INVTYPE_CHEST:
            (*gearScore)[EQUIPMENT_SLOT_CHEST] = std::max((*gearScore)[EQUIPMENT_SLOT_CHEST], level);
            break;
        case INVTYPE_WAIST:
            (*gearScore)[EQUIPMENT_SLOT_WAIST] = std::max((*gearScore)[EQUIPMENT_SLOT_WAIST], level);
            break;
        case INVTYPE_LEGS:
            (*gearScore)[EQUIPMENT_SLOT_LEGS] = std::max((*gearScore)[EQUIPMENT_SLOT_LEGS], level);
            break;
        case INVTYPE_FEET:
            (*gearScore)[EQUIPMENT_SLOT_FEET] = std::max((*gearScore)[EQUIPMENT_SLOT_FEET], level);
            break;
        case INVTYPE_WRISTS:
            (*gearScore)[EQUIPMENT_SLOT_WRISTS] = std::max((*gearScore)[EQUIPMENT_SLOT_WRISTS], level);
            break;
        case INVTYPE_HANDS:
            (*gearScore)[EQUIPMENT_SLOT_HEAD] = std::max((*gearScore)[EQUIPMENT_SLOT_HEAD], level);
            break;
        // equipped gear score check uses both rings and trinkets for calculation, assume that for bags/banks it is the same
        // with keeping second highest score at second slot
        case INVTYPE_FINGER:
        {
            if ((*gearScore)[EQUIPMENT_SLOT_FINGER1] < level)
            {
                (*gearScore)[EQUIPMENT_SLOT_FINGER2] = (*gearScore)[EQUIPMENT_SLOT_FINGER1];
                (*gearScore)[EQUIPMENT_SLOT_FINGER1] = level;
            }
            else if ((*gearScore)[EQUIPMENT_SLOT_FINGER2] < level)
                (*gearScore)[EQUIPMENT_SLOT_FINGER2] = level;
            break;
        }
        case INVTYPE_TRINKET:
        {
            if ((*gearScore)[EQUIPMENT_SLOT_TRINKET1] < level)
            {
                (*gearScore)[EQUIPMENT_SLOT_TRINKET2] = (*gearScore)[EQUIPMENT_SLOT_TRINKET1];
                (*gearScore)[EQUIPMENT_SLOT_TRINKET1] = level;
            }
            else if ((*gearScore)[EQUIPMENT_SLOT_TRINKET2] < level)
                (*gearScore)[EQUIPMENT_SLOT_TRINKET2] = level;
            break;
        }
        case INVTYPE_CLOAK:
            (*gearScore)[EQUIPMENT_SLOT_BACK] = std::max((*gearScore)[EQUIPMENT_SLOT_BACK], level);
            break;
        default:
            break;
    }
}

string PlayerbotAI::HandleRemoteCommand(string command)
{
    if (command == "state")
    {
        switch (currentState)
        {
        case BOT_STATE_COMBAT:
            return "combat";
        case BOT_STATE_DEAD:
            return "dead";
        case BOT_STATE_NON_COMBAT:
            return "non-combat";
        default:
            return "unknown";
        }
    }
    else if (command == "position")
    {
        ostringstream out; out << bot->GetPositionX() << " " << bot->GetPositionY() << " " << bot->GetPositionZ() << " " << bot->GetMapId() << " " << bot->GetOrientation();
        return out.str();
    }
    else if (command == "tpos")
    {
        Unit* target = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (!target) {
            return "";
        }

        ostringstream out; out << target->GetPositionX() << " " << target->GetPositionY() << " " << target->GetPositionZ() << " " << target->GetMapId() << " " << target->GetOrientation();
        return out.str();
    }
    else if (command == "movement")
    {
        LastMovement& data = *GetAiObjectContext()->GetValue<LastMovement&>("last movement");
        ostringstream out; out << data.lastMoveToX << " " << data.lastMoveToY << " " << data.lastMoveToZ << " " << bot->GetMapId() << " " << data.lastMoveToOri;
        return out.str();
    }
    else if (command == "target")
    {
        Unit* target = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (!target) {
            return "";
        }

        return target->GetName();
    }
    else if (command == "hp")
    {
        int pct = (int)((static_cast<float> (bot->GetHealth()) / bot->GetMaxHealth()) * 100);
        ostringstream out; out << pct << "%";

        Unit* target = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (!target) {
            return out.str();
        }

        pct = (int)((static_cast<float> (target->GetHealth()) / target->GetMaxHealth()) * 100);
        out << " / " << pct << "%";
        return out.str();
    }
    else if (command == "strategy")
    {
        return currentEngine->ListStrategies();
    }
    else if (command == "action")
    {
        return currentEngine->GetLastAction();
    }
    else if (command == "values")
    {
        return GetAiObjectContext()->FormatValues();
    }
    ostringstream out; out << "invalid command: " << command;
    return out.str();
}

bool PlayerbotAI::EqualLowercaseName(string s1, string s2)
{
    if (s1.length() != s2.length()) {
        return false;
    }
    for (int i = 0; i < s1.length(); i++) {
        if (tolower(s1[i]) != tolower(s2[i])) {
            return false;
        }
    }
    return true;
}