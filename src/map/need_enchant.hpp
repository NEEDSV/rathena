// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED Unified Enchant V2: rules layered on top of item_enchant.yml.
//
// item_enchant.yml keeps everything the client also knows (targets, order, costs, option
// weights, perfect enchants, upgrades). db/need/enchant_rules.yml adds what the 2025 client
// cannot express:
//  - failure outcomes that share one integer denominator with the option weights
//    (a slot rolls options + failures together, so 180:25:5 / 210 stays exact)
//  - upgrade success/failure outcomes, reset outcomes (incl. destroy and rewards)
//  - conditions: maximum refine, per-slot minimum refine, required enchants, per-enchant caps,
//    reset only when every slot is filled
// A group without rules behaves exactly like the stock engine. The rules file is optional.
//
// db/need/enchant_rules.yml (Header Type NEED_ENCHANT_RULES_DB, Version 1):
//  - Id                         item_enchant.yml group Id (= client EnchantList Table index)
//    MaximumRefine              highest refine allowed (Default: 0 = no limit)
//    Slots:
//      - Slot                   card[] index 0..3, must exist in the item enchant group
//        MinimumRefine          refine needed for this slot (Default: 0)
//        Require:               every listed slot must hold one of the enchants
//          - Slot               card[] index
//            Enchants:          list of enchant item names
//        MaxSame:               per-enchant cap on the item (capped options leave the roll)
//          - Enchant            enchant item name
//            Max                highest count
//        Failures:              rolled together with the option weights of item_enchant.yml
//                               (the slot's item_enchant Chance must stay 100000)
//          - Result             FAIL_KEEP | CLEAR_SLOTS | CLEAR_ENCHANTS | REFINE_DOWN | DESTROY | REWARD | DESTROY_WITH_REWARD
//            Weight             integer weight (same denominator as the option Chance values)
//            Message            msgstringtable id shown by the result window (Default: per result)
//            Slots: - Slot      CLEAR_SLOTS only
//            Amount / Random    REFINE_DOWN only (fixed amount, or Random: true = uniform 0..refine)
//            Rewards:           REWARD / DESTROY_WITH_REWARD, one entry is picked by weight
//              - Item / Amount / Weight
//        Upgrades:
//          - Enchant            enchant item name, must be an Upgrades entry of the slot
//            SuccessWeight      weight of the upgrade succeeding
//            Failures:          as above, plus DOWNGRADE (To: item name)
//    Reset:
//      RequireAllFilled         every Order slot must hold an enchant (Default: false)
//      Scope: - Slot            only these enchant slots are cleared (Default: every enchant slot)
//      Outcomes:                SUCCESS | FAIL_KEEP | DESTROY | REWARD | DESTROY_WITH_REWARD with Weight
//                               (the group's item_enchant Reset.Chance must stay 100000)

#ifndef MAP_NEED_ENCHANT_HPP
#define MAP_NEED_ENCHANT_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/database.hpp>
#include <common/mmo.hpp>

#include "itemdb.hpp"

class map_session_data;

/// Result of a rolled outcome (an option pick is a success and does not use this enum).
enum e_need_enchant_result : uint8 {
	NEED_ENCHANT_SUCCESS = 0,          ///< reset / upgrade success
	NEED_ENCHANT_FAIL_KEEP,            ///< cost paid, item untouched
	NEED_ENCHANT_CLEAR_SLOTS,          ///< listed enchant slots are emptied
	NEED_ENCHANT_CLEAR_ENCHANTS,       ///< every enchant slot is emptied, real cards stay
	NEED_ENCHANT_REFINE_DOWN,          ///< refine is lowered
	NEED_ENCHANT_DOWNGRADE,            ///< upgrade failure: the enchant is replaced by a lower one
	NEED_ENCHANT_DESTROY,              ///< the item is deleted
	NEED_ENCHANT_REWARD,               ///< the item is kept and a reward is given
	NEED_ENCHANT_DESTROY_WITH_REWARD,  ///< the item is deleted and a reward is given
	NEED_ENCHANT_RESULT_MAX
};

/// REFINE_DOWN amount meaning "uniform 0 .. current refine" (enchan_upg.txt behaviour).
constexpr int32 NEED_ENCHANT_REFINE_DOWN_RANDOM = -1;

/// Which request a check or a roll belongs to.
enum e_need_enchant_operation : uint8 {
	NEED_ENCHANT_OP_NORMAL = 0,
	NEED_ENCHANT_OP_PERFECT,
	NEED_ENCHANT_OP_UPGRADE,
	NEED_ENCHANT_OP_RESET,
};

struct s_need_enchant_reward {
	t_itemid item_id;
	uint16 amount;
	uint32 weight;
};

