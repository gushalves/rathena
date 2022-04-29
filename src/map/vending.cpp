// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "vending.hpp"

#include <memory>
#include <unordered_map>
#include <ctime>
#include <stdlib.h> // atoi

#include "../common/malloc.hpp" // aMalloc, aFree
#include "../common/nullpo.hpp"
#include "../common/showmsg.hpp" // ShowInfo
#include "../common/strlib.hpp"
#include "../common/timer.hpp"  // DIFF_TICK

#include "achievement.hpp"
#include "atcommand.hpp"
#include "battle.hpp"
#include "buyingstore.hpp"
#include "buyingstore.hpp" // struct s_autotrade_entry, struct s_autotrader
#include "chrif.hpp"
#include "clif.hpp"
#include "intif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "npc.hpp"
#include "path.hpp"
#include "pc.hpp"
#include "pc_groups.hpp"

static uint32 vending_nextid = 0; ///Vending_id counter
static DBMap *vending_db; ///DB holder the vender : charid -> map_session_data
static std::unordered_map<uint32, std::shared_ptr<assistant_data>> assistant_char_lookup;
static std::unordered_map<uint32, std::shared_ptr<assistant_data>> gid_assistant_lookup;
//Autotrader
static DBMap *vending_autotrader_db; /// Holds autotrader info: char_id -> struct s_autotrader
static void vending_autotrader_remove(struct s_autotrader *at, bool remove);
static int vending_autotrader_free(DBKey key, DBData *data, va_list ap);
TIMER_FUNC(vending_assistant_expire);

/**
 * Lookup to get the vending_db outside module
 * @return the vending_db
 */
DBMap * vending_getdb()
{
	return vending_db;
}

/**
 * Create an unique vending shop id.
 * @return the next vending_id
 */
static int vending_getuid(void)
{
	return ++vending_nextid;
}

/**
 * Make a player close his shop
 * @param sd : player session
 */
void vending_closevending(struct map_session_data* sd)
{
	nullpo_retv(sd);

	if( sd->state.vending ) {
		if( Sql_Query( mmysql_handle, "DELETE FROM `%s` WHERE vending_id = %d;", vending_items_table, sd->vender_id ) != SQL_SUCCESS ||
			Sql_Query( mmysql_handle, "DELETE FROM `%s` WHERE `id` = %d;", vendings_table, sd->vender_id ) != SQL_SUCCESS ) {
				Sql_ShowDebug(mmysql_handle);
		}

		sd->state.vending = false;
		sd->vender_id = 0;
		clif_closevendingboard(&sd->bl, 0);
		idb_remove(vending_db, sd->status.char_id);
	}
}

/**
 * Player request a shop's item list (a player shop)
 * @param sd : player requestion the list
 * @param id : vender account id (gid)
 */
void vending_vendinglistreq(struct map_session_data* sd, int id)
{
	struct map_session_data* vsd;
	nullpo_retv(sd);

	if (gid_assistant_lookup.find((uint32)id) != gid_assistant_lookup.end()) {
		auto& ad = gid_assistant_lookup[(uint32)id];

		if (!pc_can_give_items(sd)) { //check if both GMs are allowed to trade
			clif_displaymessage(sd->fd, msg_txt(sd, 246));
			return;
		}

		sd->vended_id = ad->vender_id;
		clif_assistant_store_itemlist(sd, ad.get());
	}
	else {
		if ((vsd = map_id2sd(id)) == NULL)
			return;
		if (!vsd->state.vending)
			return; // not vending

		if (!pc_can_give_items(sd) || !pc_can_give_items(vsd)) { //check if both GMs are allowed to trade
			clif_displaymessage(sd->fd, msg_txt(sd, 246));
			return;
		}

		sd->vended_id = vsd->vender_id;  // register vending uid

		clif_vendinglist(sd, vsd);
	}
}

/**
 * Calculates taxes for vending
 * @param sd: Vender
 * @param zeny: Total amount to tax
 * @return Total amount after taxes
 */
static double vending_calc_tax(struct map_session_data *sd, double zeny)
{
	if (battle_config.vending_tax && zeny >= battle_config.vending_tax_min)
		zeny -= zeny * (battle_config.vending_tax / 10000.);

	return zeny;
}

/**
 * Purchase item(s) from a shop
 * @param sd : buyer player session
 * @param aid : account id of vender
 * @param uid : shop unique id
 * @param data : items data who would like to purchase \n
 *	data := {<index>.w <amount>.w }[count]
 * @param count : number of different items he's trying to buy
 */
