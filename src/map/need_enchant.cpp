// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// NEED Unified Enchant V2: rules layered on top of item_enchant.yml, see need_enchant.hpp.

#include "need_enchant.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/utilities.hpp>

#include "clif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp"
#include "pc.hpp"

using namespace rathena;

NeedEnchantRulesDatabase need_enchant_rules_db;

namespace{
	struct s_need_enchant_result_name {
		const char* name;
		e_need_enchant_result result;
	};

	const s_need_enchant_result_name need_enchant_result_names[] = {
		{ "SUCCESS", NEED_ENCHANT_SUCCESS },
		{ "FAIL_KEEP", NEED_ENCHANT_FAIL_KEEP },
		{ "CLEAR_SLOTS", NEED_ENCHANT_CLEAR_SLOTS },
		{ "CLEAR_ENCHANTS", NEED_ENCHANT_CLEAR_ENCHANTS },
		{ "REFINE_DOWN", NEED_ENCHANT_REFINE_DOWN },
		{ "DOWNGRADE", NEED_ENCHANT_DOWNGRADE },
		{ "DESTROY", NEED_ENCHANT_DESTROY },
		{ "REWARD", NEED_ENCHANT_REWARD },
		{ "DESTROY_WITH_REWARD", NEED_ENCHANT_DESTROY_WITH_REWARD },
	};

	const char* need_enchant_result_name( e_need_enchant_result result ){
		for( const auto& entry : need_enchant_result_names ){
			if( entry.result == result ){
				return entry.name;
			}
		}

		return "?";
	}

	/// Results a request type may roll
	bool need_enchant_result_allowed( e_need_enchant_result result, e_need_enchant_operation operation ){
		switch( result ){
			case NEED_ENCHANT_SUCCESS:
				return operation == NEED_ENCHANT_OP_RESET;
			case NEED_ENCHANT_DOWNGRADE:
				return operation == NEED_ENCHANT_OP_UPGRADE;
			case NEED_ENCHANT_FAIL_KEEP:
			case NEED_ENCHANT_DESTROY:
			case NEED_ENCHANT_REWARD:
			case NEED_ENCHANT_DESTROY_WITH_REWARD:
				return true;
			case NEED_ENCHANT_CLEAR_SLOTS:
			case NEED_ENCHANT_CLEAR_ENCHANTS:
			case NEED_ENCHANT_REFINE_DOWN:
				return operation != NEED_ENCHANT_OP_RESET;
			default:
				return false;
		}
	}

	int32 need_enchant_default_message( e_need_enchant_result result ){
		switch( result ){
			case NEED_ENCHANT_SUCCESS:
				return MSI_ENCHANT_SUCCESS;
			case NEED_ENCHANT_DESTROY:
			case NEED_ENCHANT_DESTROY_WITH_REWARD:
				return MSI_ITEM_GRADE_ENCHANT_FAIL_BREAK;
			default:
				return MSI_ENCHANT_FAILED;
		}
	}

	bool need_enchant_has_reward( e_need_enchant_result result ){
		return result == NEED_ENCHANT_REWARD || result == NEED_ENCHANT_DESTROY_WITH_REWARD;
	}

	/// Every outcome list a request of this type can roll on this slot
	std::vector<const std::vector<std::shared_ptr<s_need_enchant_outcome>>*> need_enchant_outcome_lists( const s_need_enchant& rules, uint16 slot, e_need_enchant_operation operation ){
		std::vector<const std::vector<std::shared_ptr<s_need_enchant_outcome>>*> lists;

		if( operation == NEED_ENCHANT_OP_RESET ){
			lists.push_back( &rules.reset.outcomes );
			return lists;
		}

		std::shared_ptr<s_need_enchant_slot> slot_rules = util::umap_find( const_cast<s_need_enchant&>( rules ).slots, slot );

		if( slot_rules == nullptr ){
			return lists;
		}

		if( operation == NEED_ENCHANT_OP_NORMAL ){
			lists.push_back( &slot_rules->failures );
		}else if( operation == NEED_ENCHANT_OP_UPGRADE ){
			for( const auto& upgrade : slot_rules->upgrades ){
				lists.push_back( &upgrade.second->failures );
			}
		}

		return lists;
	}
}

/*==========================================
 * Database
 *------------------------------------------*/

const std::string NeedEnchantRulesDatabase::getDefaultLocation(){
	return std::string( db_path ) + "/need/enchant_rules.yml";
}

bool NeedEnchantRulesDatabase::load(){
	const std::string path = this->getDefaultLocation();
	FILE* f = fopen( path.c_str(), "r" );

	if( f == nullptr ){
		this->clear();
		ShowInfo( "No NEED enchant rules loaded ('" CL_WHITE "%s" CL_RESET "' is absent) - every item enchant group keeps the stock behaviour.\n", path.c_str() );
		return true;
	}

	fclose( f );

	return YamlDatabase::load();
}

bool NeedEnchantRulesDatabase::reload(){
	this->clear();

	return this->load();
}

bool NeedEnchantRulesDatabase::parseItemList( const ryml::NodeRef& node, const std::string& name, std::vector<t_itemid>& items ){
	if( !this->nodeExists( node, name ) ){
		return true;
	}

	for( const ryml::NodeRef& entry : node[c4::to_csubstr( name )] ){
		std::string item_name;

		c4::from_chars( entry.val(), &item_name );

		std::shared_ptr<item_data> item = item_db.search_aegisname( item_name.c_str() );

		if( item == nullptr ){
			this->invalidWarning( entry, "Unknown item \"%s\".\n", item_name.c_str() );
			return false;
		}

		items.push_back( item->nameid );
	}

	return true;
}

bool NeedEnchantRulesDatabase::parseRewards( const ryml::NodeRef& node, std::vector<std::shared_ptr<s_need_enchant_reward>>& rewards ){
	for( const ryml::NodeRef& rewardNode : node["Rewards"] ){
		std::string item_name;

		if( !this->asString( rewardNode, "Item", item_name ) ){
			return false;
		}

		std::shared_ptr<item_data> item = item_db.search_aegisname( item_name.c_str() );

		if( item == nullptr ){
			this->invalidWarning( rewardNode["Item"], "Unknown reward item \"%s\".\n", item_name.c_str() );
			return false;
		}

		std::shared_ptr<s_need_enchant_reward> reward = std::make_shared<s_need_enchant_reward>();

		reward->item_id = item->nameid;
		reward->amount = 1;
		reward->weight = 1;

		if( this->nodeExists( rewardNode, "Amount" ) && !this->asUInt16( rewardNode, "Amount", reward->amount ) ){
			return false;
		}

		if( reward->amount == 0 || reward->amount > MAX_AMOUNT ){
			this->invalidWarning( rewardNode, "Reward amount must be 1..%d.\n", MAX_AMOUNT );
			return false;
		}

		if( this->nodeExists( rewardNode, "Weight" ) && !this->asUInt32( rewardNode, "Weight", reward->weight ) ){
			return false;
		}

		if( reward->weight == 0 ){
			this->invalidWarning( rewardNode, "Reward weight must be above 0.\n" );
			return false;
		}

		rewards.push_back( reward );
	}

	return true;
}

