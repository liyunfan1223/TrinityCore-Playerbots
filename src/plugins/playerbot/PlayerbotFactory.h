#pragma once

#include "strategy/actions/InventoryAction.h"

class Player;
class PlayerbotMgr;
class ChatHandler;

using namespace std;
using ai::InventoryAction;

class PlayerbotFactory : public InventoryAction
{
public:
    PlayerbotFactory(Player* bot, uint32 level, uint32 itemQuality = 0) :
        bot(bot), level(level), itemQuality(itemQuality), InventoryAction(bot->GetPlayerbotAI(), "factory") {}

    static ObjectGuid GetRandomBot();
    void CleanRandomize();
    void Randomize();
    void Refresh(); 
    void Prepare();
    void InitSecondEquipmentSet();
    void InitEquipment(bool incremental);
    bool CanEquipItem(ItemTemplate const* proto, uint32 desiredQuality);
    bool CanEquipUnseenItem(uint8 slot, uint16 &dest, uint32 item);
    void InitSkills();
    void InitTradeSkills();
    void UpdateTradeSkills();
    void SetRandomSkill(uint16 id);
    void InitSpells();
    void ClearSpells();
    void InitAvailableSpells();
    void InitClassSpells();
    void InitSpecialSpells();
    void InitTalents(bool increment = false, bool use_template = true);
    void InitTalents(uint32 specNo);
    void InitTalentsByTemplate(uint32 specNo);
    void InitQuests();
    void InitPet();
    void ClearInventory();
    void InitAmmo();
    void InitMounts();
    void InitPotions();
    void InitFood();
    void InitClassItems();
    bool CanEquipArmor(ItemTemplate const* proto);
    bool CanEquipWeapon(ItemTemplate const* proto);
    void EnchantItem(Item* item);
    void AddItemStats(uint32 mod, uint8 &sp, uint8 &ap, uint8 &tank);
    bool CheckItemStats(uint8 sp, uint8 ap, uint8 tank);
    void CancelAuras();
    bool IsDesiredReplacement(Item* item);
    void InitBags();
    void InitInventory();
    void InitInventoryTrade();
    void InitInventoryEquip();
    void InitInventorySkill();
    Item* StoreItem(uint32 itemId, uint32 count);
    void InitGlyphs();
    void InitGuild();

private:
    void Randomize(bool incremental);
    float CalculateItemScore(uint32 item_id);
    bool IsShieldTank();
    bool NotSameArmorType(uint32 item_subclass_armor) {
        if (bot->HasSkill(SKILL_PLATE_MAIL)) {
            return item_subclass_armor != ITEM_SUBCLASS_ARMOR_PLATE;
        }
        if (bot->HasSkill(SKILL_MAIL)) {
            return item_subclass_armor != ITEM_SUBCLASS_ARMOR_MAIL;
        }
        if (bot->HasSkill(SKILL_LEATHER)) {
            return item_subclass_armor != ITEM_SUBCLASS_ARMOR_LEATHER;
        }
        return false;
    }
/*
    void Prepare(); 
    void InitSecondEquipmentSet();
    void InitEquipment(bool incremental);
    bool CanEquipItem(ItemTemplate const* proto, uint32 desiredQuality);
    bool CanEquipUnseenItem(uint8 slot, uint16 &dest, uint32 item);
    void InitSkills();
    void InitTradeSkills();
    void UpdateTradeSkills();
    void SetRandomSkill(uint16 id);
    void InitSpells();
    void ClearSpells();
    void InitAvailableSpells();
    void InitSpecialSpells();
    void InitTalents();
    void InitTalents(uint32 specNo);
    void InitQuests();
    void InitPet();
    void ClearInventory();
    void InitAmmo();
    void InitMounts();
    void InitPotions();
    void InitFood();
    bool CanEquipArmor(ItemTemplate const* proto);
    bool CanEquipWeapon(ItemTemplate const* proto);
    void EnchantItem(Item* item);
    void AddItemStats(uint32 mod, uint8 &sp, uint8 &ap, uint8 &tank);
    bool CheckItemStats(uint8 sp, uint8 ap, uint8 tank);
    void CancelAuras();
    bool IsDesiredReplacement(Item* item);
    void InitBags();
    void InitInventory();
    void InitInventoryTrade();
    void InitInventoryEquip();
    void InitInventorySkill();
    Item* StoreItem(uint32 itemId, uint32 count);
    void InitGlyphs();
    void InitGuild();
*/
public: //private:
    Player* bot;
    uint32 level;
    uint32 itemQuality;
    static uint32 tradeSkills[];
    static std::vector<std::vector<uint32>> default_talents[12][3];
};