void vending_purchasereq(struct map_session_data* sd, int aid, int uid, const uint8* data, int count)
{
	int i, j, cursor, w, new_ = 0, blank, vend_list[MAX_VENDING];
	double z;
	struct s_vending vending[MAX_VENDING]; // against duplicate packets
	struct map_session_data* vsd = map_id2sd(aid);

	nullpo_retv(sd);

	if (gid_assistant_lookup.find(aid) != gid_assistant_lookup.end()) {
		vending_purchase_from_assistant(sd, aid, uid, data, count);
		return;
	}

	if( vsd == NULL || !vsd->state.vending || vsd->bl.id == sd->bl.id )
		return; // invalid shop

	if( vsd->vender_id != uid ) { // shop has changed
		clif_buyvending(sd, 0, 0, 6);  // store information was incorrect
		return;
	}

	if( !searchstore_queryremote(sd, aid) && ( sd->bl.m != vsd->bl.m || !check_distance_bl(&sd->bl, &vsd->bl, AREA_SIZE) ) )
		return; // shop too far away

	searchstore_clearremote(sd);

	if( count < 1 || count > MAX_VENDING || count > vsd->vend_num )
		return; // invalid amount of purchased items

	blank = pc_inventoryblank(sd); //number of free cells in the buyer's inventory

	// duplicate item in vending to check hacker with multiple packets
	memcpy(&vending, &vsd->vending, sizeof(vsd->vending)); // copy vending list

	// some checks
	z = 0.; // zeny counter
	w = 0;  // weight counter
	for( i = 0; i < count; i++ ) {
		short amount = *(uint16*)(data + 4*i + 0);
		short idx    = *(uint16*)(data + 4*i + 2);
		idx -= 2;

		if( amount <= 0 )
			return;

		// check of item index in the cart
		if( idx < 0 || idx >= MAX_CART )
			return;

		ARR_FIND( 0, vsd->vend_num, j, vsd->vending[j].index == idx );
		if( j == vsd->vend_num )
			return; //picked non-existing item
		else
			vend_list[i] = j;

		z += ((double)vsd->vending[j].value * (double)amount);
		if( z > (double)sd->status.zeny || z < 0. || z > (double)MAX_ZENY ) {
			clif_buyvending(sd, idx, amount, 1); // you don't have enough zeny
			return;
		}
		if( z + (double)vsd->status.zeny > (double)MAX_ZENY && !battle_config.vending_over_max ) {
			clif_buyvending(sd, idx, vsd->vending[j].amount, 4); // too much zeny = overflow
			return;

		}
		w += itemdb_weight(vsd->cart.u.items_cart[idx].nameid) * amount;
		if( w + sd->weight > sd->max_weight ) {
			clif_buyvending(sd, idx, amount, 2); // you can not buy, because overweight
			return;
		}

		//Check to see if cart/vend info is in sync.
		if( vending[j].amount > vsd->cart.u.items_cart[idx].amount )
			vending[j].amount = vsd->cart.u.items_cart[idx].amount;

		// if they try to add packets (example: get twice or more 2 apples if marchand has only 3 apples).
		// here, we check cumulative amounts
		if( vending[j].amount < amount ) {
			// send more quantity is not a hack (an other player can have buy items just before)
			clif_buyvending(sd, idx, vsd->vending[j].amount, 4); // not enough quantity
			return;
		}

		vending[j].amount -= amount;

		switch( pc_checkadditem(sd, vsd->cart.u.items_cart[idx].nameid, amount) ) {
		case CHKADDITEM_EXIST:
			break;	//We'd add this item to the existing one (in buyers inventory)
		case CHKADDITEM_NEW:
			new_++;
			if (new_ > blank)
				return; //Buyer has no space in his inventory
			break;
		case CHKADDITEM_OVERAMOUNT:
			return; //too many items
		}
	}

	pc_payzeny(sd, (int)z, LOG_TYPE_VENDING, vsd);
	achievement_update_objective(sd, AG_SPEND_ZENY, 1, (int)z);
	z = vending_calc_tax(sd, z);
	pc_getzeny(vsd, (int)z, LOG_TYPE_VENDING, sd);

	for( i = 0; i < count; i++ ) {
		short amount = *(uint16*)(data + 4*i + 0);
		short idx    = *(uint16*)(data + 4*i + 2);
		idx -= 2;
		z = 0.; // zeny counter

		// vending item
		pc_additem(sd, &vsd->cart.u.items_cart[idx], amount, LOG_TYPE_VENDING);
		vsd->vending[vend_list[i]].amount -= amount;
		z += ((double)vsd->vending[vend_list[i]].value * (double)amount);

		if( vsd->vending[vend_list[i]].amount ) {
			if( Sql_Query( mmysql_handle, "UPDATE `%s` SET `amount` = %d WHERE `vending_id` = %d and `cartinventory_id` = %d", vending_items_table, vsd->vending[vend_list[i]].amount, vsd->vender_id, vsd->cart.u.items_cart[idx].id ) != SQL_SUCCESS ) {
				Sql_ShowDebug( mmysql_handle );
			}
		} else {
			if( Sql_Query( mmysql_handle, "DELETE FROM `%s` WHERE `vending_id` = %d and `cartinventory_id` = %d", vending_items_table, vsd->vender_id, vsd->cart.u.items_cart[idx].id ) != SQL_SUCCESS ) {
				Sql_ShowDebug( mmysql_handle );
			}
		}

		pc_cart_delitem(vsd, idx, amount, 0, LOG_TYPE_VENDING);
		z = vending_calc_tax(sd, z);
		clif_vendingreport(vsd, idx, amount, sd->status.char_id, (int)z);

		//print buyer's name
		if( battle_config.buyer_name ) {
			char temp[256];
			sprintf(temp, msg_txt(sd,265), sd->status.name);
			clif_messagecolor(&vsd->bl, color_table[COLOR_LIGHT_GREEN], temp, false, SELF);
		}
	}

	// compact the vending list
	for( i = 0, cursor = 0; i < vsd->vend_num; i++ ) {
		if( vsd->vending[i].amount == 0 )
			continue;

		if( cursor != i ) { // speedup
			vsd->vending[cursor].index = vsd->vending[i].index;
			vsd->vending[cursor].amount = vsd->vending[i].amount;
			vsd->vending[cursor].value = vsd->vending[i].value;
		}

		cursor++;
	}

	vsd->vend_num = cursor;

	//Always save BOTH: customer (buyer) and vender
	if( save_settings&CHARSAVE_VENDING ) {
		chrif_save(sd, CSAVE_INVENTORY|CSAVE_CART);
		chrif_save(vsd, CSAVE_INVENTORY|CSAVE_CART);
	}

	//check for @AUTOTRADE users [durf]
	if( vsd->state.autotrade ) {
		//see if there is anything left in the shop
		ARR_FIND( 0, vsd->vend_num, i, vsd->vending[i].amount > 0 );
		if( i == vsd->vend_num ) {
			//Close Vending (this was automatically done by the client, we have to do it manually for autovenders) [Skotlex]
			vending_closevending(vsd);
			map_quit(vsd);	//They have no reason to stay around anymore, do they?
		}
	}
}

/**
 * Player setup a new shop
 * @param sd : player opening the shop
 * @param message : shop title
 * @param data : itemlist data
 *	data := {<index>.w <amount>.w <value>.l}[count]
 * @param count : number of different items
 * @param at Autotrader info, or NULL if requetsed not from autotrade persistance
 * @return 0 If success, 1 - Cannot open (die, not state.prevend, trading), 2 - No cart, 3 - Count issue, 4 - Cart data isn't saved yet, 5 - No valid item found
 */