bool NeedEnchantRulesDatabase::parseOutcomes( const ryml::NodeRef& node, const std::string& name, std::vector<std::shared_ptr<s_need_enchant_outcome>>& outcomes, e_need_enchant_operation operation ){
	if( !this->nodeExists( node, name ) ){
		return true;
	}

	for( const ryml::NodeRef& outcomeNode : node[c4::to_csubstr( name )] ){
		std::string result_name;

		if( !this->asString( outcomeNode, "Result", result_name ) ){
			return false;
		}

		std::shared_ptr<s_need_enchant_outcome> outcome = std::make_shared<s_need_enchant_outcome>();
		bool known = false;

		for( const auto& entry : need_enchant_result_names ){
			if( result_name == entry.name ){
				outcome->result = entry.result;
				known = true;
				break;
			}
		}

		if( !known ){
			this->invalidWarning( outcomeNode["Result"], "Unknown result \"%s\".\n", result_name.c_str() );
			return false;
		}

		if( !need_enchant_result_allowed( outcome->result, operation ) ){
			this->invalidWarning( outcomeNode["Result"], "Result \"%s\" is not allowed here.\n", result_name.c_str() );
			return false;
		}

		if( !this->asUInt32( outcomeNode, "Weight", outcome->weight ) ){
			return false;
		}

		if( outcome->weight == 0 ){
			this->invalidWarning( outcomeNode["Weight"], "Weight must be above 0.\n" );
			return false;
		}

		outcome->refine_down = 0;
		outcome->downgrade_to = 0;
		outcome->message = need_enchant_default_message( outcome->result );

		if( this->nodeExists( outcomeNode, "Message" ) && !this->asInt32( outcomeNode, "Message", outcome->message ) ){
			return false;
		}

		switch( outcome->result ){
			case NEED_ENCHANT_CLEAR_SLOTS:
				if( !this->nodesExist( outcomeNode, { "Slots" } ) ){
					return false;
				}

				for( const ryml::NodeRef& slotNode : outcomeNode["Slots"] ){
					uint16 slot;

					if( !this->asUInt16( slotNode, "Slot", slot ) ){
						return false;
					}

					if( slot >= MAX_SLOTS ){
						this->invalidWarning( slotNode, "Slot %hu exceeds MAX_SLOTS.\n", slot );
						return false;
					}

					outcome->clear_slots.push_back( slot );
				}
				break;

			case NEED_ENCHANT_REFINE_DOWN:
				if( this->nodeExists( outcomeNode, "Random" ) ){
					bool random;

					if( !this->asBool( outcomeNode, "Random", random ) ){
						return false;
					}

					if( random ){
						outcome->refine_down = NEED_ENCHANT_REFINE_DOWN_RANDOM;
					}
				}

				if( outcome->refine_down != NEED_ENCHANT_REFINE_DOWN_RANDOM ){
					uint16 amount;

					if( !this->asUInt16( outcomeNode, "Amount", amount ) ){
						return false;
					}

					if( amount == 0 || amount > MAX_REFINE ){
						this->invalidWarning( outcomeNode["Amount"], "Refine down amount must be 1..%d.\n", MAX_REFINE );
						return false;
					}

					outcome->refine_down = amount;
				}
				break;

			case NEED_ENCHANT_DOWNGRADE: {
				std::string item_name;

				if( !this->asString( outcomeNode, "To", item_name ) ){
					return false;
				}

				std::shared_ptr<item_data> item = item_db.search_aegisname( item_name.c_str() );

				if( item == nullptr ){
					this->invalidWarning( outcomeNode["To"], "Unknown item \"%s\".\n", item_name.c_str() );
					return false;
				}

				outcome->downgrade_to = item->nameid;
				} break;

			default:
				break;
		}

		if( need_enchant_has_reward( outcome->result ) ){
			if( !this->nodesExist( outcomeNode, { "Rewards" } ) || !this->parseRewards( outcomeNode, outcome->rewards ) ){
				return false;
			}

			if( outcome->rewards.empty() ){
				this->invalidWarning( outcomeNode, "Rewards must not be empty.\n" );
				return false;
			}
		}

		outcomes.push_back( outcome );
	}

	return true;
}

uint64 NeedEnchantRulesDatabase::parseBodyNode( const ryml::NodeRef& node ){
	uint64 id;

	if( !this->asUInt64( node, "Id", id ) ){
		return 0;
	}

	// Rules of a group are always defined as a whole; an import replaces them
	std::shared_ptr<s_need_enchant> rules = std::make_shared<s_need_enchant>();

	rules->id = id;
	rules->maximum_refine = 0;
	rules->reset.require_all_filled = false;

	if( this->nodeExists( node, "MaximumRefine" ) && !this->asUInt16( node, "MaximumRefine", rules->maximum_refine ) ){
		return 0;
	}

	if( this->nodeExists( node, "Slots" ) ){
		for( const ryml::NodeRef& slotNode : node["Slots"] ){
			std::shared_ptr<s_need_enchant_slot> slot = std::make_shared<s_need_enchant_slot>();

			if( !this->asUInt16( slotNode, "Slot", slot->slot ) ){
				return 0;
			}

			if( slot->slot >= MAX_SLOTS ){
				this->invalidWarning( slotNode["Slot"], "Slot %hu exceeds MAX_SLOTS.\n", slot->slot );
				return 0;
			}

			slot->minimum_refine = 0;

			if( this->nodeExists( slotNode, "MinimumRefine" ) && !this->asUInt16( slotNode, "MinimumRefine", slot->minimum_refine ) ){
				return 0;
			}

			if( this->nodeExists( slotNode, "Require" ) ){
				for( const ryml::NodeRef& requireNode : slotNode["Require"] ){
					s_need_enchant_requirement requirement;

					if( !this->asUInt16( requireNode, "Slot", requirement.slot ) ){
						return 0;
					}

					if( requirement.slot >= MAX_SLOTS ){
						this->invalidWarning( requireNode["Slot"], "Slot %hu exceeds MAX_SLOTS.\n", requirement.slot );
						return 0;
					}

					if( !this->nodesExist( requireNode, { "Enchants" } ) || !this->parseItemList( requireNode, "Enchants", requirement.enchants ) ){
						return 0;
					}

					slot->requirements.push_back( requirement );
				}
			}

			if( this->nodeExists( slotNode, "MaxSame" ) ){
				for( const ryml::NodeRef& capNode : slotNode["MaxSame"] ){
					std::string item_name;
					uint16 max;

					if( !this->asString( capNode, "Enchant", item_name ) || !this->asUInt16( capNode, "Max", max ) ){
						return 0;
					}

					std::shared_ptr<item_data> item = item_db.search_aegisname( item_name.c_str() );

					if( item == nullptr ){
						this->invalidWarning( capNode["Enchant"], "Unknown item \"%s\".\n", item_name.c_str() );
						return 0;
					}

					if( max == 0 ){
						this->invalidWarning( capNode["Max"], "Max must be above 0.\n" );
						return 0;
					}

					slot->max_same[item->nameid] = max;
				}
			}

			if( !this->parseOutcomes( slotNode, "Failures", slot->failures, NEED_ENCHANT_OP_NORMAL ) ){
				return 0;
			}

			if( this->nodeExists( slotNode, "Upgrades" ) ){
				for( const ryml::NodeRef& upgradeNode : slotNode["Upgrades"] ){
					std::string item_name;
					std::shared_ptr<s_need_enchant_upgrade> upgrade = std::make_shared<s_need_enchant_upgrade>();

					if( !this->asString( upgradeNode, "Enchant", item_name ) || !this->asUInt32( upgradeNode, "SuccessWeight", upgrade->success_weight ) ){
						return 0;
					}

					std::shared_ptr<item_data> item = item_db.search_aegisname( item_name.c_str() );

					if( item == nullptr ){
						this->invalidWarning( upgradeNode["Enchant"], "Unknown item \"%s\".\n", item_name.c_str() );
						return 0;
					}

					upgrade->enchant = item->nameid;

					if( !this->parseOutcomes( upgradeNode, "Failures", upgrade->failures, NEED_ENCHANT_OP_UPGRADE ) ){
						return 0;
					}

					slot->upgrades[upgrade->enchant] = upgrade;
				}
			}

			rules->slots[slot->slot] = slot;
		}
	}

	if( this->nodeExists( node, "Reset" ) ){
		const ryml::NodeRef& resetNode = node["Reset"];

		if( this->nodeExists( resetNode, "RequireAllFilled" ) && !this->asBool( resetNode, "RequireAllFilled", rules->reset.require_all_filled ) ){
			return 0;
		}

		if( this->nodeExists( resetNode, "Scope" ) ){
			for( const ryml::NodeRef& slotNode : resetNode["Scope"] ){
				uint16 slot;

				if( !this->asUInt16( slotNode, "Slot", slot ) ){
					return 0;
				}

				if( slot >= MAX_SLOTS ){
					this->invalidWarning( slotNode, "Slot %hu exceeds MAX_SLOTS.\n", slot );
					return 0;
				}

				rules->reset.scope.push_back( slot );
			}
		}

		if( !this->parseOutcomes( resetNode, "Outcomes", rules->reset.outcomes, NEED_ENCHANT_OP_RESET ) ){
			return 0;
		}
	}

	this->put( rules->id, rules );

	return 1;
}