struct s_need_enchant_outcome {
	e_need_enchant_result result;
	uint32 weight;
	std::vector<uint16> clear_slots;                              ///< CLEAR_SLOTS
	int32 refine_down;                                            ///< REFINE_DOWN: fixed amount or NEED_ENCHANT_REFINE_DOWN_RANDOM
	t_itemid downgrade_to;                                        ///< DOWNGRADE
	std::vector<std::shared_ptr<s_need_enchant_reward>> rewards; ///< REWARD / DESTROY_WITH_REWARD, one entry is picked by weight
	int32 message;                                                ///< msgstringtable id of the result window message
};

struct s_need_enchant_requirement {
	uint16 slot;
	std::vector<t_itemid> enchants;   ///< the slot must hold one of these
};

struct s_need_enchant_upgrade {
	t_itemid enchant;
	uint32 success_weight;
	std::vector<std::shared_ptr<s_need_enchant_outcome>> failures;
};

struct s_need_enchant_slot {
	uint16 slot;
	uint16 minimum_refine;
	std::vector<s_need_enchant_requirement> requirements;
	std::vector<std::shared_ptr<s_need_enchant_outcome>> failures;          ///< rolled together with the option weights
	std::unordered_map<t_itemid, uint16> max_same;                         ///< enchant -> highest count allowed on the item
	std::unordered_map<t_itemid, std::shared_ptr<s_need_enchant_upgrade>> upgrades;
};

struct s_need_enchant {
	uint64 id;
	uint16 maximum_refine;
	struct {
		bool require_all_filled;
		std::vector<uint16> scope;                                      ///< empty = every enchant slot
		std::vector<std::shared_ptr<s_need_enchant_outcome>> outcomes;  ///< empty = stock reset chance
	} reset;
	std::unordered_map<uint16, std::shared_ptr<s_need_enchant_slot>> slots;
};

class NeedEnchantRulesDatabase : public TypesafeYamlDatabase<uint64, s_need_enchant> {
private:
	bool parseOutcomes( const ryml::NodeRef& node, const std::string& name, std::vector<std::shared_ptr<s_need_enchant_outcome>>& outcomes, e_need_enchant_operation operation );
	bool parseRewards( const ryml::NodeRef& node, std::vector<std::shared_ptr<s_need_enchant_reward>>& rewards );
	bool parseItemList( const ryml::NodeRef& node, const std::string& name, std::vector<t_itemid>& items );

public:
	NeedEnchantRulesDatabase() : TypesafeYamlDatabase( "NEED_ENCHANT_RULES_DB", 1 ){
	}

	const std::string getDefaultLocation() override;
	uint64 parseBodyNode( const ryml::NodeRef& node ) override;
	void loadingFinished() override;

	bool load();     ///< the file is optional: no file = no rules (stock behaviour)
	bool reload();
	bool validate( uint64 id, bool report );
};

extern NeedEnchantRulesDatabase need_enchant_rules_db;

/// What a normal enchant roll produced: an enchant item id, or a failure outcome (nullptr = plain failure).
struct s_need_enchant_roll {
	t_itemid enchant;
	std::shared_ptr<s_need_enchant_outcome> failure;
};

// Pure rules (no session, no packets) - also used by the self test
bool need_enchant_check( const struct item& it, const item_data& data, const s_item_enchant& group, uint16 slot, e_need_enchant_operation operation, const s_item_enchant_normal* pool, t_itemid perfect_item );
s_need_enchant_roll need_enchant_roll_normal( const struct item& it, const item_data& data, const s_item_enchant& group, const s_item_enchant_slot& slot, const s_item_enchant_normal& pool );
std::shared_ptr<s_need_enchant_outcome> need_enchant_roll_outcomes( const std::vector<std::shared_ptr<s_need_enchant_outcome>>& outcomes, uint32 success_weight, bool& success );
std::shared_ptr<s_need_enchant_reward> need_enchant_pick_reward( const s_need_enchant_outcome& outcome );
bool need_enchant_mutate( struct item& it, const item_data& data, const s_need_enchant_outcome& outcome, uint16 slot );
uint16 need_enchant_count( const struct item& it, const item_data& data, t_itemid enchant );

// Session side (packets, inventory)
bool need_enchant_can_receive_rewards( map_session_data& sd, const s_item_enchant& group, uint16 slot, e_need_enchant_operation operation );
void need_enchant_apply_failure( map_session_data& sd, uint16 index, const s_need_enchant_outcome* outcome, uint16 slot );
std::shared_ptr<s_need_enchant_upgrade> need_enchant_find_upgrade( uint64 group, uint16 slot, t_itemid enchant );
std::shared_ptr<s_need_enchant> need_enchant_find( uint64 group );

#ifdef NEED_ENCHANT_TEST
bool need_enchant_selftest();
bool need_enchant_load_test_rules( const char* path );
#endif

#endif /* MAP_NEED_ENCHANT_HPP */
