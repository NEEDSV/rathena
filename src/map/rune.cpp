/***********************************/
/***********    Shakto      ********/
/**    https://ronovelty.com/     **/
/***********************************/

#include "rune.hpp"

#include <stdlib.h>
#include <iostream>
#include <set>
#include <tuple>

#include <common/cbasetypes.hpp>
#include <common/database.hpp>
#include <common/db.hpp>
#include <common/ers.hpp>
#include <common/nullpo.hpp>
#include <common/random.hpp>
#include <common/showmsg.hpp>
#include <common/strlib.hpp>

#include "clif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "pc.hpp"

using namespace rathena;

RuneDecompoDatabase runedecompo_db;
RuneBookDatabase runebook_db;
RuneDatabase rune_db;

// ---------------------------------------------------------------------------------------------
// NEED rune tablet buff tooltip (display only). See tools/need_rune_tooltip/generate.py.
// ---------------------------------------------------------------------------------------------

// clif.cpp has no header declaration for this; the icon goes to the owner only (SELF).
void clif_status_change_sub(const block_list* bl, int32 id, int32 type, int32 flag, t_tick tick, int32 val1, int32 val2, int32 val3, enum send_target target_type);

static constexpr size_t RUNE_DISPLAY_BONUS_CAP = 8;

struct s_rune_display_state {
	int16 efst;
	uint32 setid;
	uint8 count;
	s_rune_display_bonus bonus[RUNE_DISPLAY_BONUS_CAP];
};

struct s_rune_display_set {
	uint16 tag;
	uint32 setid;
	uint64 script_hash;
	int16 efst[16][7];	///< [grade][pieces] -> EFST, 0 = no icon
};

#include "rune_display_table.inc"

static_assert( NEED_RUNE_DISPLAY_MAX_BONUS <= RUNE_DISPLAY_BONUS_CAP, "rune tooltip: raise RUNE_DISPLAY_BONUS_CAP" );
static_assert( NEED_RUNE_DISPLAY_GRADES == 16 && NEED_RUNE_DISPLAY_PIECES == 7, "rune tooltip: grid size changed, update s_rune_display_set" );
static_assert( EFST_MAX <= NEED_RUNE_DISPLAY_EFST_FIRST, "rune tooltip: official EFSTs reached the NEED rune tooltip block, move it (tools/need_rune_tooltip/config.yml)" );

/// Sets whose loaded scripts still match the generated table (anything else shows no icon)
static std::unordered_map<uint32, const s_rune_display_set*> rune_display_enabled;

static uint64 rune_display_fnv1a( const std::string& data, uint64 h = 0xcbf29ce484222325ULL ){
	for( unsigned char c : data ){
		h ^= c;
		h *= 0x100000001b3ULL;
	}
	return h;
}

/// Hash of one set script: lines trimmed of spaces/tabs/CR, empty lines dropped (same as generate.py)
static uint64 rune_display_script_hash( const std::string& script ){
	std::string norm, line;
	size_t start = 0;

	while( start <= script.size() ){
		size_t end = script.find( '\n', start );
		if( end == std::string::npos )
			end = script.size();
		line = script.substr( start, end - start );
		size_t b = line.find_first_not_of( " \t\r" );
		if( b != std::string::npos ){
			size_t e = line.find_last_not_of( " \t\r" );
			if( !norm.empty() )
				norm += '\n';
			norm += line.substr( b, e - b + 1 );
		}
		start = end + 1;
	}
	return rune_display_fnv1a( norm );
}

static uint64 rune_display_set_hash( const s_set_rune& set ){
	std::string acc;
	char buf[64];

	for( const auto& it : set.display_script_hash ){
		safesnprintf( buf, sizeof( buf ), "%u:%016llx\n", static_cast<uint32>( it.first ), static_cast<unsigned long long>( it.second ) );
		acc += buf;
	}
	return rune_display_fnv1a( acc );
}

static const s_rune_display_state* rune_display_state_find( int32 efst ){
	for( const auto& st : rune_display_states ){
		if( st.efst == efst )
			return &st;
	}
	return nullptr;
}

/// EFST of the current rune state, using exactly what rune_active_bonus used (tag, set, grade, pieces)
static int32 rune_display_efst( const map_session_data* sd ){
	const auto& d = sd->runeactivated_data;

	if( d.tagID == 0 || d.runesetid == 0 )
		return 0;

	auto it = rune_display_enabled.find( d.runesetid );

	if( it == rune_display_enabled.end() || it->second->tag != d.tagID )
		return 0;
	if( d.upgrade >= NEED_RUNE_DISPLAY_GRADES || d.bookNumber >= NEED_RUNE_DISPLAY_PIECES )
		return 0;

	return it->second->efst[d.upgrade][d.bookNumber];
}

/**
 * Check the generated table against the loaded rune_db (called after every load/reload).
 * A set whose scripts changed since generation keeps its effects but shows no icon.
 */
void rune_display_validate(){
	size_t total = 0;

	rune_display_enabled.clear();

	for( const auto& def : rune_display_sets ){
		std::shared_ptr<s_rune_db> rune = rune_db.find( def.tag );
		std::shared_ptr<s_set_rune> set = rune != nullptr ? util::umap_find( rune->sets, def.setid ) : nullptr;

		if( set == nullptr ){
			ShowWarning( "rune tooltip: set %u (tag %u) is not in rune_db, its icon stays hidden.\n", def.setid, def.tag );
			continue;
		}
		if( rune_display_set_hash( *set ) != def.script_hash ){
			ShowWarning( "rune tooltip: set %u (%s) scripts changed since the tooltip table was generated, its icon stays hidden. Run tools/need_rune_tooltip/generate.py.\n", def.setid, set->name.c_str() );
			continue;
		}
		rune_display_enabled[def.setid] = &def;
	}

	for( const auto& rune : rune_db ){
		for( const auto& set : rune.second->sets ){
			total++;
			bool known = false;

			for( const auto& def : rune_display_sets ){
				if( def.setid == set.first ){
					known = true;
					break;
				}
			}
			if( !known )
				ShowWarning( "rune tooltip: set %u (%s) has no tooltip states, its icon stays hidden. Run tools/need_rune_tooltip/generate.py.\n", set.first, set.second->name.c_str() );
		}
	}

	ShowStatus( "rune tooltip: %" PRIuPTR "/%" PRIuPTR " rune sets show a buff icon.\n", rune_display_enabled.size(), total );
}