/**
 * Cross check one group's rules against item_enchant_db.
 * @return false if the rules are unusable (they are removed by the caller)
 */
bool NeedEnchantRulesDatabase::validate( uint64 id, bool report ){
	std::shared_ptr<s_need_enchant> rules = this->find( id );
	std::shared_ptr<s_item_enchant> group = item_enchant_db.find( id );

	if( rules == nullptr ){
		return false;
	}

	if( group == nullptr ){
		if( report ){
			ShowError( "NEED enchant rules: item enchant group %" PRIu64 " does not exist, its rules are ignored.\n", id );
		}
		return false;
	}

	if( rules->maximum_refine != 0 && rules->maximum_refine < group->minimumRefine ){
		if( report ){
			ShowError( "NEED enchant rules %" PRIu64 ": MaximumRefine %hu is below the group's MinimumRefine %hu, its rules are ignored.\n", id, rules->maximum_refine, group->minimumRefine );
		}
		return false;
	}

	for( const auto& entry : rules->slots ){
		const s_need_enchant_slot& slot = *entry.second;
		std::shared_ptr<s_item_enchant_slot> base = util::umap_find( group->slots, slot.slot );

		if( base == nullptr ){
			if( report ){
				ShowError( "NEED enchant rules %" PRIu64 ": slot %hu is not defined in the item enchant group, its rules are ignored.\n", id, slot.slot );
			}
			return false;
		}

		if( !slot.failures.empty() ){
			// The failure weights replace the stock success chance: a second chance roll would change every rate
			if( base->normal.chance != ITEM_ENCHANT_CHANCE_BASE ){
				if( report ){
					ShowError( "NEED enchant rules %" PRIu64 ": slot %hu has Failures but its item enchant Chance is %u (must be %u), its rules are ignored.\n", id, slot.slot, base->normal.chance, ITEM_ENCHANT_CHANCE_BASE );
				}
				return false;
			}

			for( const auto& bonus : base->normal.enchantgradeChanceIncrease ){
				if( bonus.second != 0 ){
					if( report ){
						ShowError( "NEED enchant rules %" PRIu64 ": slot %hu has Failures and an EnchantgradeBonus, its rules are ignored.\n", id, slot.slot );
					}
					return false;
				}
			}
		}

		for( const auto& upgrade : slot.upgrades ){
			if( util::umap_find( base->upgrade.enchants, upgrade.first ) == nullptr ){
				if( report ){
					ShowError( "NEED enchant rules %" PRIu64 ": slot %hu upgrade %u is not an Upgrades entry of the item enchant group, its rules are ignored.\n", id, slot.slot, upgrade.first );
				}
				return false;
			}
		}

		for( const auto& requirement : slot.requirements ){
			if( requirement.slot == slot.slot ){
				if( report ){
					ShowError( "NEED enchant rules %" PRIu64 ": slot %hu requires itself, its rules are ignored.\n", id, slot.slot );
				}
				return false;
			}
		}
	}

	if( !rules->reset.outcomes.empty() || rules->reset.require_all_filled || !rules->reset.scope.empty() ){
		if( group->reset.chance == 0 ){
			if( report ){
				ShowError( "NEED enchant rules %" PRIu64 ": Reset rules on a group without a reset (Reset.Chance 0), its rules are ignored.\n", id );
			}
			return false;
		}
	}

	if( !rules->reset.outcomes.empty() && group->reset.chance != ITEM_ENCHANT_CHANCE_BASE ){
		if( report ){
			ShowError( "NEED enchant rules %" PRIu64 ": Reset Outcomes need the item enchant Reset.Chance at %u (it is %u), its rules are ignored.\n", id, ITEM_ENCHANT_CHANCE_BASE, group->reset.chance );
		}
		return false;
	}

	return true;
}

void NeedEnchantRulesDatabase::loadingFinished(){
	std::vector<uint64> invalid;

	for( const auto& entry : *this ){
		if( !this->validate( entry.first, true ) ){
			invalid.push_back( entry.first );
		}
	}

	for( uint64 id : invalid ){
		this->erase( id );
	}

	YamlDatabase::loadingFinished();
}

std::shared_ptr<s_need_enchant> need_enchant_find( uint64 group ){
	return need_enchant_rules_db.find( group );
}

std::shared_ptr<s_need_enchant_upgrade> need_enchant_find_upgrade( uint64 group, uint16 slot, t_itemid enchant ){
	std::shared_ptr<s_need_enchant> rules = need_enchant_rules_db.find( group );

	if( rules == nullptr ){
		return nullptr;
	}

	std::shared_ptr<s_need_enchant_slot> slot_rules = util::umap_find( rules->slots, slot );

	if( slot_rules == nullptr ){
		return nullptr;
	}

	return util::umap_find( slot_rules->upgrades, enchant );
}