int8 vending_openvending(struct map_session_data* sd, const char* message, const uint8* data, int count, struct s_autotrader *at)
{
	int i, j;
	int vending_skill_lvl;
	char message_sql[MESSAGE_SIZE*2];
	StringBuf buf;
	
	nullpo_retr(false,sd);

	if ( pc_isdead(sd) || !sd->state.prevend || pc_istrading(sd)) {
		return 1; // can't open vendings lying dead || didn't use via the skill (wpe/hack) || can't have 2 shops at once
	}

	vending_skill_lvl = pc_checkskill(sd, MC_VENDING);
	
	// skill level and cart check
	if( !vending_skill_lvl || !pc_iscarton(sd) ) {
		clif_skill_fail(sd, MC_VENDING, USESKILL_FAIL_LEVEL, 0);
		return 2;
	}

	// check number of items in shop
	if( count < 1 || count > MAX_VENDING || count > 2 + vending_skill_lvl ) { // invalid item count
		clif_skill_fail(sd, MC_VENDING, USESKILL_FAIL_LEVEL, 0);
		return 3;
	}

	if (save_settings&CHARSAVE_VENDING) // Avoid invalid data from saving
		chrif_save(sd, CSAVE_INVENTORY|CSAVE_CART);

	// filter out invalid items
	i = 0;
	for( j = 0; j < count; j++ ) {
		short index        = *(uint16*)(data + 8*j + 0);
		short amount       = *(uint16*)(data + 8*j + 2);
		unsigned int value = *(uint32*)(data + 8*j + 4);

		index -= 2; // offset adjustment (client says that the first cart position is 2)

		if( index < 0 || index >= MAX_CART // invalid position
		||  pc_cartitem_amount(sd, index, amount) < 0 // invalid item or insufficient quantity
		//NOTE: official server does not do any of the following checks!
		||  !sd->cart.u.items_cart[index].identify // unidentified item
		||  sd->cart.u.items_cart[index].attribute == 1 // broken item
		||  sd->cart.u.items_cart[index].expire_time // It should not be in the cart but just in case
		||  (sd->cart.u.items_cart[index].bound && !pc_can_give_bounded_items(sd)) // can't trade account bound items and has no permission
		||  !itemdb_cantrade(&sd->cart.u.items_cart[index], pc_get_group_level(sd), pc_get_group_level(sd)) ) // untradeable item
			continue;

		sd->vending[i].index = index;
		sd->vending[i].amount = amount;
		sd->vending[i].value = min(value, (unsigned int)battle_config.vending_max_value);
		i++; // item successfully added
	}

	if (i != j) {
		clif_displaymessage(sd->fd, msg_txt(sd, 266)); //"Some of your items cannot be vended and were removed from the shop."
		clif_skill_fail(sd, MC_VENDING, USESKILL_FAIL_LEVEL, 0); // custom reply packet
		return 5;
	}

	if( i == 0 ) { // no valid item found
		clif_skill_fail(sd, MC_VENDING, USESKILL_FAIL_LEVEL, 0); // custom reply packet
		return 5;
	}

	sd->state.prevend = 0;
	sd->state.vending = true;
	sd->state.workinprogress = WIP_DISABLE_NONE;
	sd->vender_id = vending_getuid();
	sd->vend_num = i;
	safestrncpy(sd->message, message, MESSAGE_SIZE);
	
	Sql_EscapeString( mmysql_handle, message_sql, sd->message );

	if( Sql_Query( mmysql_handle, "INSERT INTO `%s`(`id`, `account_id`, `char_id`, `sex`, `map`, `x`, `y`, `title`, `autotrade`, `body_direction`, `head_direction`, `sit`) "
		"VALUES( %d, %d, %d, '%c', '%s', %d, %d, '%s', %d, '%d', '%d', '%d' );",
		vendings_table, sd->vender_id, sd->status.account_id, sd->status.char_id, sd->status.sex == SEX_FEMALE ? 'F' : 'M', map_getmapdata(sd->bl.m)->name, sd->bl.x, sd->bl.y, message_sql, sd->state.autotrade, at ? at->dir : sd->ud.dir, at ? at->head_dir : sd->head_dir, at ? at->sit : pc_issit(sd) ) != SQL_SUCCESS ) {
		Sql_ShowDebug(mmysql_handle);
	}

	StringBuf_Init(&buf);
	StringBuf_Printf(&buf, "INSERT INTO `%s`(`vending_id`,`index`,`cartinventory_id`,`amount`,`price`) VALUES", vending_items_table);
	for (j = 0; j < i; j++) {
		StringBuf_Printf(&buf, "(%d,%d,%d,%d,%d)", sd->vender_id, j, sd->cart.u.items_cart[sd->vending[j].index].id, sd->vending[j].amount, sd->vending[j].value);
		if (j < i-1)
			StringBuf_AppendStr(&buf, ",");
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, StringBuf_Value(&buf)))
		Sql_ShowDebug(mmysql_handle);
	StringBuf_Destroy(&buf);

	clif_openvending(sd,sd->bl.id,sd->vending);
	clif_showvendingboard(&sd->bl,message,0);

	idb_put(vending_db, sd->status.char_id, sd);

	return 0;
}

/**
 * Checks if an item is being sold in given player's vending.
 * @param sd : vender session (player)
 * @param nameid : item id
 * @return 0:not selling it, 1: yes
 */
bool vending_search(struct map_session_data* sd, t_itemid nameid)
{
	int i;

	if( !sd->state.vending ) { // not vending
		return false;
	}

	ARR_FIND( 0, sd->vend_num, i, sd->cart.u.items_cart[sd->vending[i].index].nameid == nameid );
	if( i == sd->vend_num ) { // not found
		return false;
	}

	return true;
}

/**
 * Searches for all items in a vending, that match given ids, price and possible cards.
 * @param sd : The vender session to search into
 * @param s : parameter of the search (see s_search_store_search)
 * @return Whether or not the search should be continued.
 */
bool vending_searchall(struct map_session_data* sd, const struct s_search_store_search* s)
{
	int i, c, slot;
	unsigned int idx, cidx;
	struct item* it;

	if( !sd->state.vending ) // not vending
		return true;

	for( idx = 0; idx < s->item_count; idx++ ) {
		ARR_FIND( 0, sd->vend_num, i, sd->cart.u.items_cart[sd->vending[i].index].nameid == s->itemlist[idx].itemId );
		if( i == sd->vend_num ) { // not found
			continue;
		}
		it = &sd->cart.u.items_cart[sd->vending[i].index];

		if( s->min_price && s->min_price > sd->vending[i].value ) { // too low price
			continue;
		}

		if( s->max_price && s->max_price < sd->vending[i].value ) { // too high price
			continue;
		}

		if( s->card_count ) { // check cards
			if( itemdb_isspecial(it->card[0]) ) { // something, that is not a carded
				continue;
			}
			slot = itemdb_slots(it->nameid);

			for( c = 0; c < slot && it->card[c]; c ++ ) {
				ARR_FIND( 0, s->card_count, cidx, s->cardlist[cidx].itemId == it->card[c] );
				if( cidx != s->card_count ) { // found
					break;
				}
			}

			if( c == slot || !it->card[c] ) { // no card match
				continue;
			}
		}

		// Check if the result set is full
		if( s->search_sd->searchstore.items.size() >= (unsigned int)battle_config.searchstore_maxresults ){
			return false;
		}

		std::shared_ptr<s_search_store_info_item> ssitem = std::make_shared<s_search_store_info_item>();

		ssitem->store_id = sd->vender_id;
		ssitem->account_id = sd->status.account_id;
		safestrncpy( ssitem->store_name, sd->message, sizeof( ssitem->store_name ) );
		ssitem->nameid = it->nameid;
		ssitem->amount = sd->vending[i].amount;
		ssitem->price = sd->vending[i].value;
		for( int j = 0; j < MAX_SLOTS; j++ ){
			ssitem->card[j] = it->card[j];
		}
		ssitem->refine = it->refine;
		ssitem->enchantgrade = it->enchantgrade;

		s->search_sd->searchstore.items.push_back( ssitem );
	}

	return true;
}