/**
 * Show, swap or remove the rune icon after status_calc_pc. Sends nothing while the state is unchanged.
 * Before the player is on a map nothing is sent; clif_parse_LoadEndAck sets resend and recalculates.
 */
void rune_display_refresh( map_session_data* sd ){
	nullpo_retv( sd );

	auto& rd = sd->rune_display;

	if( rd.checking || sd->state.autotrade || sd->prev == nullptr )
		return;
	if( rd.want == rd.sent && !rd.resend )
		return;

	if( rd.sent != 0 && rd.sent != rd.want )
		clif_status_change_sub( sd, sd->id, rd.sent, 0, 0, 0, 0, 0, SELF );
	if( rd.want != 0 )
		clif_status_change_sub( sd, sd->id, rd.want, 1, INFINITE_TICK, 0, 0, 0, SELF );

	rd.sent = rd.want;
	rd.resend = false;
}

/**
 * @runetooltipcheck: run every (set, grade, pieces) through the real status_calc_pc and compare
 * the icon chosen and the bonuses the rune scripts passed to pc_bonus/pc_bonus2 with the table.
 * The player's rune state is restored afterwards. Current HP/SP may be clamped by MaxHP/SP changes.
 */
bool rune_display_check( map_session_data* sd, std::vector<std::string>& report ){
	nullpo_retr( false, sd );

	const auto saved = sd->runeactivated_data;
	std::vector<s_rune_display_bonus> log;
	size_t states = 0, shown = 0, fails = 0, skipped = 0;
	char buf[256];

	sd->rune_display.checking = true;
	sd->rune_display.check_log = &log;
	sd->runeactivated_data.loaded = false;	// keeps rune_active_bonus from sending its rune UI refresh on every pass

	for( const auto& def : rune_display_sets ){
		if( rune_display_enabled.find( def.setid ) == rune_display_enabled.end() ){
			skipped++;
			fails++;
			safesnprintf( buf, sizeof( buf ), "set %u: disabled (not in rune_db or script hash mismatch)", def.setid );
			report.push_back( buf );
			continue;
		}

		for( uint16 g = 0; g < NEED_RUNE_DISPLAY_GRADES; g++ ){
			for( uint8 p = 0; p < NEED_RUNE_DISPLAY_PIECES; p++ ){
				sd->runeactivated_data.tagID = def.tag;
				sd->runeactivated_data.runesetid = def.setid;
				sd->runeactivated_data.upgrade = g;
				sd->runeactivated_data.bookNumber = p;
				log.clear();
				status_calc_pc( sd, SCO_FORCE );
				states++;

				int32 want = def.efst[g][p];
				std::map<std::pair<int32, int32>, int32> got, expect;

				for( const auto& b : log )
					got[std::make_pair( b.type, b.type2 )] += b.val;
				for( auto it = got.begin(); it != got.end(); ){
					if( it->second == 0 )
						it = got.erase( it );
					else
						++it;
				}

				if( want != 0 ){
					const s_rune_display_state* st = rune_display_state_find( want );

					shown++;
					if( st == nullptr || st->setid != def.setid ){
						fails++;
						safesnprintf( buf, sizeof( buf ), "set %u +%u %u pcs: EFST %d has no state row", def.setid, g, p, want );
						report.push_back( buf );
						continue;
					}
					for( uint8 i = 0; i < st->count; i++ )
						expect[std::make_pair( st->bonus[i].type, st->bonus[i].type2 )] += st->bonus[i].val;
				}

				if( sd->rune_display.want != want || got != expect ){
					fails++;
					if( report.size() < 20 ){
						safesnprintf( buf, sizeof( buf ), "set %u +%u %u pcs: icon %d (table %d), bonuses %" PRIuPTR " (table %" PRIuPTR ")%s",
							def.setid, g, p, sd->rune_display.want, want, got.size(), expect.size(), got != expect ? " VALUES DIFFER" : "" );
						report.push_back( buf );
					}
				}
			}
		}
	}

	sd->runeactivated_data = saved;
	sd->rune_display.check_log = nullptr;
	sd->rune_display.checking = false;
	sd->rune_display.resend = true;
	status_calc_pc( sd, SCO_FORCE );

	safesnprintf( buf, sizeof( buf ), "rune tooltip check: %" PRIuPTR " sets, %" PRIuPTR " states (%" PRIuPTR " with icon), %" PRIuPTR " disabled sets, %" PRIuPTR " failures -> %s",
		sizeof( rune_display_sets ) / sizeof( rune_display_sets[0] ), states, shown, skipped, fails, fails == 0 ? "PASS" : "FAIL" );
	report.push_back( buf );

	return fails == 0;
}

/**
 * Get location of rune decomposition database
 * @author [Shakto]
 **/
const std::string RuneDecompoDatabase::getDefaultLocation() {
	return std::string(db_path) + "/runedecomposition_db.yml";
}

/**
 * Read rune book YML db
 * @author [Shakto]
 **/