/*==========================================
 * Pure rules
 *------------------------------------------*/

/// Count of an enchant on the item's enchant slots (real card slots excluded)
uint16 need_enchant_count( const struct item& it, const item_data& data, t_itemid enchant ){
	uint16 count = 0;

	for( int32 i = data.slots; i < MAX_SLOTS; i++ ){
		if( it.card[i] == enchant ){
			count++;
		}
	}

	return count;
}

namespace{
	/// Option weight after the per-enchant caps (a capped option cannot be rolled)
	uint32 need_enchant_option_weight( const struct item& it, const item_data& data, const s_need_enchant_slot* slot_rules, const s_item_enchant_normal_sub& option ){
		if( slot_rules != nullptr ){
			const auto cap = slot_rules->max_same.find( option.item_id );

			if( cap != slot_rules->max_same.end() && need_enchant_count( it, data, option.item_id ) >= cap->second ){
				return 0;
			}
		}

		return option.chance;
	}
}

/**
 * NEED: Conditions of the V2 rules, checked before anything is paid.
 * @param pool normal enchant pool (OP_NORMAL), else nullptr
 * @param perfect_item requested perfect enchant (OP_PERFECT), else 0
 */
bool need_enchant_check( const struct item& it, const item_data& data, const s_item_enchant& group, uint16 slot, e_need_enchant_operation operation, const s_item_enchant_normal* pool, t_itemid perfect_item ){
	std::shared_ptr<s_need_enchant> rules = need_enchant_rules_db.find( group.id );

	if( rules == nullptr ){
		return true;
	}

	if( rules->maximum_refine != 0 && it.refine > rules->maximum_refine ){
		return false;
	}

	if( operation == NEED_ENCHANT_OP_RESET ){
		if( rules->reset.require_all_filled ){
			for( uint16 order_slot : group.order ){
				if( order_slot >= data.slots && it.card[order_slot] == 0 ){
					return false;
				}
			}
		}

		return true;
	}

	std::shared_ptr<s_need_enchant_slot> slot_rules = util::umap_find( rules->slots, slot );

	if( slot_rules == nullptr ){
		return true;
	}

	if( operation == NEED_ENCHANT_OP_NORMAL || operation == NEED_ENCHANT_OP_PERFECT ){
		if( it.refine < slot_rules->minimum_refine ){
			return false;
		}

		for( const auto& requirement : slot_rules->requirements ){
			if( !util::vector_exists( requirement.enchants, it.card[requirement.slot] ) ){
				return false;
			}
		}
	}

	if( operation == NEED_ENCHANT_OP_PERFECT ){
		const auto cap = slot_rules->max_same.find( perfect_item );

		if( cap != slot_rules->max_same.end() && need_enchant_count( it, data, perfect_item ) >= cap->second ){
			return false;
		}
	}

	if( operation == NEED_ENCHANT_OP_NORMAL && pool != nullptr ){
		uint64 eligible = 0;

		for( const auto& option : pool->enchants ){
			eligible += need_enchant_option_weight( it, data, slot_rules.get(), *option.second );
		}

		if( eligible == 0 ){
			return false;
		}
	}

	return true;
}

/**
 * NEED: Roll a normal enchant.
 * Without Failures: stock success chance, then a weighted option pick.
 * With Failures: one integer roll over (option weights + failure weights).
 */
s_need_enchant_roll need_enchant_roll_normal( const struct item& it, const item_data& data, const s_item_enchant& group, const s_item_enchant_slot& slot, const s_item_enchant_normal& pool ){
	s_need_enchant_roll roll = {};
	std::shared_ptr<s_need_enchant> rules = need_enchant_rules_db.find( group.id );
	std::shared_ptr<s_need_enchant_slot> slot_rules = rules != nullptr ? util::umap_find( rules->slots, slot.slot ) : nullptr;
	const s_need_enchant_slot* caps = slot_rules.get();
	bool has_failures = caps != nullptr && !caps->failures.empty();

	if( !has_failures && !itemdb_enchant_roll( itemdb_enchant_success_chance( slot, it.enchantgrade ) ) ){
		return roll;
	}

	uint64 total = 0;

	for( const auto& option : pool.enchants ){
		total += need_enchant_option_weight( it, data, caps, *option.second );
	}

	if( has_failures ){
		for( const auto& failure : caps->failures ){
			total += failure->weight;
		}
	}

	if( total == 0 ){
		return roll;
	}

	uint64 value = rnd_value<uint64>( 0, total - 1 );

	for( const auto& option : pool.enchants ){
		uint32 weight = need_enchant_option_weight( it, data, caps, *option.second );

		if( value < weight ){
			roll.enchant = option.second->item_id;
			return roll;
		}

		value -= weight;
	}

	if( has_failures ){
		for( const auto& failure : caps->failures ){
			if( value < failure->weight ){
				roll.failure = failure;
				return roll;
			}

			value -= failure->weight;
		}
	}

	return roll;
}

/**
 * NEED: Roll an outcome list (reset outcomes, upgrade failures).
 * @param success_weight weight of an implicit success (upgrades), 0 for reset lists that hold SUCCESS entries
 * @param success set to true when a success was rolled
 * @return the rolled outcome (a SUCCESS outcome of a reset list, or a failure), nullptr for the implicit success
 */
std::shared_ptr<s_need_enchant_outcome> need_enchant_roll_outcomes( const std::vector<std::shared_ptr<s_need_enchant_outcome>>& outcomes, uint32 success_weight, bool& success ){
	uint64 total = success_weight;

	for( const auto& outcome : outcomes ){
		total += outcome->weight;
	}

	success = false;

	if( total == 0 ){
		return nullptr;
	}

	uint64 value = rnd_value<uint64>( 0, total - 1 );

	if( value < success_weight ){
		success = true;
		return nullptr;
	}

	value -= success_weight;

	for( const auto& outcome : outcomes ){
		if( value < outcome->weight ){
			success = outcome->result == NEED_ENCHANT_SUCCESS;
			return outcome;
		}

		value -= outcome->weight;
	}

	return nullptr;
}

std::shared_ptr<s_need_enchant_reward> need_enchant_pick_reward( const s_need_enchant_outcome& outcome ){
	uint64 total = 0;

	for( const auto& reward : outcome.rewards ){
		total += reward->weight;
	}

	if( total == 0 ){
		return nullptr;
	}

	uint64 value = rnd_value<uint64>( 0, total - 1 );

	for( const auto& reward : outcome.rewards ){
		if( value < reward->weight ){
			return reward;
		}

		value -= reward->weight;
	}

	return nullptr;
}

/**
 * NEED: Apply an outcome's item change (no packets). Real card slots [0, data.slots) are never touched.
 * @param slot slot of the request (DOWNGRADE replaces this slot)
 * @return true if the item changed
 */