/**
* Open vending for Autotrader
* @param sd Player as autotrader
*/
void vending_reopen( struct map_session_data* sd )
{
	struct s_autotrader *at = NULL;
	int8 fail = -1;

	nullpo_retv(sd);

	// Open vending for this autotrader
	if ((at = (struct s_autotrader *)uidb_get(vending_autotrader_db, sd->status.char_id)) && at->count && at->entries) {
		uint8 *data, *p;
		uint16 j, count;

		// Init vending data for autotrader
		CREATE(data, uint8, at->count * 8);

		for (j = 0, p = data, count = at->count; j < at->count; j++) {
			struct s_autotrade_entry *entry = at->entries[j];
			uint16 *index = (uint16*)(p + 0);
			uint16 *amount = (uint16*)(p + 2);
			uint32 *value = (uint32*)(p + 4);

			// Find item position in cart
			ARR_FIND(0, MAX_CART, entry->index, sd->cart.u.items_cart[entry->index].id == entry->cartinventory_id);

			if (entry->index == MAX_CART) {
				count--;
				continue;
			}

			*index = entry->index + 2;
			*amount = itemdb_isstackable(sd->cart.u.items_cart[entry->index].nameid) ? entry->amount : 1;
			*value = entry->price;

			p += 8;
		}

		sd->state.prevend = 1; // Set him into a hacked prevend state
		sd->state.autotrade = 1;

		// Make sure abort all NPCs
		npc_event_dequeue(sd);
		pc_cleareventtimer(sd);

		// Open the vending again
		if( (fail = vending_openvending(sd, at->title, data, count, at)) == 0 ) {
			// Make vendor look perfect
			pc_setdir(sd, at->dir, at->head_dir);
			clif_changed_dir(&sd->bl, AREA_WOS);
			if( at->sit ) {
				pc_setsit(sd);
				skill_sit(sd, 1);
				clif_sitting(&sd->bl);
			}

			// Immediate save
			chrif_save(sd, CSAVE_AUTOTRADE);

			ShowInfo("Vending loaded for '" CL_WHITE "%s" CL_RESET "' with '" CL_WHITE "%d" CL_RESET "' items at " CL_WHITE "%s (%d,%d)" CL_RESET "\n",
				sd->status.name, count, mapindex_id2name(sd->mapindex), sd->bl.x, sd->bl.y);
		}
		aFree(data);
	}

	if (at) {
		vending_autotrader_remove(at, true);
		if (db_size(vending_autotrader_db) == 0)
			vending_autotrader_db->clear(vending_autotrader_db, vending_autotrader_free);
	}

	if (fail != 0) {
		ShowError("vending_reopen: (Error:%d) Load failed for autotrader '" CL_WHITE "%s" CL_RESET "' (CID=%d/AID=%d)\n", fail, sd->status.name, sd->status.char_id, sd->status.account_id);
		map_quit(sd);
	}
}

/**
* Initializing autotraders from table
*/
void do_init_vending_autotrade(void)
{
	if (battle_config.feature_autotrade) {
		if (Sql_Query(mmysql_handle,
			"SELECT `id`, `account_id`, `char_id`, `sex`, `title`, `body_direction`, `head_direction`, `sit` "
			"FROM `%s` "
			"WHERE `autotrade` = 1 AND (SELECT COUNT(`vending_id`) FROM `%s` WHERE `vending_id` = `id`) > 0 "
			"ORDER BY `id`;",
			vendings_table, vending_items_table ) != SQL_SUCCESS )
		{
			Sql_ShowDebug(mmysql_handle);
			return;
		}

		if( Sql_NumRows(mmysql_handle) > 0 ) {
			uint16 items = 0;
			DBIterator *iter = NULL;
			struct s_autotrader *at = NULL;

			// Init each autotrader data
			while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
				size_t len;
				char* data;

				at = NULL;
				CREATE(at, struct s_autotrader, 1);
				Sql_GetData(mmysql_handle, 0, &data, NULL); at->id = atoi(data);
				Sql_GetData(mmysql_handle, 1, &data, NULL); at->account_id = atoi(data);
				Sql_GetData(mmysql_handle, 2, &data, NULL); at->char_id = atoi(data);
				Sql_GetData(mmysql_handle, 3, &data, NULL); at->sex = (data[0] == 'F') ? SEX_FEMALE : SEX_MALE;
				Sql_GetData(mmysql_handle, 4, &data, &len); safestrncpy(at->title, data, zmin(len + 1, MESSAGE_SIZE));
				Sql_GetData(mmysql_handle, 5, &data, NULL); at->dir = atoi(data);
				Sql_GetData(mmysql_handle, 6, &data, NULL); at->head_dir = atoi(data);
				Sql_GetData(mmysql_handle, 7, &data, NULL); at->sit = atoi(data);
				at->count = 0;

				if (battle_config.feature_autotrade_direction >= 0)
					at->dir = battle_config.feature_autotrade_direction;
				if (battle_config.feature_autotrade_head_direction >= 0)
					at->head_dir = battle_config.feature_autotrade_head_direction;
				if (battle_config.feature_autotrade_sit >= 0)
					at->sit = battle_config.feature_autotrade_sit;

				// initialize player
				CREATE(at->sd, struct map_session_data, 1);
				pc_setnewpc(at->sd, at->account_id, at->char_id, 0, gettick(), at->sex, 0);
				at->sd->state.autotrade = 1|2;
				if (battle_config.autotrade_monsterignore)
					at->sd->state.block_action |= PCBLOCK_IMMUNE;
				else
					at->sd->state.block_action &= ~PCBLOCK_IMMUNE;
				chrif_authreq(at->sd, true);
				uidb_put(vending_autotrader_db, at->char_id, at);
			}
			Sql_FreeResult(mmysql_handle);

			// Init items for each autotraders
			iter = db_iterator(vending_autotrader_db);
			for (at = (struct s_autotrader *)dbi_first(iter); dbi_exists(iter); at = (struct s_autotrader *)dbi_next(iter)) {
				uint16 j = 0;

				if (SQL_ERROR == Sql_Query(mmysql_handle,
					"SELECT `cartinventory_id`, `amount`, `price` "
					"FROM `%s` "
					"WHERE `vending_id` = %d "
					"ORDER BY `index` ASC;",
					vending_items_table, at->id ) )
				{
					Sql_ShowDebug(mmysql_handle);
					continue;
				}

				if (!(at->count = (uint16)Sql_NumRows(mmysql_handle))) {
					map_quit(at->sd);
					vending_autotrader_remove(at, true);
					continue;
				}

				//Init the list
				CREATE(at->entries, struct s_autotrade_entry *, at->count);

				//Add the item into list
				j = 0;
				while (SQL_SUCCESS == Sql_NextRow(mmysql_handle) && j < at->count) {
					char *data;
					CREATE(at->entries[j], struct s_autotrade_entry, 1);
					Sql_GetData(mmysql_handle, 0, &data, NULL); at->entries[j]->cartinventory_id = atoi(data);
					Sql_GetData(mmysql_handle, 1, &data, NULL); at->entries[j]->amount = atoi(data);
					Sql_GetData(mmysql_handle, 2, &data, NULL); at->entries[j]->price = atoi(data);
					j++;
				}
				items += j;
				Sql_FreeResult(mmysql_handle);
			}
			dbi_destroy(iter);

			ShowStatus("Done loading '" CL_WHITE "%d" CL_RESET "' vending autotraders with '" CL_WHITE "%d" CL_RESET "' items.\n", db_size(vending_autotrader_db), items);
		}
	}

	// Everything is loaded fine, their entries will be reinserted once they are loaded
	if (Sql_Query( mmysql_handle, "DELETE FROM `%s` where `assistant` = 0;", vendings_table ) != SQL_SUCCESS ||
		Sql_Query( mmysql_handle, "DELETE FROM `%s`;", vending_items_table ) != SQL_SUCCESS) {
		Sql_ShowDebug(mmysql_handle);
	}
}

/**
 * Remove an autotrader's data
 * @param at Autotrader
 * @param remove If true will removes from vending_autotrader_db
 **/
