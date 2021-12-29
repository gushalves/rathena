// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef	_VENDING_HPP_
#define	_VENDING_HPP_

#include "../common/cbasetypes.hpp"
#include "../common/db.hpp"
#include "../common/mmo.hpp"
#include "map.hpp"
#include "unit.hpp"

struct map_session_data;
struct s_search_store_search;
struct s_autotrader;

struct s_vending {
	short index; /// cart index (return item data)
	short amount; ///amout of the item for vending
	unsigned int value; ///at wich price
};

struct s_assistant_vending_item {
	item item;
	uint16 index;
	uint16 amount;
	uint32 price;
};

struct assistant_data {
	block_list bl;
	view_data vd;
	char name[NAME_LENGTH];
	uint32 owner_id;
	uint32 owner_aid;
	t_tick expire;
	int expire_timer;
	int vender_id;
	int vend_num;
	s_assistant_vending_item items[MAX_ASSISTANT_VENDING];
	char message[MESSAGE_SIZE];

	void initialize();
	void close();
	~assistant_data();
};

DBMap * vending_getdb();
void do_final_vending(void);
void do_init_vending(void);
void do_init_vending_autotrade( void );
 
void vending_reopen( struct map_session_data* sd );
void vending_closevending(struct map_session_data* sd);
int8 vending_openvending(struct map_session_data* sd, const char* message, const uint8* data, int count, struct s_autotrader *at);
void vending_vendinglistreq(struct map_session_data* sd, int id);
void vending_purchasereq(struct map_session_data* sd, int aid, int uid, const uint8* data, int count);
bool vending_search(struct map_session_data* sd, t_itemid nameid);
bool vending_searchall(struct map_session_data* sd, const struct s_search_store_search* s);

// Vending assistant
int8 vending_open_assistant(struct map_session_data* sd, const char* title, int count, int16 x, int16 y, const struct PACKET_CZ_OPEN_ASSISTANT_STORE_sub* entries);
void vending_purchase_from_assistant(struct map_session_data* sd, int gid, int uid, const uint8* data, int count);
std::shared_ptr<assistant_data> vending_get_assistant(map_session_data* sd);
std::shared_ptr<assistant_data> vending_gid2ad(uint32 gid);

#endif /* _VENDING_HPP_ */