bool need_enchant_mutate( struct item& it, const item_data& data, const s_need_enchant_outcome& outcome, uint16 slot ){
	bool changed = false;

	switch( outcome.result ){
		case NEED_ENCHANT_CLEAR_SLOTS:
			for( uint16 clear : outcome.clear_slots ){
				if( clear >= data.slots && it.card[clear] != 0 ){
					it.card[clear] = 0;
					changed = true;
				}
			}
			break;

		case NEED_ENCHANT_CLEAR_ENCHANTS:
			for( int32 i = data.slots; i < MAX_SLOTS; i++ ){
				if( it.card[i] != 0 ){
					it.card[i] = 0;
					changed = true;
				}
			}
			break;

		case NEED_ENCHANT_REFINE_DOWN: {
			int32 amount = outcome.refine_down;

			if( amount == NEED_ENCHANT_REFINE_DOWN_RANDOM ){
				amount = rnd_value<int32>( 0, it.refine );
			}

			int32 refine = std::max( 0, static_cast<int32>( it.refine ) - amount );

			if( refine != it.refine ){
				it.refine = static_cast<char>( refine );
				changed = true;
			}
			} break;

		case NEED_ENCHANT_DOWNGRADE:
			if( slot >= data.slots && slot < MAX_SLOTS && it.card[slot] != outcome.downgrade_to ){
				it.card[slot] = outcome.downgrade_to;
				changed = true;
			}
			break;

		default:
			break;
	}

	return changed;
}

/*==========================================
 * Session side
 *------------------------------------------*/

/**
 * NEED: Every reward this request could give must fit (amount and weight) before anything is paid.
 */
bool need_enchant_can_receive_rewards( map_session_data& sd, const s_item_enchant& group, uint16 slot, e_need_enchant_operation operation ){
	std::shared_ptr<s_need_enchant> rules = need_enchant_rules_db.find( group.id );

	if( rules == nullptr ){
		return true;
	}

	for( const auto* list : need_enchant_outcome_lists( *rules, slot, operation ) ){
		for( const auto& outcome : *list ){
			if( !need_enchant_has_reward( outcome->result ) ){
				continue;
			}

			for( const auto& reward : outcome->rewards ){
				std::shared_ptr<item_data> data = item_db.find( reward->item_id );

				if( data == nullptr ){
					return false;
				}

				switch( pc_checkadditem( &sd, reward->item_id, reward->amount ) ){
					case CHKADDITEM_OVERAMOUNT:
						return false;
					case CHKADDITEM_NEW:
						if( pc_inventoryblank( &sd ) == 0 ){
							return false;
						}
						break;
				}

				if( static_cast<uint64>( sd.weight ) + static_cast<uint64>( data->weight ) * reward->amount > sd.max_weight ){
					return false;
				}
			}
		}
	}

	return true;
}

namespace{
	void need_enchant_give_reward( map_session_data& sd, const s_need_enchant_outcome& outcome ){
		std::shared_ptr<s_need_enchant_reward> reward = need_enchant_pick_reward( outcome );

		if( reward == nullptr ){
			return;
		}

		struct item it = {};

		it.nameid = reward->item_id;
		it.identify = itemdb_isidentified( reward->item_id );

		e_additem_result flag = pc_additem( &sd, &it, reward->amount, LOG_TYPE_ENCHANT );

		if( flag != ADDITEM_SUCCESS ){
			// Checked before payment; only reachable if the inventory changed in between
			ShowWarning( "NEED enchant: reward %u x%hu could not be added to '%s' (flag %d), dropped on the floor.\n", reward->item_id, reward->amount, sd.status.name, flag );
			clif_additem( &sd, 0, 0, flag );

			if( pc_candrop( &sd, &it ) ){
				map_addflooritem( &it, reward->amount, sd.m, sd.x, sd.y, 0, 0, 0, 0, 0 );
			}
		}
	}
}

/**
 * NEED: Execute a failure outcome after the cost was paid.
 * The result window message goes first, item packets follow (the client closes the window on a result).
 * @param outcome nullptr = plain stock failure
 */
void need_enchant_apply_failure( map_session_data& sd, uint16 index, const s_need_enchant_outcome* outcome, uint16 slot ){
	if( outcome == nullptr ){
		clif_enchantwindow_result_message( sd, MSI_ENCHANT_FAILED );
		return;
	}

	struct item& it = sd.inventory.u.items_inventory[index];
	item_data* data = sd.inventory_data[index];

	switch( outcome->result ){
		case NEED_ENCHANT_CLEAR_SLOTS:
		case NEED_ENCHANT_CLEAR_ENCHANTS:
		case NEED_ENCHANT_REFINE_DOWN:
		case NEED_ENCHANT_DOWNGRADE: {
			struct item after = it;

			if( need_enchant_mutate( after, *data, *outcome, slot ) ){
				// Same visibility pattern as the item reform UI: hide, change, show again
				log_pick_pc( &sd, LOG_TYPE_ENCHANT, -1, &it );
				clif_delitem( sd, index, 1, 0 );
				it = after;
				log_pick_pc( &sd, LOG_TYPE_ENCHANT, 1, &it );
				clif_additem( &sd, index, 1, 0 );
			}

			clif_enchantwindow_result_message( sd, outcome->message );
			} break;

		case NEED_ENCHANT_DESTROY:
			clif_enchantwindow_result_message( sd, outcome->message );
			pc_delitem( &sd, index, 1, 0, 0, LOG_TYPE_ENCHANT );
			break;

		case NEED_ENCHANT_REWARD:
			clif_enchantwindow_result_message( sd, outcome->message );
			need_enchant_give_reward( sd, *outcome );
			break;

		case NEED_ENCHANT_DESTROY_WITH_REWARD:
			clif_enchantwindow_result_message( sd, outcome->message );
			pc_delitem( &sd, index, 1, 0, 0, LOG_TYPE_ENCHANT );
			need_enchant_give_reward( sd, *outcome );
			break;

		case NEED_ENCHANT_FAIL_KEEP:
		default:
			clif_enchantwindow_result_message( sd, outcome->message );
			break;
	}
}

/*==========================================
 * Test harness (only built with NEED_ENCHANT_TEST)
 *------------------------------------------*/
#ifdef NEED_ENCHANT_TEST
namespace{
	const uint64 NEED_ENCHANT_RULES_TEST_SAMPLES = 1000000;
	const double NEED_ENCHANT_RULES_TEST_Z = 3.29;

	double need_enchant_rules_chisq_z( double chisq, double df ){
		double h = 2.0 / ( 9.0 * df );

		return ( std::cbrt( chisq / df ) - ( 1.0 - h ) ) / std::sqrt( h );
	}