uint64 RuneDecompoDatabase::parseBodyNode( const ryml::NodeRef &node ){
	uint32 id;

	if( !this->asUInt32( node, "Id", id ) )
		return 0;

	std::shared_ptr<s_runedecompo_db> runedecompo = this->find(id);
	bool exists = runedecompo != nullptr;

	if (!exists) {
		if (!this->nodesExist(node, {"Materials" }))
			return 0;

		runedecompo = std::make_shared<s_runedecompo_db>();
		runedecompo->id = id;
	}

	if( this->nodeExists( node, "Materials" ) ){
		for( const ryml::NodeRef& materialNode : node["Materials"] ){
			std::string item_name;

			if (!this->asString(materialNode, "Material", item_name))
				return false;

			std::shared_ptr<item_data> item = item_db.search_aegisname( item_name.c_str() );

			if (item == nullptr) {
				this->invalidWarning(materialNode["Material"], "Rune Decompo %d item %s does not exist, skipping.\n", runedecompo->id, item_name.c_str());
				continue;
			}

			t_itemid material_id = item->nameid;

			std::shared_ptr<s_runedecompo_material_data> runedecompomat;

			auto isRunedecompomat = runedecompo->materials.find(material_id);
			bool material_exists = isRunedecompomat != runedecompo->materials.end();

			if( !material_exists ){
				runedecompomat = std::make_shared<s_runedecompo_material_data>();
				runedecompomat->material = material_id;
			} else
				return 0;

			uint16 amountmin;

			runedecompomat->amountmin = 0;
			if (this->nodeExists(materialNode, "Amountmin")) {

				if (!this->asUInt16(materialNode, "Amountmin", amountmin))
					return 0;

				if( amountmin > MAX_AMOUNT ){
					this->invalidWarning( materialNode["Amountmin"], "Amount %hu is too high, capping to MAX_AMOUNT...\n", amountmin );
					amountmin = MAX_AMOUNT;
				}
			} else {
				if (!material_exists )
					amountmin = 1;
			}

			runedecompomat->amountmin = amountmin;

			uint16 amountmax;

			if (this->nodeExists(materialNode, "Amountmax")) {

				if (!this->asUInt16(materialNode, "Amountmax", amountmax))
					return 0;

				if( amountmax < amountmin ){
					this->invalidWarning( materialNode["Amountmax"], "Amountmax %hu is lower than amountmin, capping to amountmin...\n", amountmax );
					amountmax = amountmin;
				}
				if( amountmax > MAX_AMOUNT ){
					this->invalidWarning( materialNode["Amountmax"], "Amountmax %hu is too high, capping to MAX_AMOUNT...\n", amountmax );
					amountmax = MAX_AMOUNT;
				}
			} else {
				if (!material_exists )
					amountmax = 1;
			}

			runedecompomat->amountmax = amountmax;

			uint32 chance;

			if( this->nodeExists( materialNode, "Chance" ) ){

				if( !this->asUInt32Rate( materialNode, "Chance", chance, 100000 ) ){
					return 0;
				}

				runedecompomat->chance = chance;
			} else {
				runedecompomat->chance = 100000;
			}

			if( amountmax > 0)
				runedecompo->materials[material_id] = *runedecompomat;
			else
				runedecompo->materials.erase( material_id );

			//ShowError("id %d item id %d amountmin %d amountmax %d chance %d runedecompomat->chance %d material size %d \n",id,item->nameid,amountmin,amountmax,chance,runedecompomat->chance,runedecompo->materials.size());
		}
	}

	if (!exists)
		this->put( runedecompo->id, runedecompo );

	return 1;
}

/**
 * Get location of rune book database
 * @author [Shakto]
 **/
const std::string RuneBookDatabase::getDefaultLocation() {
	return std::string(db_path) + "/runebook_db.yml";
}

/**
 * Read rune book YML db
 * @author [Shakto]
 **/
uint64 RuneBookDatabase::parseBodyNode( const ryml::NodeRef &node ){
	uint32 id;

	if( !this->asUInt32( node, "Id", id ) )
		return 0;

	std::shared_ptr<s_runebook_db> runebook = this->find(id);
	bool exists = runebook != nullptr;

	if (!exists) {
		if (!this->nodesExist(node, { "Name", "Materials" }))
			return 0;

		runebook = std::make_shared<s_runebook_db>();
		runebook->id = id;
	}

	std::string name;

	if (this->nodeExists(node, "Name")) {
		if( !this->asString( node, "Name", name ) )
			return 0;

		runebook->name = name;
	} else
		return 0;
	//ShowError("id %d, name %s \n",runebook->id,runebook->name.c_str());

	uint32 temp_id = runebook_db.search_name(name.c_str());
	if (temp_id != 0)
		this->nameToBookId.erase( name );
	else
		this->nameToBookId[name] = id;

	if( this->nodeExists( node, "Materials" ) ){
		for( const ryml::NodeRef& materialNode : node["Materials"] ){
			std::string item_name;

			if (!this->asString(materialNode, "Material", item_name))
				return false;

			std::shared_ptr<item_data> item = item_db.search_aegisname( item_name.c_str() );

			if (item == nullptr) {
				this->invalidWarning(materialNode["Material"], "Rune Book %d item %s does not exist, skipping.\n", runebook->id, item_name.c_str());
				continue;
			}

			t_itemid material_id = item->nameid;
			bool material_exists = util::umap_find( runebook->materials, material_id ) != nullptr;
			uint16 amount = 0;

			if (this->nodeExists(materialNode, "Amount")) {

				if (!this->asUInt16(materialNode, "Amount", amount))
					return 0;

				if( amount > MAX_AMOUNT ){
					this->invalidWarning( materialNode["Amount"], "Amount %hu is too high, capping to MAX_AMOUNT...\n", amount );
					amount = MAX_AMOUNT;
				}
			} else {
				if (!material_exists )
					amount = 1;
			}

			if( amount > 0 )
				runebook->materials[material_id] = amount;
			else
				runebook->materials.erase( material_id );

			//ShowError("item id %d amount %d \n",item->nameid,amount);
		}
	}

	if (!exists)
		this->put( runebook->id, runebook );

	return 1;
}

uint32 RuneBookDatabase::search_name( const char* name ){
	uint32 *bookid = util::umap_find(this->nameToBookId, std::string(name));

	if(bookid == nullptr)
		return 0;

	return *bookid;
}

/**
 * Get location of rune book database
 * @author [Shakto]
 **/
const std::string RuneDatabase::getDefaultLocation() {
	return std::string(db_path) + "/rune_db.yml";
}

/**
 * Read rune book YML db
 * @author [Shakto]
 **/