static void vending_autotrader_remove(struct s_autotrader *at, bool remove) {
	nullpo_retv(at);
	if (at->count && at->entries) {
		uint16 i = 0;
		for (i = 0; i < at->count; i++) {
			if (at->entries[i])
				aFree(at->entries[i]);
		}
		aFree(at->entries);
	}
	if (remove)
		uidb_remove(vending_autotrader_db, at->char_id);
	aFree(at);
}

/**
* Clear all autotraders
* @author [Cydh]
*/
static int vending_autotrader_free(DBKey key, DBData *data, va_list ap) {
	struct s_autotrader *at = (struct s_autotrader *)db_data2ptr(data);
	if (at)
		vending_autotrader_remove(at, false);
	return 0;
}

int8 vending_open_assistant(struct map_session_data* sd, const char* title, int count, int16 x, int16 y, const struct PACKET_CZ_OPEN_ASSISTANT_STORE_sub* entries)
{
	nullpo_retr(1, sd);
	nullpo_retr(1, title);
	nullpo_retr(1, entries);

	if (pc_isdead(sd) || sd->state.trading) {
		return 1;
	}
	if (sd->vend_skill_lv == 0) {
		return 2;
	}

	if (count < 1 || count > MAX_ASSISTANT_VENDING || count > sd->vend_skill_lv + 2) {
		return 3;
	}

	if (assistant_char_lookup.find(sd->status.char_id) != assistant_char_lookup.end()) {
		return 4; // Already have active vending assistant
	}

	if (save_settings & CHARSAVE_VENDING) // Avoid invalid data from saving
		chrif_save(sd, CSAVE_INVENTORY | CSAVE_CART);


	auto ad = std::make_shared<assistant_data>();
	s_vending itemsToVend[MAX_ASSISTANT_VENDING];
	item vendingItems[MAX_ASSISTANT_VENDING];
	// filter out invalid items
	int i = 0, j = 0;
	for (; j < count; j++) {
		short index = entries[j].index;
		short amount = entries[j].amount;
		int value = entries[j].value;

		index -= 2; // offset adjustment (client says that the first inventory position is 2)

		if (index < 0 || index >= MAX_INVENTORY // invalid position
			|| sd->inventory.u.items_inventory[index].nameid == 0 || sd->inventory_data[index] == nullptr // Invalid item
			|| sd->inventory.u.items_inventory[index].amount <= 0 || sd->inventory.u.items_inventory[index].amount < amount // invalid item or insufficient quantity
			//NOTE: official server does not do any of the following checks!
			|| !sd->inventory.u.items_inventory[index].identify // unidentified item
			|| sd->inventory.u.items_inventory[index].attribute == 1 // broken item
			|| sd->inventory.u.items_inventory[index].expire_time // rentals should not be tradable
			|| (sd->inventory.u.items_inventory[index].bound && !pc_can_give_bounded_items(sd)) // can't trade account bound items and has no permission
			|| !itemdb_cantrade(&sd->inventory.u.items_inventory[index], pc_get_group_level(sd), pc_get_group_level(sd))) // untradeable item
			continue;

		itemsToVend[i].index = index;
		itemsToVend[i].amount = amount;
		itemsToVend[i].value = min(value, (unsigned int)battle_config.vending_max_value);
		i++;
	}

	if (i != j) {
		clif_displaymessage(sd->fd, msg_txt(sd, 266)); //"Some of your items cannot be vended and were removed from the shop."
		return 5;
	}
	if (i == 0) { // no valid item found
		return 5;
	}

	for (int k = 0; k < i; ++k) {
		vendingItems[k] = sd->inventory.u.items_inventory[itemsToVend[k].index];
		pc_delitem(sd, itemsToVend[k].index, itemsToVend[k].amount, 0, 9, LOG_TYPE_VENDING);
		ad->items[k].item = vendingItems[k];
		ad->items[k].index = k;
		ad->items[k].amount = itemsToVend[k].amount;
		ad->items[k].price = itemsToVend[k].value;
	}

	sd->state.prevend = 0;
	sd->state.workinprogress = WIP_DISABLE_NONE;
	sd->state.using_vending_assistant = false;

	ad->bl.id = npc_get_new_npc_id();
	ad->bl.m = sd->bl.m;
	ad->bl.x = x;
	ad->bl.y = y;
	ad->bl.type = BL_ASSISTANT;
	ad->vender_id = vending_getuid();
	ad->vend_num = i;
	ad->expire = time(NULL) + sd->assistant_duration;
	sd->assistant_duration = 0;
	memcpy(&ad->vd, &sd->vd, sizeof(view_data));
	ad->owner_id = sd->status.char_id;
	ad->owner_aid = sd->status.account_id;
	safestrncpy(ad->name, sd->status.name, NAME_LENGTH);
	safestrncpy(ad->message, title, MESSAGE_SIZE);

	assistant_char_lookup[ad->owner_id] = ad;
	gid_assistant_lookup[ad->bl.id] = ad;
	ad->initialize();

	return 0;
}

void assistant_data::initialize()
{
	char message_sql[MESSAGE_SIZE * 2];
	char name_sql[NAME_LENGTH * 2];
	StringBuf buf;
	Sql_EscapeString(mmysql_handle, message_sql, this->message);
	Sql_EscapeString(mmysql_handle, name_sql, this->name);

	if (Sql_Query(mmysql_handle, "INSERT INTO `%s`(`id`, `account_id`, `char_id`, `sex`, `map`, `x`, `y`, `title`, `autotrade`, `assistant`,"
		"`job`, `hair_style`, `hair_color`, `weapon`, `shield`, `head_top`, `head_mid`, `head_bottom`, `robe`, `cloth_color`, `name`, `expire`, `bodystyle`) "
		"VALUES( %d, %d, %d, '%c', '%s', %d, %d, '%s', %d, '%d', '%d', '%d', %d, %d, %d, %d, %d, %d, %d, %d, '%s', FROM_UNIXTIME(%" PRtf "), '%d');",
		vendings_table, this->vender_id, this->owner_aid, this->owner_id, this->vd.sex == SEX_FEMALE ? 'F' : 'M', map_getmapdata(this->bl.m)->name, this->bl.x, this->bl.y, message_sql, 0, 1,
		this->vd.class_, this->vd.hair_style, this->vd.hair_color, this->vd.weapon, this->vd.shield, this->vd.head_top, this->vd.head_mid, this->vd.head_bottom, this->vd.robe, this->vd.cloth_color, name_sql, this->expire, this->vd.body_style) != SQL_SUCCESS) {
		Sql_ShowDebug(mmysql_handle);
	}

	StringBuf_Init(&buf);
	StringBuf_Printf(&buf, "INSERT INTO `%s` (`vending_id`,`index`,`amount`,`price`, `nameid`, `refine`, `card0`, `card1`, `card2`, `card3`,"
		" `option_id0`, `option_val0`, `option_parm0`, `option_id1`, `option_val1`, `option_parm1`, `option_id2`, `option_val2`, `option_parm2`, `option_id3`, `option_val3`, `option_parm3`, `option_id4`, `option_val4`, `option_parm4`,"
		" `unique_id`, `enchantgrade`) VALUES ", vending_assistant_items_table);
	for (int j = 0; j < this->vend_num; j++) {
		auto* itm = &this->items[j].item;
		StringBuf_Printf(&buf, "(%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%llu,%d)", this->vender_id, j, this->items[j].amount, this->items[j].price, itm->nameid, itm->refine, itm->card[0], itm->card[1], itm->card[2], itm->card[3],
			itm->option[0].id, itm->option[0].value, itm->option[0].param, itm->option[1].id, itm->option[1].value, itm->option[1].param, itm->option[2].id, itm->option[2].value, itm->option[2].param, itm->option[3].id, itm->option[3].value, itm->option[3].param,
			itm->option[4].id, itm->option[4].value, itm->option[4].param, itm->unique_id, itm->enchantgrade);
		if (j < this->vend_num - 1)
			StringBuf_AppendStr(&buf, ",");
	}
	if (SQL_ERROR == Sql_QueryStr(mmysql_handle, StringBuf_Value(&buf)))
		Sql_ShowDebug(mmysql_handle);
	StringBuf_Destroy(&buf);

	if (this->expire > 0) {
		t_tick expire_tick = (this->expire - time(NULL)) * 1000U;
		this->expire_timer = add_timer(gettick() + expire_tick, vending_assistant_expire, this->bl.id, 0);
		//this->expire_timer = INVALID_TIMER;
	}
	else {
		this->expire_timer = INVALID_TIMER;
	}
	map_addiddb(&this->bl);
	map_addblock(&this->bl);
	clif_spawn(&this->bl);
	// idb_put(vending_db, sd->status.char_id, sd);
}