	/// counts vs expected weights, all keyed by label
	bool need_enchant_rules_check_distribution( const char* label, const std::map<std::string, uint64>& weights, const std::map<std::string, uint64>& counts, uint64 samples ){
		uint64 total = 0;

		for( const auto& w : weights ){
			total += w.second;
		}

		double chisq = 0;
		int32 bins = 0;

		for( const auto& w : weights ){
			double expected = static_cast<double>( samples ) * w.second / total;
			const auto c = counts.find( w.first );
			double observed = c == counts.end() ? 0 : static_cast<double>( c->second );

			if( expected >= 5 ){
				chisq += ( observed - expected ) * ( observed - expected ) / expected;
				bins++;
			}

			ShowInfo( "  %-28s weight %10" PRIu64 "  expected %8.4f%%  observed %8.4f%%\n", w.first.c_str(), w.second, 100.0 * w.second / total, 100.0 * observed / samples );
		}

		double z = bins > 1 ? need_enchant_rules_chisq_z( chisq, bins - 1 ) : 0;
		bool pass = z < NEED_ENCHANT_RULES_TEST_Z;

		ShowInfo( "[NEED enchant rules test] %s: z=%.2f %s\n", label, z, pass ? "PASS" : "FAIL" );

		return pass;
	}

	bool need_enchant_rules_parse( const char* yaml ){
		ryml::Tree tree = ryml::parse_in_arena( c4::to_csubstr( yaml ) );
		bool ok = true;

		for( const ryml::NodeRef& node : tree["Body"] ){
			ok &= need_enchant_rules_db.parseBodyNode( node ) != 0;
		}

		return ok;
	}

	bool need_enchant_rules_expect( const char* label, bool value, bool expected ){
		bool pass = value == expected;

		ShowInfo( "[NEED enchant rules test] %s: %s (expected %s) %s\n", label, value ? "true" : "false", expected ? "true" : "false", pass ? "PASS" : "FAIL" );

		return pass;
	}

	std::string need_enchant_item_label( t_itemid id ){
		std::shared_ptr<item_data> data = item_db.find( id );

		return std::to_string( id ) + " " + ( data != nullptr ? data->name : "?" );
	}
}

/**
 * NEED: Rules self test. Uses official groups 4 (Gray Wolf pendant/earring, Slots 1, Order 3-2-1, reset 100%)
 * and 57 (Barmund soul ring, reset 70%) as carriers; rules are parsed from strings, never from the db folder.
 */