uint64 RuneDatabase::parseBodyNode( const ryml::NodeRef &node ){
	uint16 id;

	if( !this->asUInt16( node, "Id", id ) )
		return 0;

	std::shared_ptr<s_rune_db> rune = this->find(id);
	bool exists = rune != nullptr;

	if (!exists) {
		if (!this->nodesExist(node, { "Name", "Set" }))
			return 0;

		rune = std::make_shared<s_rune_db>();
		rune->id = id;
	}

	if (this->nodeExists(node, "Name")) {
		std::string name;

		if( !this->asString( node, "Name", name ) )
			return 0;

		rune->name = name;
	} else
		return 0;

	//ShowError("Rune id %d, name %s \n",rune->id,rune->name.c_str());

	if (this->nodeExists(node, "Set")) {
		for( const ryml::NodeRef& setNode : node["Set"] ){
			uint32 setid;

			if( !this->asUInt32( setNode, "Id", setid ) )
				return 0;

			std::shared_ptr<s_set_rune> set = util::umap_find( rune->sets, setid );

			bool set_exists = set != nullptr;

			if( !set_exists ){
				set = std::make_shared<s_set_rune>();
				set->id = setid;
			}

			std::string setname;
			if (this->nodeExists(setNode, "Name")) {
				if( !this->asString( setNode, "Name", setname ) )
					return 0;

				set->name = setname;
			} else
				return 0;

			//ShowError("set %d name %s \n",set->id,set->name.c_str());

			if (this->nodeExists(setNode, "Books")) {
				for( const ryml::NodeRef& bookNode : setNode["Books"] ){
					uint16 slot;

					if (!this->asUInt16(bookNode, "Slot", slot))
						return 0;

					if( slot > MAX_RUNESLOT ){
						this->invalidWarning( bookNode["Slot"], "Slot %hu is too high...\n", slot );
						return 0;
					}

					uint32* slot_bookid = util::umap_find( set->slots, slot );
					bool slot_exists = slot_bookid != nullptr;

					if( slot_exists )
						set->slots.erase( slot );

					std::string bookname;
					uint32 bookid;

					if (this->nodeExists(bookNode, "Name")) {
						if( !this->asString( bookNode, "Name", bookname ) )
							return 0;

						bookid = runebook_db.search_name( bookname.c_str() );
						if(bookid == 0)
							return 0;
					}

					set->slots[slot] = bookid;

					//ShowError("slot %d, bookid %d \n",slot,bookid);
				}
			} else
				return 0;

			if (this->nodeExists(setNode, "Reward")) {
				for (const ryml::NodeRef& rewardNode : setNode["Reward"]) {
					uint16 slot;

					if (!this->asUInt16(rewardNode, "Slot", slot))
						return 0;

					if (slot <=0 || slot > MAX_REWARDSLOT) {
						this->invalidWarning(rewardNode["Slot"], "Slot %hu, need to be between 0 and %d\n", slot, MAX_REWARDSLOT);
						return 0;
					}

					std::string item_name;

					if (!this->asString(rewardNode, "Name", item_name))
						return false;

					std::shared_ptr<item_data> item = item_db.search_aegisname(item_name.c_str());

					if (item == nullptr) {
						this->invalidWarning(rewardNode["Name"], "Rune Reward of set %d item %s does not exist, skipping.\n", set->id, item_name.c_str());
						continue;
					}

					t_itemid material_id = item->nameid;

					set->rewards[slot] = item->nameid;
				}
			}

				if( this->nodeExists( setNode, "Activation" ) ){
				for( const ryml::NodeRef& activationNode : setNode["Activation"] ){
					std::string item_name;

					if (!this->asString(activationNode, "Material", item_name))
						return false;

					std::shared_ptr<item_data> item = item_db.search_aegisname( item_name.c_str() );

					if (item == nullptr) {
						this->invalidWarning(activationNode["Activation"], "Rune Book %d item %s does not exist, skipping.\n", set->id, item_name.c_str());
						continue;
					}

					t_itemid material_id = item->nameid;
					bool material_exists = util::umap_find( set->activation_materials, material_id ) != nullptr;
					uint16 amount;

					if (this->nodeExists(activationNode, "Amount")) {

						if (!this->asUInt16(activationNode, "Amount", amount))
							return 0;

						if( amount > MAX_AMOUNT ){
							this->invalidWarning( activationNode["Amount"], "Amount %hu is too high, capping to MAX_AMOUNT...\n", amount );
							amount = MAX_AMOUNT;
						}
					} else {
						if (!material_exists )
							amount = 1;
					}

					if( amount > 0 )
						set->activation_materials[material_id] = amount;
					else
						set->activation_materials.erase( material_id );

					//ShowError("Activation item id %d amount %d \n",item->nameid,amount);
				}
			}

			if( this->nodeExists( setNode, "Scripts" ) ){
				for( const ryml::NodeRef& scriptNode : setNode["Scripts"] ){
					uint16 amount;

					if (!this->asUInt16(scriptNode, "Amount", amount))
						return 0;

					std::shared_ptr<s_script_rune> script_data = util::umap_find( set->scripts, amount );
					bool script_exists = script_data != nullptr;

					if( !script_exists ){
						script_data = std::make_shared<s_script_rune>();
						script_data->amount = amount;
					} else
						return 0;

					if (this->nodeExists(scriptNode, "Script")) {
						std::string script;

						if (!this->asString(scriptNode, "Script", script))
							return 0;

						if (set->scripts[amount] && set->scripts[amount]->script) {
							script_free_code(set->scripts[amount]->script);
							set->scripts[amount]->script = nullptr;
						}

						script_data->script = parse_script(script.c_str(), this->getCurrentFile().c_str(), this->getLineNumber(scriptNode["Script"]), SCRIPT_IGNORE_EXTERNAL_BRACKETS);
						set->display_script_hash[amount] = rune_display_script_hash(script);
					}

					if( !script_exists )
						set->scripts[amount] = script_data;
				}
			}

			if( this->nodeExists( setNode, "Upgrades" ) ){
				for( const ryml::NodeRef& upgradeNode : setNode["Upgrades"] ){

					uint16 grade;

					if (!this->asUInt16(upgradeNode, "Grade", grade))
						return 0;

					std::shared_ptr<s_upgrade_rune> upgrade = util::umap_find( set->upgrades, grade );
					bool upgrade_exists = upgrade != nullptr;

					if( !upgrade_exists ){
						upgrade = std::make_shared<s_upgrade_rune>();
						upgrade->grade = grade;
					} else
						return 0;

					if( this->nodeExists( upgradeNode, "Chance" ) ){
						uint32 chance;

						if( !this->asUInt32( upgradeNode, "Chance", chance ) ){
							return 0;
						}

						if( chance > 100000 ){
							this->invalidWarning( upgradeNode["Chance"], "Node \"Chance\" value %u exceeds maximum of 100000.\n", chance );
							return 0;
						}

						upgrade->chance = chance;
					}

					if( this->nodeExists( upgradeNode, "ChancePerFail" ) ){
						uint32 chanceperfail;

						if( !this->asUInt32( upgradeNode, "ChancePerFail", chanceperfail ) ){
							return 0;
						}

						if( chanceperfail > 100000 ){
							this->invalidWarning( upgradeNode["ChancePerFail"], "Node \"ChancePerFail\" value %u exceeds maximum of 100000.\n", chanceperfail );
							return 0;
						}

						upgrade->chanceperfail = chanceperfail;
					}

					//ShowError("Upgrades grade %d chance %d chanceperfail %d \n",grade,upgrade->chance,upgrade->chanceperfail);

					if( this->nodeExists( upgradeNode, "Materials" ) ){
						for( const ryml::NodeRef& setmaterialNode : upgradeNode["Materials"] ){
							std::string item_name;

							if (!this->asString(setmaterialNode, "Material", item_name))
								return false;

							std::shared_ptr<item_data> item = item_db.search_aegisname( item_name.c_str() );

							if (item == nullptr) {
								this->invalidWarning(setmaterialNode["Materials"], "Rune Book %d grade %d item %s does not exist, skipping.\n", set->id, grade, item_name.c_str());
								continue;
							}

							t_itemid material_id = item->nameid;
							bool material_exists = util::umap_find( upgrade->materials, material_id ) != nullptr;
							uint16 amount = 0;

							if (this->nodeExists(setmaterialNode, "Amount")) {

								if (!this->asUInt16(setmaterialNode, "Amount", amount))
									return 0;

								if( amount > MAX_AMOUNT ){
									this->invalidWarning( setmaterialNode["Amount"], "Amount %hu is too high, capping to MAX_AMOUNT...\n", amount );
									amount = MAX_AMOUNT;
								}
							} else {
								if (!material_exists )
									amount = 1;
							}

							if( amount > 0 )
								upgrade->materials[material_id] = amount;
							else
								upgrade->materials.erase( material_id );

							//ShowError("Activation item id %d amount %d \n",item->nameid,amount);
						}
					}

					if( !upgrade_exists )
						set->upgrades[grade] = upgrade;

				}
			}

			if( !set_exists ){
				rune->sets[setid] = set;
			}

		}
	} else
		return 0;

	if (!exists)
		this->put( rune->id, rune );

	return 1;
}