void assistant_data::close()
{
	if (this->vend_num > 0) { // Return unsold items
		mail_message msg{};
		char sender_name[NAME_LENGTH] = "<MSG>2943</MSG>";
		char mail_title[MAIL_TITLE_LENGTH] = "<MSG>2939</MSG>";
		auto* dest_name = map_charid2nick(this->owner_id);
		time_t ts;
		time(&ts);
		StringBuf buf;


		memcpy(msg.send_name, sender_name, NAME_LENGTH);
		msg.dest_id = this->owner_id;
		if (dest_name != nullptr) {
			memcpy(msg.dest_name, dest_name, NAME_LENGTH);
		}
		memcpy(msg.title, mail_title, MAIL_TITLE_LENGTH);
		msg.type = MAIL_INBOX_NORMAL;
		msg.timestamp = ts;
		msg.scheduled_deletion = ts + (24 * 60 * 60 * 14);
		msg.zeny = 0;

		tm* local_time = localtime(&ts);
		char time_buf[30];
		strftime(time_buf, 30, "%Y-%m-%d %H:%M:%S", local_time);
		StringBuf_Init(&buf);
		StringBuf_Printf(&buf, "<MSG>2946</MSG> %s\r\n<MSG>2942</MSG>\r\n", time_buf);
		for (int i = 0; i < this->vend_num && i < MAIL_MAX_ITEM; ++i) {
			memcpy(&msg.item[i], &this->items[i].item, sizeof(item));
			msg.item[i].amount = this->items[i].amount;
			StringBuf_Printf(&buf, "\r\n<MSG>2944</MSG> %s\r\n<MSG>2945</MSG>%d", itemdb_name(msg.item[i].nameid), msg.item[i].amount); // TODO: item link
		}
		memcpy(msg.body, StringBuf_Value(&buf), min(StringBuf_Length(&buf), MAIL_BODY_LENGTH));

		intif_Mail_send(0, &msg);
	}

	if (Sql_Query(mmysql_handle, "DELETE FROM `%s` WHERE `vending_id` = %d", vending_assistant_items_table, this->vender_id) != SQL_SUCCESS) {
		Sql_ShowDebug(mmysql_handle);
	}
	if (Sql_Query(mmysql_handle, "DELETE FROM `%s` WHERE `id` = %d", vendings_table, this->vender_id) != SQL_SUCCESS) {
		Sql_ShowDebug(mmysql_handle);
	}
	assistant_char_lookup.erase(this->owner_id);
	gid_assistant_lookup.erase(this->bl.id);
}

assistant_data::~assistant_data()
{
	if (this->expire_timer != INVALID_TIMER) {
		delete_timer(this->expire_timer, vending_assistant_expire);
		this->expire_timer = INVALID_TIMER;
	}
	map_foreachinallrange(clif_outsight, &this->bl, AREA_SIZE, BL_PC, &this->bl);
	map_delblock(&this->bl);
	map_deliddb(&this->bl);
}

std::shared_ptr<assistant_data> vending_get_assistant(map_session_data* sd)
{
	if (assistant_char_lookup.find(sd->status.char_id) != assistant_char_lookup.end()) {
		return assistant_char_lookup[sd->status.char_id];
	}
	else {
		return nullptr;
	}
}

std::shared_ptr<assistant_data> vending_gid2ad(uint32 gid)
{
	if (gid_assistant_lookup.find(gid) != assistant_char_lookup.end()) {
		return gid_assistant_lookup[gid];
	}
	else {
		return nullptr;
	}
}