bool need_enchant_selftest(){
	bool pass = true;
	std::shared_ptr<s_item_enchant> group4 = item_enchant_db.find( 4 );
	std::shared_ptr<item_data> pendant = item_db.search_aegisname( "Gray_W_Pendant" );

	ShowStatus( "[NEED enchant rules test] start\n" );

	if( group4 == nullptr || pendant == nullptr ){
		ShowError( "[NEED enchant rules test] group 4 / Gray_W_Pendant missing.\n" );
		return false;
	}

	need_enchant_rules_db.clear();

	// ---- 1. parser rejects invalid rules
	{
		pass &= need_enchant_rules_expect( "parse: unknown result rejected", need_enchant_rules_parse(
			"Body:\n  - Id: 4\n    Slots:\n      - Slot: 3\n        Failures:\n          - Result: EXPLODE\n            Weight: 1\n" ), false );
		pass &= need_enchant_rules_expect( "parse: SUCCESS in slot failures rejected", need_enchant_rules_parse(
			"Body:\n  - Id: 4\n    Slots:\n      - Slot: 3\n        Failures:\n          - Result: SUCCESS\n            Weight: 1\n" ), false );
		pass &= need_enchant_rules_expect( "parse: weight 0 rejected", need_enchant_rules_parse(
			"Body:\n  - Id: 4\n    Slots:\n      - Slot: 3\n        Failures:\n          - Result: DESTROY\n            Weight: 0\n" ), false );
		pass &= need_enchant_rules_expect( "parse: reward without Rewards rejected", need_enchant_rules_parse(
			"Body:\n  - Id: 4\n    Slots:\n      - Slot: 3\n        Failures:\n          - Result: REWARD\n            Weight: 1\n" ), false );
		need_enchant_rules_db.clear();

		need_enchant_rules_parse( "Body:\n  - Id: 999999902\n    MaximumRefine: 8\n" );
		pass &= need_enchant_rules_expect( "validate: unknown group rejected", need_enchant_rules_db.validate( 999999902, false ), false );
		need_enchant_rules_parse( "Body:\n  - Id: 4\n    Slots:\n      - Slot: 0\n        MinimumRefine: 1\n" );
		pass &= need_enchant_rules_expect( "validate: undefined slot rejected", need_enchant_rules_db.validate( 4, false ), false );

		// Failures on a slot whose stock Chance is below 100% must be refused
		std::shared_ptr<s_item_enchant_slot> slot3 = util::umap_find( group4->slots, static_cast<uint16>( 3 ) );
		uint32 saved = slot3->normal.chance;

		slot3->normal.chance = ITEM_ENCHANT_CHANCE_BASE / 2;
		need_enchant_rules_parse( "Body:\n  - Id: 4\n    Slots:\n      - Slot: 3\n        Failures:\n          - Result: FAIL_KEEP\n            Weight: 10\n" );
		pass &= need_enchant_rules_expect( "validate: failures on a Chance<100% slot rejected", need_enchant_rules_db.validate( 4, false ), false );
		slot3->normal.chance = saved;
		pass &= need_enchant_rules_expect( "validate: same rules on a 100% slot accepted", need_enchant_rules_db.validate( 4, false ), true );

		need_enchant_rules_parse( "Body:\n  - Id: 57\n    Slots:\n      - Slot: 3\n    Reset:\n      Outcomes:\n        - Result: SUCCESS\n          Weight: 1\n" );
		pass &= need_enchant_rules_expect( "validate: reset outcomes on a 70% stock reset rejected", need_enchant_rules_db.validate( 57, false ), false );
		need_enchant_rules_db.clear();
	}

	// ---- 2. normal roll: options + failures share one denominator (Charleston-like)
	{
		need_enchant_rules_parse(
			"Body:\n"
			"  - Id: 4\n"
			"    Slots:\n"
			"      - Slot: 3\n"
			"        Failures:\n"
			"          - Result: FAIL_KEEP\n"
			"            Weight: 13889\n"
			"          - Result: DESTROY\n"
			"            Weight: 2778\n" );

		std::shared_ptr<s_item_enchant_slot> slot3 = util::umap_find( group4->slots, static_cast<uint16>( 3 ) );
		std::shared_ptr<s_item_enchant_normal> pool = util::umap_find( slot3->normal.enchants, static_cast<uint16>( 0 ) );
		struct item it = {};

		it.nameid = pendant->nameid;
		it.identify = 1;

		std::map<std::string, uint64> weights, counts;

		for( const auto& option : pool->enchants ){
			weights[need_enchant_item_label( option.first )] = option.second->chance;
		}
		weights["FAIL_KEEP"] = 13889;
		weights["DESTROY"] = 2778;

		for( uint64 i = 0; i < NEED_ENCHANT_RULES_TEST_SAMPLES; i++ ){
			s_need_enchant_roll roll = need_enchant_roll_normal( it, *pendant, *group4, *slot3, *pool );

			if( roll.enchant != 0 ){
				counts[need_enchant_item_label( roll.enchant )]++;
			}else if( roll.failure != nullptr ){
				counts[need_enchant_result_name( roll.failure->result )]++;
			}else{
				counts["(none)"]++;
			}
		}

		ShowInfo( "[NEED enchant rules test] normal roll with failures (group 4 slot 3), %" PRIu64 " samples\n", NEED_ENCHANT_RULES_TEST_SAMPLES );
		pass &= need_enchant_rules_check_distribution( "normal roll options+failures", weights, counts, NEED_ENCHANT_RULES_TEST_SAMPLES );
		pass &= need_enchant_rules_expect( "normal roll never returns nothing", counts.count( "(none)" ) == 0, true );
		need_enchant_rules_db.clear();
	}

	// ---- 3. stock behaviour without rules is the Phase 1.1 engine (100% slot -> always an option)
	{
		std::shared_ptr<s_item_enchant_slot> slot3 = util::umap_find( group4->slots, static_cast<uint16>( 3 ) );
		std::shared_ptr<s_item_enchant_normal> pool = util::umap_find( slot3->normal.enchants, static_cast<uint16>( 0 ) );
		struct item it = {};
		uint64 none = 0;

		it.nameid = pendant->nameid;

		for( uint64 i = 0; i < 100000; i++ ){
			if( need_enchant_roll_normal( it, *pendant, *group4, *slot3, *pool ).enchant == 0 ){
				none++;
			}
		}

		pass &= need_enchant_rules_expect( "no rules: 100% slot always enchants", none == 0, true );
	}

	// ---- 4. conditions
	{
		need_enchant_rules_parse(
			"Body:\n"
			"  - Id: 4\n"
			"    MaximumRefine: 8\n"
			"    Slots:\n"
			"      - Slot: 2\n"
			"        MinimumRefine: 5\n"
			"        Require:\n"
			"          - Slot: 3\n"
			"            Enchants:\n"
			"              - Wolf_Orb_Str_1\n"
			"        MaxSame:\n"
			"          - Enchant: Wolf_Orb_Str_1\n"
			"            Max: 1\n"
			"    Reset:\n"
			"      RequireAllFilled: true\n" );

		std::shared_ptr<item_data> str1 = item_db.search_aegisname( "Wolf_Orb_Str_1" );
		struct item it = {};

		it.nameid = pendant->nameid;
		it.refine = 5;
		it.card[3] = str1->nameid;

		pass &= need_enchant_rules_expect( "cond: refine 5, slot3=Str1 -> slot 2 allowed", need_enchant_check( it, *pendant, *group4, 2, NEED_ENCHANT_OP_NORMAL, nullptr, 0 ), true );
		it.refine = 9;
		pass &= need_enchant_rules_expect( "cond: refine 9 > MaximumRefine 8 refused", need_enchant_check( it, *pendant, *group4, 2, NEED_ENCHANT_OP_NORMAL, nullptr, 0 ), false );
		it.refine = 4;
		pass &= need_enchant_rules_expect( "cond: refine 4 < slot MinimumRefine 5 refused", need_enchant_check( it, *pendant, *group4, 2, NEED_ENCHANT_OP_NORMAL, nullptr, 0 ), false );
		it.refine = 5;
		it.card[3] = 0;
		pass &= need_enchant_rules_expect( "cond: required enchant missing refused", need_enchant_check( it, *pendant, *group4, 2, NEED_ENCHANT_OP_NORMAL, nullptr, 0 ), false );
		it.card[3] = str1->nameid;
		pass &= need_enchant_rules_expect( "cond: perfect Str1 at cap (1 on item) refused", need_enchant_check( it, *pendant, *group4, 2, NEED_ENCHANT_OP_PERFECT, nullptr, str1->nameid ), false );
		pass &= need_enchant_rules_expect( "cond: reset with empty slots refused (RequireAllFilled)", need_enchant_check( it, *pendant, *group4, 0, NEED_ENCHANT_OP_RESET, nullptr, 0 ), false );
		it.card[2] = str1->nameid;
		it.card[1] = str1->nameid;
		pass &= need_enchant_rules_expect( "cond: reset with all slots filled allowed", need_enchant_check( it, *pendant, *group4, 0, NEED_ENCHANT_OP_RESET, nullptr, 0 ), true );
		need_enchant_rules_db.clear();
	}

	// ---- 5. item changes keep the real card slot
	{
		struct item it = {};
		s_need_enchant_outcome outcome = {};

		it.nameid = pendant->nameid;
		it.card[0] = 4079; // Mantis_Card in the real card slot (Slots 1)
		it.card[1] = 1;
		it.card[2] = 2;
		it.card[3] = 3;
		it.refine = 7;

		outcome.result = NEED_ENCHANT_CLEAR_SLOTS;
		outcome.clear_slots = { 0, 3 };
		struct item a = it;
		need_enchant_mutate( a, *pendant, outcome, 3 );
		pass &= need_enchant_rules_expect( "mutate: CLEAR_SLOTS {0,3} keeps card0, clears card3", a.card[0] == 4079 && a.card[3] == 0 && a.card[2] == 2, true );

		outcome.result = NEED_ENCHANT_CLEAR_ENCHANTS;
		struct item b = it;
		need_enchant_mutate( b, *pendant, outcome, 3 );
		pass &= need_enchant_rules_expect( "mutate: CLEAR_ENCHANTS keeps card0", b.card[0] == 4079 && b.card[1] == 0 && b.card[2] == 0 && b.card[3] == 0, true );

		outcome.result = NEED_ENCHANT_REFINE_DOWN;
		outcome.refine_down = 3;
		struct item c = it;
		need_enchant_mutate( c, *pendant, outcome, 3 );
		pass &= need_enchant_rules_expect( "mutate: REFINE_DOWN 3 from +7 -> +4", c.refine == 4, true );

		outcome.result = NEED_ENCHANT_DOWNGRADE;
		outcome.downgrade_to = 77;
		struct item d = it;
		need_enchant_mutate( d, *pendant, outcome, 2 );
		pass &= need_enchant_rules_expect( "mutate: DOWNGRADE slot 2 -> 77", d.card[2] == 77 && d.card[3] == 3, true );

		// REFINE_DOWN random: uniform 0..refine
		outcome.result = NEED_ENCHANT_REFINE_DOWN;
		outcome.refine_down = NEED_ENCHANT_REFINE_DOWN_RANDOM;
		std::map<std::string, uint64> weights, counts;

		for( int32 r = 0; r <= 7; r++ ){
			weights["refine +" + std::to_string( r )] = 1;
		}

		for( uint64 i = 0; i < NEED_ENCHANT_RULES_TEST_SAMPLES; i++ ){
			struct item e = it;

			need_enchant_mutate( e, *pendant, outcome, 3 );
			counts["refine +" + std::to_string( static_cast<int32>( e.refine ) )]++;
		}

		ShowInfo( "[NEED enchant rules test] REFINE_DOWN random from +7 (enchan_upg.txt rand(0,refine))\n" );
		pass &= need_enchant_rules_check_distribution( "refine down uniform 0..7", weights, counts, NEED_ENCHANT_RULES_TEST_SAMPLES );
	}

	// ---- 6. upgrade outcomes and reset outcomes with rewards (Prontera badge numbers)
	{
		std::vector<std::shared_ptr<s_need_enchant_outcome>> failures;
		std::shared_ptr<s_need_enchant_outcome> down = std::make_shared<s_need_enchant_outcome>();

		down->result = NEED_ENCHANT_DOWNGRADE;
		down->weight = 20;
		failures.push_back( down );

		std::map<std::string, uint64> weights = { { "SUCCESS", 80 }, { "DOWNGRADE", 20 } }, counts;

		for( uint64 i = 0; i < NEED_ENCHANT_RULES_TEST_SAMPLES; i++ ){
			bool success;
			std::shared_ptr<s_need_enchant_outcome> outcome = need_enchant_roll_outcomes( failures, 80, success );

			counts[success ? "SUCCESS" : need_enchant_result_name( outcome->result )]++;
		}

		ShowInfo( "[NEED enchant rules test] upgrade 80 / downgrade 20\n" );
		pass &= need_enchant_rules_check_distribution( "upgrade outcomes", weights, counts, NEED_ENCHANT_RULES_TEST_SAMPLES );

		std::shared_ptr<s_need_enchant_outcome> ok = std::make_shared<s_need_enchant_outcome>();
		std::shared_ptr<s_need_enchant_outcome> broken = std::make_shared<s_need_enchant_outcome>();
		std::vector<std::shared_ptr<s_need_enchant_outcome>> reset;
		const uint16 amounts[] = { 9, 10, 11, 12, 13, 14, 15 };
		const uint32 reward_weights[] = { 60, 20, 6, 6, 5, 2, 1 };

		ok->result = NEED_ENCHANT_SUCCESS;
		ok->weight = 79;
		broken->result = NEED_ENCHANT_DESTROY_WITH_REWARD;
		broken->weight = 21;

		for( size_t i = 0; i < ARRAYLENGTH( amounts ); i++ ){
			std::shared_ptr<s_need_enchant_reward> reward = std::make_shared<s_need_enchant_reward>();

			reward->item_id = 6920;
			reward->amount = amounts[i];
			reward->weight = reward_weights[i];
			broken->rewards.push_back( reward );
		}

		reset.push_back( ok );
		reset.push_back( broken );

		std::map<std::string, uint64> reset_weights, reset_counts, reward_w, reward_c;

		reset_weights["SUCCESS"] = 79;
		for( size_t i = 0; i < ARRAYLENGTH( amounts ); i++ ){
			reset_weights["DESTROY_WITH_REWARD x" + std::to_string( amounts[i] )] = 21 * reward_weights[i];
		}
		// expected total weight is 79*100 + 21*100 when SUCCESS is scaled by the reward total (100)
		reset_weights["SUCCESS"] = 79 * 100;

		for( uint64 i = 0; i < NEED_ENCHANT_RULES_TEST_SAMPLES; i++ ){
			bool success;
			std::shared_ptr<s_need_enchant_outcome> outcome = need_enchant_roll_outcomes( reset, 0, success );

			if( success ){
				reset_counts["SUCCESS"]++;
			}else{
				reset_counts["DESTROY_WITH_REWARD x" + std::to_string( need_enchant_pick_reward( *outcome )->amount )]++;
			}
		}

		ShowInfo( "[NEED enchant rules test] Prontera badge reset: 79%% success / 21%% destroy + RuneMagicPowder 9..15\n" );
		pass &= need_enchant_rules_check_distribution( "reset outcomes with reward table", reset_weights, reset_counts, NEED_ENCHANT_RULES_TEST_SAMPLES );
	}

	need_enchant_rules_db.clear();
	ShowStatus( "[NEED enchant rules test] result: %s\n", pass ? "PASS" : "FAIL" );

	return pass;
}