void rune_save(map_session_data* sd) {
	//runeSet
	for (const auto& set_data : sd->runeSets) {
		//ShowError("Save char_id %d rune_id %u set_id %u selected %u upgrade %u failcount %u \n ", sd->status.char_id, set_data.tagId, set_data.setId, set_data.selected, set_data.upgrade, set_data.failcount);
		if (SQL_ERROR == Sql_Query(mmysql_handle,
			"INSERT IGNORE INTO `runes` (`char_id`,`rune_id`,`set_id`,`selected`,`upgrade`,`failcount`,`reward`) "
			"VALUES (%u, %u, %u, %u, %u, %u, %u) "
			"ON DUPLICATE KEY UPDATE "
			"`selected` = %u, "
			"`upgrade` = %u, "
			"`failcount` = %u, "
			"`reward` = %u ",
			sd->status.char_id, // char_id
			set_data.tagId,       // tag id
			set_data.setId,        // set_id
			set_data.selected,  // selected
			set_data.upgrade,   // upgrade
			set_data.failcount, // failcount
			set_data.reward, // reward
			set_data.selected,  // selected
			set_data.upgrade,   // upgrade
			set_data.failcount, // failcount
			set_data.reward // reward
		)) {
			Sql_ShowDebug(mmysql_handle);
		}
	}

	// runes_book
	for (const auto& book_data : sd->runeBooks) {
		if (SQL_ERROR == Sql_Query(mmysql_handle,
			"INSERT IGNORE INTO `runes_book` (`char_id`,`rune_id`,`book_id`) "
			"VALUES (%u, %u, %u) ",
			sd->status.char_id, // char_id
			book_data.tagId,    // tag_id
			book_data.bookId    // book_id
		)) {
			Sql_ShowDebug(mmysql_handle);
		}
	}

}

void rune_load(map_session_data* sd) {
	sd->runeSets.clear();
	sd->runeBooks.clear();

	bool isSelected = false;

	// Load rune sets associated with the current rune_id from the 'runes' table
	if (Sql_Query(mmysql_handle,
		"SELECT `rune_id`, `set_id`, `selected`, `upgrade`, `failcount`, `reward` "
		"FROM `runes` "
		"WHERE `char_id` = %d",
		sd->status.char_id) != SQL_SUCCESS)
	{
		Sql_ShowDebug(mmysql_handle);
	}

	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		s_runeset_data set_data;
		char* data;
		Sql_GetData(mmysql_handle, 0, &data, NULL); set_data.tagId = atoi(data); //0
		Sql_GetData(mmysql_handle, 1, &data, NULL); set_data.setId = strtoul(data, NULL, 10); //0
		Sql_GetData(mmysql_handle, 2, &data, NULL); set_data.selected = atoi(data); //2
		Sql_GetData(mmysql_handle, 3, &data, NULL); set_data.upgrade = atoi(data); //4
		Sql_GetData(mmysql_handle, 4, &data, NULL); set_data.failcount = atoi(data); //5
		Sql_GetData(mmysql_handle, 5, &data, NULL); set_data.reward = atoi(data); //6
		if (set_data.tagId <= 0 || set_data.setId <= 0 || set_data.upgrade < 0 || set_data.failcount < 0 || set_data.reward < 0) {
			// Datas invalids sd->runeSets
			continue;
		}
		if (set_data.selected && !isSelected) {
			isSelected = true;
			sd->runeactivated_data.tagID = set_data.tagId;
			sd->runeactivated_data.runesetid = set_data.setId;
			sd->runeactivated_data.upgrade = set_data.upgrade;
		} else if (set_data.selected) {
			set_data.selected = false;
		}
		sd->runeSets.push_back(set_data);
	}
	Sql_FreeResult(mmysql_handle);

	// Load rune books associated with the current rune_id from the 'runes_book' table
	if (Sql_Query(mmysql_handle,
		"SELECT `rune_id`, `book_id` "
		"FROM `runes_book` "
		"WHERE `char_id` = %d",
		sd->status.char_id) != SQL_SUCCESS)
	{
		Sql_ShowDebug(mmysql_handle);
	}

	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		s_runebook_data book;
		char* data;
		Sql_GetData(mmysql_handle, 0, &data, NULL); book.tagId = atoi(data);
		Sql_GetData(mmysql_handle, 1, &data, NULL); book.bookId = strtoul(data, NULL, 10);
		sd->runeBooks.push_back(book);
	}
	Sql_FreeResult(mmysql_handle);

	if(!isSelected){
		sd->runeactivated_data.loaded = false;
		sd->runeactivated_data.tagID = 0;
		sd->runeactivated_data.upgrade = 0;
		sd->runeactivated_data.runesetid = 0;
	}
	sd->runeactivated_data.bookNumber = 0;

}