void vending_purchase_from_assistant(map_session_data* sd, int gid, int uid, const uint8* data, int count)
{
	int i, cursor;
	double z;
	s_assistant_vending_item vending[MAX_ASSISTANT_VENDING]; // against duplicate packets
	auto ad = vending_gid2ad(gid);

	nullpo_retv(sd);

	if (ad == nullptr)
		return; // invalid shop

	if (ad->vender_id != uid) { // shop has changed
		clif_buyvending(sd, 0, 0, 6);  // store information was incorrect
		return;
	}

	//if (!searchstore_queryremote(sd, aid) && (sd->bl.m != vsd->bl.m || !check_distance_bl(&sd->bl, &vsd->bl, AREA_SIZE)))
	//	return; // shop too far away

	//searchstore_clearremote(sd);

	if (count < 1 || count > MAX_ASSISTANT_VENDING || count > ad->vend_num)
		return; // invalid amount of purchased items

	// duplicate item in vending to check hacker with multiple packets
	memcpy(&vending, &ad->items, sizeof(ad->items)); // copy vending list

	// some checks
	z = 0.; // zeny counter
	// Mail stuffs
	mail_message msg{};
	mail_message buyer_msg{};
	char sender_name[NAME_LENGTH] = "<MSG>2943</MSG>";
	char mail_title[MAIL_TITLE_LENGTH] = "<MSG>2938</MSG>";
	StringBuf buf; // mail body buffer
	StringBuf buyer_buf;
	time_t ts;
	time(&ts);
	tm* local_time = localtime(&ts);
	char time_buf[30];
	strftime(time_buf, 30, "%Y-%m-%d %H:%M:%S", local_time);
	StringBuf_Init(&buf);
	StringBuf_Init(&buyer_buf);
	StringBuf_Printf(&buf, "<MSG>2932</MSG> %s\r\n\r\n", time_buf);

	for (i = 0; i < count; i++) {
		short amount = *(uint16*)(data + 4 * i + 0);
		short idx = *(uint16*)(data + 4 * i + 2);
		idx -= 1;

		if (amount <= 0)
			return;

		// check of item index in the assistant's inventory
		if (idx < 0 || idx >= ad->vend_num)
			return;

		z += ((double)ad->items[idx].price * (double)amount);
		if (z > (double)sd->status.zeny || z < 0. || z >(double)MAX_ZENY) {
			clif_buyvending(sd, idx, amount, 1); // you don't have enough zeny
			return;
		}
		if (z > (double)MAX_ZENY && !battle_config.vending_over_max) {
			clif_buyvending(sd, idx, ad->items[idx].amount, 4); // too much zeny = overflow
			return;

		}

		//Check to see if cart/vend info is in sync.
		if (vending[idx].amount > ad->items[idx].amount)
			vending[idx].amount = ad->items[idx].amount;

		// if they try to add packets (example: get twice or more 2 apples if marchand has only 3 apples).
		// here, we check cumulative amounts
		if (vending[idx].amount < amount) {
			// send more quantity is not a hack (an other player can have buy items just before)
			clif_buyvending(sd, idx, ad->items[idx].amount, 4); // not enough quantity
			return;
		}

		vending[idx].amount -= amount;

		StringBuf_Printf(&buf, "\r\n<MSG>2933</MSG> %s\r\n<MSG>2935</MSG> %d", itemdb_name(ad->items[idx].item.nameid), (int)vending_calc_tax(nullptr, ad->items[idx].price)); // TODO: item link
	}

	map_session_data* fake_tsd = (map_session_data*)aMalloc(sizeof(map_session_data)); // THIS just for logging? didnt know i'm this lazy, alloc on heap because stack is large enough
	fake_tsd->status.char_id = ad->owner_id;
	fake_tsd->status.account_id = ad->owner_aid;
	memcpy(fake_tsd->status.name, ad->name, NAME_LENGTH);

	pc_payzeny(sd, (int)z, LOG_TYPE_VENDING, fake_tsd);
	aFree(fake_tsd);
	achievement_update_objective(sd, AG_SPEND_ZENY, 1, (int)z);
	int taxed_zeny = (int)vending_calc_tax(sd, z);

	// Prepare mails
	// Common parts
	memcpy(msg.send_name, sender_name, NAME_LENGTH);
	memcpy(msg.title, mail_title, MAIL_TITLE_LENGTH);
	msg.type = MAIL_INBOX_NORMAL;
	msg.timestamp = ts;
	msg.scheduled_deletion = ts + (24 * 60 * 60 * 14);
	msg.zeny = 0;
	memcpy(&buyer_msg, &msg, sizeof(mail_message));
	// Buyer mail
	buyer_msg.dest_id = sd->status.char_id;
	memcpy(buyer_msg.dest_name, sd->status.name, NAME_LENGTH);
	StringBuf_Append(&buyer_buf, &buf); // Add common text to buyer_buf at this point
	StringBuf_Printf(&buyer_buf, "\r\n<MSG>2936</MSG> %d", (int)z);
	memcpy(buyer_msg.body, StringBuf_Value(&buyer_buf), min(StringBuf_Length(&buyer_buf), MAIL_BODY_LENGTH));

	// Vendor mail
	auto* dest_name = map_charid2nick(ad->owner_id);
	msg.dest_id = ad->owner_id;
	if (dest_name != nullptr) {
		memcpy(msg.dest_name, dest_name, NAME_LENGTH);
	}
	StringBuf_Printf(&buf, "\r\n<MSG>2936</MSG> %d", taxed_zeny);
	msg.zeny = taxed_zeny;
	memcpy(msg.body, StringBuf_Value(&buf), min(StringBuf_Length(&buf), MAIL_BODY_LENGTH));
	intif_Mail_send(0, &msg);

	// Update inventory
	for (i = 0; i < count; i++) {
		short amount = *(uint16*)(data + 4 * i + 0);
		short idx = *(uint16*)(data + 4 * i + 2);
		idx -= 1;
		z = 0.; // zeny counter

		// vending item
		memcpy(&buyer_msg.item[i], &ad->items[idx].item, sizeof(item));
		buyer_msg.item[i].amount = amount;
		ad->items[idx].amount -= amount;

		if (ad->items[idx].amount) {
			if (Sql_Query(mmysql_handle, "UPDATE `%s` SET `amount` = %d WHERE `vending_id` = %d and `index` = %d", vending_assistant_items_table, ad->items[idx].amount, ad->vender_id, idx) != SQL_SUCCESS) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
		else {
			if (Sql_Query(mmysql_handle, "DELETE FROM `%s` WHERE `vending_id` = %d and `index` = %d", vending_assistant_items_table, ad->vender_id, idx) != SQL_SUCCESS) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
	}
	intif_Mail_send(0, &buyer_msg);

	// compact the vending list
	for (i = 0, cursor = 0; i < ad->vend_num; i++) {
		if (ad->items[i].amount == 0)
			continue;

		if (cursor != i) { // speedup
			ad->items[cursor].index = ad->items[i].index;
			ad->items[cursor].amount = ad->items[i].amount;
			ad->items[cursor].price = ad->items[i].price;
			ad->items[cursor].item = ad->items[i].item;
		}

		cursor++;
	}

	ad->vend_num = cursor;
	for (; cursor < MAX_ASSISTANT_VENDING; ++cursor) {
		memset(&ad->items[cursor], 0, sizeof(s_assistant_vending_item));
	}

	//save customer
	if (save_settings & CHARSAVE_VENDING) {
		chrif_save(sd, CSAVE_INVENTORY | CSAVE_CART);
	}

	//see if there is anything left in the shop
	ARR_FIND(0, ad->vend_num, i, ad->items[i].amount > 0);
	if (i == ad->vend_num) {
		ad->close();
	}
}

void do_init_vending_assistant()
{
	if (battle_config.feature_autotrade) {
		if (Sql_Query(mmysql_handle,
			"SELECT `id`, `account_id`, `char_id`, `sex`, `title`, `map`, `x`, `y`, `job`, `hair_style`, `hair_color`, `weapon`, `shield`, `head_top`, `head_mid`, `head_bottom`, `robe`, `cloth_color`, `name`, UNIX_TIMESTAMP(`expire`), `bodystyle` "
			"FROM `%s` "
			"WHERE `assistant` = 1 AND (SELECT SUM(`amount`) FROM `%s` WHERE `vending_id` = `id`) > 0 AND expire > NOW()"
			"ORDER BY `id`;",
			vendings_table, vending_assistant_items_table) != SQL_SUCCESS)
		{
			Sql_ShowDebug(mmysql_handle);
			return;
		}
		std::vector<std::shared_ptr<assistant_data>> assistants;
		int items = 0;

		if (Sql_NumRows(mmysql_handle) > 0) {
			// Init each autotrader data
			while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
				size_t len;
				char* data;
				auto ad = std::make_shared<assistant_data>();

				Sql_GetData(mmysql_handle, 0, &data, NULL); ad->vender_id = atoi(data);
				Sql_GetData(mmysql_handle, 1, &data, NULL); ad->owner_aid = atoi(data);
				Sql_GetData(mmysql_handle, 2, &data, NULL); ad->owner_id = atoi(data);
				Sql_GetData(mmysql_handle, 3, &data, NULL); ad->vd.sex = (data[0] == 'F') ? SEX_FEMALE : SEX_MALE;
				Sql_GetData(mmysql_handle, 4, &data, &len); safestrncpy(ad->message, data, zmin(len + 1, MESSAGE_SIZE));
				Sql_GetData(mmysql_handle, 5, &data, &len); ad->bl.m = map_mapname2mapid(data);
				Sql_GetData(mmysql_handle, 6, &data, NULL); ad->bl.x = atoi(data);
				Sql_GetData(mmysql_handle, 7, &data, NULL); ad->bl.y = atoi(data);
				Sql_GetData(mmysql_handle, 8, &data, NULL); ad->vd.class_ = atoi(data);
				Sql_GetData(mmysql_handle, 9, &data, NULL); ad->vd.hair_style = atoi(data);
				Sql_GetData(mmysql_handle, 10, &data, NULL); ad->vd.hair_color = atoi(data);
				Sql_GetData(mmysql_handle, 11, &data, NULL); ad->vd.weapon = atoi(data);
				Sql_GetData(mmysql_handle, 12, &data, NULL); ad->vd.shield = atoi(data);
				Sql_GetData(mmysql_handle, 13, &data, NULL); ad->vd.head_top = atoi(data);
				Sql_GetData(mmysql_handle, 14, &data, NULL); ad->vd.head_mid = atoi(data);
				Sql_GetData(mmysql_handle, 15, &data, NULL); ad->vd.head_bottom = atoi(data);
				Sql_GetData(mmysql_handle, 16, &data, NULL); ad->vd.robe = atoi(data);
				Sql_GetData(mmysql_handle, 17, &data, NULL); ad->vd.cloth_color = atoi(data);
				Sql_GetData(mmysql_handle, 18, &data, &len); safestrncpy(ad->name, data, zmin(len + 1, NAME_LENGTH));
				Sql_GetData(mmysql_handle, 19, &data, NULL); data ? ad->expire = strtoll(data, nullptr, 10) : 0;
				Sql_GetData(mmysql_handle, 20, &data, NULL); ad->vd.body_style = atoi(data);
				ad->bl.type = BL_ASSISTANT;
				ad->bl.id = npc_get_new_npc_id();
				assistants.push_back(ad);
			}
			Sql_FreeResult(mmysql_handle);

			// Init items for each assistants
			for (auto ad : assistants) {
				uint16 j = 0;

				if (SQL_ERROR == Sql_Query(mmysql_handle,
					"SELECT  `index`,  `amount`,  `price`,  `nameid`,  `refine`,  `card0`,  `card1`,  `card2`,  `card3`,  `option_id0`,  `option_val0`,  `option_parm0`,  `option_id1`,  `option_val1`,  `option_parm1`,"
					" `option_id2`,  `option_val2`,  `option_parm2`,  `option_id3`,  `option_val3`,  `option_parm3`,  `option_id4`,  `option_val4`,  `option_parm4`,  `unique_id`, `enchantgrade` "
					"FROM `%s` "
					"WHERE `vending_id` = %d AND `amount` > 0 "
					"ORDER BY `index` ASC LIMIT " EXPAND_AND_QUOTE(MAX_ASSISTANT_VENDING) ";",
					vending_assistant_items_table, ad->vender_id))
				{
					Sql_ShowDebug(mmysql_handle);
					continue;
				}

				if (!(ad->vend_num = (int)Sql_NumRows(mmysql_handle))) {
					continue;
				}
				//Add the item into list
				j = 0;
				int idx = 0;
				int col;
				while (SQL_SUCCESS == Sql_NextRow(mmysql_handle) && j < ad->vend_num) {
					char* data;
					col = 1;
					ad->items[j].index = j;
					Sql_GetData(mmysql_handle, col++, &data, NULL); ad->items[j].amount = atoi(data);
					Sql_GetData(mmysql_handle, col++, &data, NULL); ad->items[j].price = atoi(data);
					Sql_GetData(mmysql_handle, col++, &data, NULL); ad->items[j].item.nameid = atoi(data);
					Sql_GetData(mmysql_handle, col++, &data, NULL); ad->items[j].item.refine = atoi(data);
					for (int k = 0; k < MAX_SLOTS; ++k) {
						Sql_GetData(mmysql_handle, col++, &data, NULL); ad->items[j].item.card[k] = atoi(data);
					}
					for (int k = 0; k < MAX_ITEM_RDM_OPT; ++k) {
						Sql_GetData(mmysql_handle, col++, &data, NULL); ad->items[j].item.option[k].id = atoi(data);
						Sql_GetData(mmysql_handle, col++, &data, NULL); ad->items[j].item.option[k].value = atoi(data);
						Sql_GetData(mmysql_handle, col++, &data, NULL); ad->items[j].item.option[k].param = atoi(data);
					}
					Sql_GetData(mmysql_handle, col++, &data, NULL); data ? ad->items[j].item.unique_id = strtoull(data, nullptr, 10) : 0;
					Sql_GetData(mmysql_handle, col++, &data, NULL); ad->items[j].item.enchantgrade = (uint8)atoi(data);
					j++;
					items++;
				}
				Sql_FreeResult(mmysql_handle);
				ad->vender_id = vending_getuid(); // Re-acquire vender_id
			}

			// Everything is loaded fine, their entries will be reinserted once they are loaded
			if (Sql_Query(mmysql_handle, "DELETE FROM `%s` where `assistant` = 1;", vendings_table) != SQL_SUCCESS ||
				Sql_Query(mmysql_handle, "DELETE FROM `%s`;", vending_assistant_items_table) != SQL_SUCCESS) {
				Sql_ShowDebug(mmysql_handle);
			}

			// 2nd pass, actually process them
			for (auto ad : assistants) {
				if (battle_config.persistent_assistant) {
					assistant_char_lookup[ad->owner_id] = ad;
					gid_assistant_lookup[ad->bl.id] = ad;
					ad->initialize();
				}
				else {
					ad->close();
				}
			}


			ShowStatus("Done loading '" CL_WHITE "%d" CL_RESET "' vending assistants with '" CL_WHITE "%d" CL_RESET "' items.\n", assistants.size(), items);
		}
	}
}

TIMER_FUNC(vending_assistant_expire)
{
	auto ad = gid_assistant_lookup[id];
	if (ad != nullptr) {
		ad->expire_timer = INVALID_TIMER;
		ad->close();
	}

	return 0;
}

/**
 * Initialise the vending module
 * called in map::do_init
 */
void do_final_vending(void)
{
	db_destroy(vending_db);
	vending_autotrader_db->destroy(vending_autotrader_db, vending_autotrader_free);
}

/**
 * Destory the vending module
 * called in map::do_final
 */
void do_init_vending(void)
{
	add_timer_func_list(vending_assistant_expire, "vending_assistant_expire");

	vending_db = idb_alloc(DB_OPT_BASE);
	vending_autotrader_db = uidb_alloc(DB_OPT_BASE);
	if (Sql_Query(mmysql_handle,
		"SELECT COUNT(1) as cnt FROM `%s` ", vendings_table) != SQL_SUCCESS)
	{
		Sql_ShowDebug(mmysql_handle);
	}
	vending_nextid = (int)Sql_NumRows(mmysql_handle);
	do_init_vending_assistant();
}