/**
 * NEED test only: parse generator output files into memory (never the db folder) and cross check them.
 * map-server-enchanttest --need-enchant-check <item_enchant.yml> <enchant_rules.yml>
 */
bool need_enchant_check_generated( const char* item_enchant_path, const char* rules_path ){
	std::ifstream in( item_enchant_path, std::ios::binary );

	if( !in ){
		ShowError( "[NEED enchant check] cannot open '%s'.\n", item_enchant_path );
		return false;
	}

	std::stringstream buffer;

	buffer << in.rdbuf();

	std::string text = buffer.str();
	ryml::Tree tree = ryml::parse_in_arena( c4::to_csubstr( text ) );
	size_t groups = 0, failed = 0;
	// Parse into a fresh database first: warnings about a foreign tree must not use the line table
	// of the file the global database loaded last
	ItemEnchantDatabase probe;

	for( const ryml::NodeRef& node : tree["Body"] ){
		groups++;

		if( probe.parseBodyNode( node ) == 0 ){
			failed++;
		}
	}

	if( failed == 0 ){
		for( const ryml::NodeRef& node : tree["Body"] ){
			item_enchant_db.parseBodyNode( node );
		}
	}

	ShowInfo( "[NEED enchant check] %s: %zu groups, %zu rejected\n", item_enchant_path, groups, failed );

	need_enchant_rules_db.clear();

	bool rules_ok = need_enchant_load_test_rules( rules_path );

	ShowInfo( "[NEED enchant check] %s: %zu groups with rules, %s\n", rules_path, need_enchant_rules_db.size(), rules_ok ? "valid" : "INVALID" );

	bool pass = failed == 0 && groups > 0 && rules_ok;

	ShowStatus( "[NEED enchant check] result: %s\n", pass ? "PASS" : "FAIL" );

	return pass;
}

/**
 * NEED test only: load a rules file on top of the current rules (@enchanttest rules <path>).
 */
bool need_enchant_load_test_rules( const char* path ){
	std::ifstream in( path, std::ios::binary );

	if( !in ){
		ShowError( "NEED enchant rules: cannot open '%s'.\n", path );
		return false;
	}

	std::stringstream buffer;

	buffer << in.rdbuf();

	std::string text = buffer.str();
	ryml::Tree tree = ryml::parse_in_arena( c4::to_csubstr( text ) );
	bool ok = true;
	// Parse into a fresh database first (see need_enchant_check_generated), then apply only a clean file
	NeedEnchantRulesDatabase probe;

	for( const ryml::NodeRef& node : tree["Body"] ){
		ok &= probe.parseBodyNode( node ) != 0;
	}

	if( !ok ){
		return false;
	}

	for( const ryml::NodeRef& node : tree["Body"] ){
		need_enchant_rules_db.parseBodyNode( node );
	}

	std::vector<uint64> invalid;

	for( const auto& entry : need_enchant_rules_db ){
		if( !need_enchant_rules_db.validate( entry.first, true ) ){
			invalid.push_back( entry.first );
		}
	}

	for( uint64 id : invalid ){
		need_enchant_rules_db.erase( id );
	}

	return ok && invalid.empty();
}
#endif