int32 rune_bookactivate(map_session_data* sd, uint16 tagID, uint32 runebookid){
	if (!sd) return ZC_RUNESET_TABLET_INVALID2;

	//ShowError("rune_bookactivate \n");

	uint32 runesetid = 0;

	// Check if book exist
	std::shared_ptr<s_runebook_db> runebook = runebook_db.find( runebookid );

	if( runebook == nullptr )
		return ZC_RUNESET_TABLET_INVALID2;

	// Check if rune book exist for this rune set
	std::shared_ptr<s_rune_db> rune = rune_db.find( tagID );

	if( rune == nullptr )
		return ZC_RUNESET_TABLET_INVALID2;

	bool isBookinSet = false;
	for (const auto& set_pair : rune->sets) {
		const auto& set_data = set_pair.second;
		for (const auto& slot_pair : set_data->slots) {
			if (slot_pair.second == runebookid) {
				runesetid = set_pair.first;
				isBookinSet = true;
				break;
			}
		}
		if (isBookinSet)
			break;
	}

	if(!isBookinSet)
		return ZC_RUNESET_TABLET_INVALID2;

	// Check if already activated
	auto it_setrune_data = std::find_if(sd->runeBooks.begin(), sd->runeBooks.end(), [tagID, runebookid](const s_runebook_data& runeBook) {
		return runeBook.tagId == tagID && runeBook.bookId == runebookid;
	});

	if (it_setrune_data != sd->runeBooks.end()){
		return ZC_RUNEBOOK_ALRDYACTIVATED;
	}

	// Check items
	std::unordered_map<t_itemid, uint16> materials;

	for( const auto& entry : runebook->materials ){
		int16 idx = pc_search_inventory( sd, entry.first );

		if( idx < 0 ){
			return ZC_RUNEBOOK_NOITEM;
		}

		if( sd->inventory.u.items_inventory[idx].amount < entry.second ){
			return ZC_RUNEBOOK_NOITEM;
		}

		materials[idx] = entry.second;
	}

	// Delete items
	for( const auto& entry : materials ){
		if( pc_delitem( sd, entry.first, entry.second, 0, 0, LOG_TYPE_ENCHANT )  != 0 ){
			return ZC_RUNEBOOK_NOITEM;
		}
	}

	// Saving new book
	s_runebook_data book;
	book.tagId = tagID;
	book.bookId = runebookid;

	sd->runeBooks.push_back(book);

	//Update the rune book number if one rune is activated
	if(sd->runeactivated_data.runesetid){
		rune_count_bookactivated(sd, tagID, runesetid);
		status_calc_pc(sd, SCO_FORCE);
	}

	return ZC_RUNEBOOK_SUCCESS;
}

int32 rune_setactivate(map_session_data* sd, uint16 tagID, uint32 runesetid){
	if (!sd) return ZC_RUNESET_TABLET_INVALID;

	//ShowError("rune_setactivate \n");

	// Check if already activated
	bool isRunetag = false;
	auto it_setrune_data = std::find_if(sd->runeSets.begin(), sd->runeSets.end(), [tagID, runesetid](const s_runeset_data& runeSet) {
		return runeSet.tagId == tagID && runeSet.setId == runesetid;
	});

	if (it_setrune_data != sd->runeSets.end()){
		return ZC_RUNESET_ALRDYACTIVATED;
	}

	// Check if rune exist
	std::shared_ptr<s_rune_db> rune = rune_db.find( tagID );

	if( rune == nullptr )
		return ZC_RUNESET_TABLET_INVALID;

	bool isSet = false;
	for (const auto& set_pair : rune->sets) {
		if(set_pair.first == runesetid){
			const auto& set_data = set_pair.second;

			// Check items
			std::unordered_map<t_itemid, uint16> materials;

			for( const auto& entry : set_data->activation_materials ){
				int16 idx = pc_search_inventory( sd, entry.first );

				if( idx < 0 ){
					return ZC_RUNEBOOK_NOITEM;
				}

				if( sd->inventory.u.items_inventory[idx].amount < entry.second ){
					return ZC_RUNEBOOK_NOITEM;
				}

				materials[idx] = entry.second;
			}

			// Delete items
			for( const auto& entry : materials ){
				if( pc_delitem( sd, entry.first, entry.second, 0, 0, LOG_TYPE_ENCHANT )  != 0 ){
					return ZC_RUNEBOOK_NOITEM;
				}
			}

			isSet = true;
			break;
		}
	}

	if(!isSet)
		return ZC_RUNESET_TABLET_INVALID;

	// Saving new book
	s_runeset_data set_data;
	set_data.tagId = tagID;
	set_data.setId = runesetid;
	set_data.selected = 0;
	set_data.upgrade = 0;
	set_data.failcount = 0;
	set_data.reward = 0;
	//ShowError("Activate char_id %d rune_id %u set_id %u selected %u upgrade %u failcount %u \n ", sd->status.char_id, set_data.tagId, set_data.setId, set_data.selected, set_data.upgrade, set_data.failcount);
	sd->runeSets.push_back(set_data);

	return ZC_RUNESET_SUCCESS;
}

std::tuple<e_runereward_result, uint8> rune_askreward(map_session_data* sd, uint16 tagID, uint32 runesetid, uint8 reward) {
	uint8 bookNumber = 0;
	enum e_additem_result flag;
	t_itemid item_id;
	size_t max_reward = 0;

	//ShowError("rune_askreward tagID %d runesetid %d reward %d \n", tagID, runesetid, reward);
	auto it_setrune_data = std::find_if(sd->runeSets.begin(), sd->runeSets.end(), [tagID, runesetid](const s_runeset_data& runeSet) {
		return runeSet.tagId == tagID && runeSet.setId == runesetid;
	});

	if (it_setrune_data == sd->runeSets.end())
		return std::make_tuple(ZC_RUNEREWARD_FAIL, 0); // Not found

	// Check if rune exist
	std::shared_ptr<s_rune_db> rune_data = rune_db.find(tagID);

	if (rune_data == nullptr)
		return std::make_tuple(ZC_RUNEREWARD_INVALID, 0);

	// Check if rune set exist
	std::shared_ptr<s_set_rune> runeset_data = util::umap_find(rune_data->sets, runesetid);

	if (runeset_data == nullptr)
		return std::make_tuple(ZC_RUNEREWARD_INVALID, 0);

	// Check if rune reward exist
	auto reward_it = runeset_data->rewards.find(reward);
	if (reward_it == runeset_data->rewards.end())
		return std::make_tuple(ZC_RUNEREWARD_INFONOTMATCH, 0);
	else {
		item_id = reward_it->second;
	}

	// Get max reward
	max_reward = runeset_data->rewards.size();

	// Check if reward is more or already have it
	if (it_setrune_data->reward >= reward)
		return std::make_tuple(ZC_RUNEREWARD_FAIL, 0);

	// count the book of the rune data activated
	for (const auto& slot_pair : runeset_data->slots) {
		uint32 bookId = slot_pair.second;

		// Check if the book id exists in the books vector
		for (const auto& book : sd->runeBooks) {
			if (book.tagId == tagID && book.bookId == bookId) {
				bookNumber++;
			}
		}
	}

	//ShowError("reward %d bookNumber %d num of slot in set %d \n", reward, bookNumber, runeset_data->slots.size());

	//Check if we have enough book for the reward
	if (bookNumber < reward && reward != MAX_REWARDSLOT)
		return std::make_tuple(ZC_RUNEREWARD_FAIL, 0);

	//Check if reward is the max, special check
	if(reward == MAX_REWARDSLOT && bookNumber < runeset_data->slots.size())
		return std::make_tuple(ZC_RUNEREWARD_FAIL, 0);

	struct item item_tmp = {};

	item_tmp.nameid = item_id;
	item_tmp.identify = 1;
	item_tmp.bound = 0;
	//ShowError("before rune_askreward success tagID %d runesetid %d reward %d it_setrune_data->reward %d \n", tagID, runesetid, reward, it_setrune_data->reward);

	if (flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND)) {
		clif_additem(sd, 0, 0, flag);
	}

	switch (flag) {
	case ADDITEM_SUCCESS:
		it_setrune_data->reward = reward;
		return std::make_tuple(ZC_RUNEREWARD_SUCCESS, reward);
		break;
	case ADDITEM_INVALID:
		return std::make_tuple(ZC_RUNEREWARD_FAIL, 0);
		break;
	case ADDITEM_OVERWEIGHT:
		return std::make_tuple(ZC_RUNEREWARD_WEIGH, 0);
		break;
	case ADDITEM_ITEM:
		return std::make_tuple(ZC_RUNEREWARD_MAXITEMS, 0);
		break;
	case ADDITEM_OVERITEM:
		return std::make_tuple(ZC_RUNEREWARD_INVENTORYSPACE, 0);
		break;
	default:
		return std::make_tuple(ZC_RUNEREWARD_FAIL, 0);
		break;
	}

	return std::make_tuple(ZC_RUNEREWARD_FAIL, 0);
}

std::tuple<uint8, uint16, uint16> rune_setupgrade(map_session_data* sd, uint16 tagID, uint32 runesetid){
	if (!sd) return std::make_tuple(ZC_RUNESET_TABLET_INVALID, 0, 0);

	//ShowError("rune_setupgrade \n");

	uint16 upgrade = 0;
	uint16 failcount = 0;

	auto it_setrune_data = std::find_if(sd->runeSets.begin(), sd->runeSets.end(), [tagID, runesetid](const s_runeset_data& runeSet) {
		return runeSet.tagId == tagID && runeSet.setId == runesetid;
	});

	if (it_setrune_data != sd->runeSets.end()) {
		upgrade = it_setrune_data->upgrade;
		failcount = it_setrune_data->failcount;
	} else
		return std::make_tuple(ZC_RUNESET_TABLET_INVALID3, 0, 0); 	// If not found then it is not activated

	//ShowError("Upgrade before : %d failcount %d \n",upgrade, failcount);

	// Check if rune exist
	std::shared_ptr<s_rune_db> rune_data = rune_db.find( tagID );

	if( rune_data == nullptr )
		return std::make_tuple(ZC_RUNESET_TABLET_INVALID, 0, 0);

	// Check if rune set exist
	std::shared_ptr<s_set_rune> runeset_data = util::umap_find( rune_data->sets, runesetid );

	if( runeset_data == nullptr )
		return std::make_tuple(ZC_RUNESET_TABLET_INVALID, 0, 0);

	// Check if rune upgrade exist
	std::shared_ptr<s_upgrade_rune> runeupgrade_data = util::umap_find( runeset_data->upgrades, upgrade );

	if( runeupgrade_data == nullptr )
		return std::make_tuple(ZC_RUNESET_TABLET_MAXUPGRADE, upgrade, failcount);

	//ShowError("Upgrade and Set exist \n");

	uint32 chance = runeupgrade_data->chance + (runeupgrade_data->chanceperfail * failcount);

	if( chance == 0 ){
		return std::make_tuple(ZC_RUNESET_TABLET_MAXUPGRADE, upgrade, failcount);
	}

	// Check upgrade material
	std::unordered_map<t_itemid, uint16> materials;

	for( const auto& entry : runeupgrade_data->materials ){
		int16 idx = pc_search_inventory( sd, entry.first );

		if( idx < 0 ){
			return std::make_tuple(ZC_RUNESET_NOITEM, upgrade, failcount);
		}

		if( sd->inventory.u.items_inventory[idx].amount < entry.second ){
			return std::make_tuple(ZC_RUNESET_NOITEM, upgrade, failcount);
		}

		materials[idx] = entry.second;
	}

	// Delete items
	for( const auto& entry : materials ){
		if( pc_delitem( sd, entry.first, entry.second, 0, 0, LOG_TYPE_ENCHANT )  != 0 ){
			return std::make_tuple(ZC_RUNESET_NOITEM, upgrade, failcount);
		}
	}

	//ShowError("Materials done \n");

	// Chance
	//uint32 rdm_chance = rnd_value( 0, 100000 );
	//ShowError("chance %d, random chance %d, runeupgrade_data->chance %d, runeupgrade_data->chanceperfail %d, failcount %d \n",chance,rdm_chance,runeupgrade_data->chance,runeupgrade_data->chanceperfail,failcount);

	//if( chance < 100000 && rdm_chance > chance )
	if( chance < 100000 && rnd_value( 0, 100000 ) > chance )
		failcount++;
	else {
		upgrade++;
		failcount = 0;
	}

	it_setrune_data->upgrade = upgrade;
	it_setrune_data->failcount = failcount;

	//Update the rune book number if one rune is activated
	if(sd->runeactivated_data.runesetid
		&& sd->runeactivated_data.tagID == tagID && sd->runeactivated_data.runesetid == runesetid){
			sd->runeactivated_data.upgrade = static_cast<uint8>(upgrade);
			status_calc_pc(sd, SCO_FORCE);
	}

	//ShowError("Upgrade after : %d failcount %d chance%d \n",upgrade, failcount, chance);
	return std::make_tuple(ZC_RUNESET_SUCCESS, upgrade, failcount);
}

bool rune_changestate(map_session_data* sd, uint16 tagID, uint32 runesetid){
	if (!sd) return false;

	//ShowError("0 - rune_changestate - tagID %d runesetid %d \n",tagID,runesetid);

	if( !runesetid ){
		for( auto& set_data : sd->runeSets ){
			set_data.selected = false;
		}

		sd->runeactivated_data.tagID = 0;
		sd->runeactivated_data.runesetid = 0;
		sd->runeactivated_data.upgrade = 0;
		sd->runeactivated_data.bookNumber = 0;
		status_calc_pc(sd, SCO_FORCE);
		return false;
	}

	s_runeset_data* selected_set = nullptr;

	for (auto& set_data : sd->runeSets) {
		set_data.selected = false;

		if( set_data.tagId == tagID && set_data.setId == runesetid ){
			selected_set = &set_data;
		}
	}

	if( selected_set == nullptr ){
		sd->runeactivated_data.tagID = 0;
		sd->runeactivated_data.runesetid = 0;
		sd->runeactivated_data.upgrade = 0;
		sd->runeactivated_data.bookNumber = 0;
		status_calc_pc(sd, SCO_FORCE);
		return false;
	}

	selected_set->selected = true;
	sd->runeactivated_data.tagID = selected_set->tagId;
	sd->runeactivated_data.runesetid = selected_set->setId;
	sd->runeactivated_data.upgrade = selected_set->upgrade;
	rune_count_bookactivated(sd, tagID, runesetid);
	status_calc_pc(sd, SCO_FORCE);

	return true;
}

void rune_count_bookactivated(map_session_data* sd, uint16 tagID, uint32 runesetid){
	nullpo_retv(sd);

	sd->runeactivated_data.bookNumber = 0;

	// Check if rune exist
	std::shared_ptr<s_rune_db> rune_data = rune_db.find( tagID );

	if( rune_data == nullptr )
		return;

	// Check if rune set exist
	std::shared_ptr<s_set_rune> runeset_data = util::umap_find( rune_data->sets, runesetid );

	if( runeset_data == nullptr )
		return;

	// Now let's iterate through slots and check if each slot's id exists in the books vector
	for (const auto& slot_pair : runeset_data->slots) {
		uint32 bookId = slot_pair.second;

		// Check if the book id exists in the books vector
		bool bookFound = false;
		for (const auto& book : sd->runeBooks) {
			if (book.tagId == tagID && book.bookId == bookId) {
				sd->runeactivated_data.bookNumber++;
			}
		}
	}
	//ShowError("sd->runeactivated_data.bookNumber %d \n",sd->runeactivated_data.bookNumber);
}

void rune_active_bonus(map_session_data *sd) {
	nullpo_retv(sd);

	//ShowError("rune_active_bonus execution -- sd->runeactivated_data.tagID %d sd->runeactivated_data.runesetid %d \n", sd->runeactivated_data.tagID, sd->runeactivated_data.runesetid);

	// Check if rune exist
	std::shared_ptr<s_rune_db> rune_data = rune_db.find( sd->runeactivated_data.tagID );

	if( rune_data == nullptr )
		return;

	// Check if rune set exist
	std::shared_ptr<s_set_rune> runeset_data = util::umap_find( rune_data->sets, sd->runeactivated_data.runesetid );

	if( runeset_data == nullptr )
		return;

	//ShowError("rune_active_bonus found in database \n");

	// Rune tooltip check: record what the scripts pass to pc_bonus/pc_bonus2 (null outside @runetooltipcheck)
	sd->rune_display.capture = sd->rune_display.check_log;

	// Now let's iterate through slots and check if each slot's id exists in the books vector
	for (const auto& script_pair : runeset_data->scripts) {
		 const std::shared_ptr<s_script_rune> script_data = script_pair.second;

		//ShowError("sd->runeactivated_data.bookNumber %d, script_data->amount %d \n",sd->runeactivated_data.bookNumber,script_data->amount);

		if(sd->runeactivated_data.bookNumber >= script_data->amount){
			run_script(script_data->script, 0, sd->id, 0);
		}
	}

	sd->rune_display.capture = nullptr;
	// Rune tooltip: the icon for the state these scripts were just run with (sent by rune_display_refresh)
	sd->rune_display.want = rune_display_efst(sd);

	if(sd->runeactivated_data.tagID && sd->runeactivated_data.loaded){
		clif_enablerefresh_rune2(sd,0,0);
		clif_enablerefresh_rune2(sd,sd->runeactivated_data.tagID,sd->runeactivated_data.runesetid);
	}
}

void rune_db_reload(){
	do_final_rune();
	do_init_rune();
}

void do_init_rune(){
	runedecompo_db.load();
	runebook_db.load();
	rune_db.load();
	rune_display_validate();
}

void do_final_rune(){
	rune_db.clear();
	runebook_db.clear();
	runedecompo_db.clear();
}
