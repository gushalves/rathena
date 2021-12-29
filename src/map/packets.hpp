// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef PACKETS_HPP
#define PACKETS_HPP

#pragma warning( push )
#pragma warning( disable : 4200 )

// Required for MESSAGE_SIZE, TALKBOX_MESSAGE_SIZE
#include "map.hpp"

#define MAX_ITEM_OPTIONS MAX_ITEM_RDM_OPT
#define UNAVAILABLE_STRUCT int8 _____unavailable
/* packet size constant for itemlist */
#if MAX_INVENTORY > MAX_STORAGE && MAX_INVENTORY > MAX_CART
	#define MAX_ITEMLIST MAX_INVENTORY
#elif MAX_CART > MAX_INVENTORY && MAX_CART > MAX_STORAGE
	#define MAX_ITEMLIST MAX_CART
#else
	#define MAX_ITEMLIST MAX_STORAGE
#endif
#define MAX_ACHIEVEMENT_DB MAX_ACHIEVEMENT_OBJECTIVES

#define DEFINE_PACKET_HEADER(name, id) const int16 HEADER_##name = id;
#define DEFINE_PACKET_ID(name, id) DEFINE_PACKET_HEADER(name, id)

#include "packets_struct.hpp"

// NetBSD 5 and Solaris don't like pragma pack but accept the packed attribute
#if !defined( sun ) && ( !defined( __NETBSD__ ) || __NetBSD_Version__ >= 600000000 )
	#pragma pack( push, 1 )
#endif

struct PACKET_CZ_REQ_MAKINGARROW{
	int16 packetType;
#if PACKETVER_MAIN_NUM >= 20181121 || PACKETVER_RE_NUM >= 20180704 || PACKETVER_ZERO_NUM >= 20181114
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_CZ_SE_PC_BUY_CASHITEM_LIST_sub{
	uint32 itemId;
	uint32 amount;
	uint16 tab;
} __attribute__((packed));

struct PACKET_CZ_SE_PC_BUY_CASHITEM_LIST{
	int16 packetType;
	int16 packetLength;
	uint16 count;
	uint32 kafraPoints;
	struct PACKET_CZ_SE_PC_BUY_CASHITEM_LIST_sub items[];
} __attribute__((packed));

struct PACKET_CZ_REQ_CASH_BARGAIN_SALE_ITEM_INFO{
	int16 packetType;
	int16 packetLength;
	uint32 AID;
#if PACKETVER_MAIN_NUM >= 20181121 || PACKETVER_RE_NUM >= 20180704 || PACKETVER_ZERO_NUM >= 20181114
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_ACK_CASH_BARGAIN_SALE_ITEM_INFO{
	int16 packetType;
	uint16 result;
#if PACKETVER_MAIN_NUM >= 20181121 || PACKETVER_RE_NUM >= 20180704 || PACKETVER_ZERO_NUM >= 20181114
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint32 price;
} __attribute__((packed));

struct PACKET_CZ_REQ_APPLY_BARGAIN_SALE_ITEM{
	int16 packetType;
	uint32 AID;
#if PACKETVER_MAIN_NUM >= 20181121 || PACKETVER_RE_NUM >= 20180704 || PACKETVER_ZERO_NUM >= 20181114
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint32 amount;
	uint32 startTime;
#if PACKETVER >= 20150520
	uint16 hours;
#else
	uint8 hours;
#endif
} __attribute__((packed));

struct PACKET_CZ_REQ_REMOVE_BARGAIN_SALE_ITEM{
	int16 packetType;
	uint32 AID;
#if PACKETVER_MAIN_NUM >= 20181121 || PACKETVER_RE_NUM >= 20180704 || PACKETVER_ZERO_NUM >= 20181114
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_NOTIFY_BARGAIN_SALE_SELLING{
	int16 packetType;
#if PACKETVER_MAIN_NUM >= 20181121 || PACKETVER_RE_NUM >= 20180704 || PACKETVER_ZERO_NUM >= 20181114
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint32 remainingTime;
} __attribute__((packed));

struct PACKET_ZC_NOTIFY_BARGAIN_SALE_CLOSE{
	int16 packetType;
#if PACKETVER_MAIN_NUM >= 20181121 || PACKETVER_RE_NUM >= 20180704 || PACKETVER_ZERO_NUM >= 20181114
	uint32 itemId;
#else
	uint16 itemId;
#endif
} __attribute__((packed));

struct PACKET_ZC_ACK_COUNT_BARGAIN_SALE_ITEM{
	int16 packetType;
#if PACKETVER_MAIN_NUM >= 20181121 || PACKETVER_RE_NUM >= 20180704 || PACKETVER_ZERO_NUM >= 20181114
	uint32 itemId;
#else
	uint16 itemId;
#endif
	uint32 amount;
} __attribute__((packed));

struct PACKET_ZC_ACK_GUILDSTORAGE_LOG_sub{
	uint32 id;
#if PACKETVER_MAIN_NUM >= 20181121 || PACKETVER_RE_NUM >= 20180704 || PACKETVER_ZERO_NUM >= 20181114
	uint32 itemId;
#else
	uint16 itemId;
#endif
	int32 amount;
	uint8 action;
	int32 refine;
	int64 uniqueId;
	uint8 IsIdentified;
	uint16 itemType;
	struct EQUIPSLOTINFO slot;
	char name[NAME_LENGTH];
	char time[NAME_LENGTH];
	uint8 attribute;
} __attribute__((packed));

struct PACKET_ZC_ACK_GUILDSTORAGE_LOG{
	int16 packetType;
	int16 PacketLength;
	uint16 result;
	uint16 amount;
	struct PACKET_ZC_ACK_GUILDSTORAGE_LOG_sub items[];
} __attribute__((packed));

struct PACKET_CZ_UNCONFIRMED_TSTATUS_UP{
	int16 packetType;
	int16 type;
	int16 amount;
} __attribute__((packed));

struct PACKET_CZ_GUILD_EMBLEM_CHANGE2 {
	int16 packetType;
	uint32 guild_id;
	uint32 version;
} __attribute__((packed));

struct PACKET_ZC_CHANGE_GUILD {
	int16 packetType;
#if PACKETVER < 20190724
	uint32 aid;
	uint32 guild_id;
	uint16 emblem_id;
#else
	uint32 guild_id;
	uint32 emblem_id;
	uint32 unknown;
#endif
} __attribute__((packed));

struct PACKET_ZC_BROADCAST{
	int16 packetType;
	int16 PacketLength;
	char message[];
} __attribute__((packed));

struct PACKET_ZC_BROADCAST2{
	int16 packetType;
	int16 PacketLength;
	uint32 fontColor;
	int16 fontType;
	int16 fontSize;
	int16 fontAlign;
	int16 fontY;
	char message[];
} __attribute__((packed));

struct PACKET_ZC_SPIRITS{
	int16 packetType;
	uint32 GID;
	uint16 amount;
} __attribute__((packed));

struct PACKET_ZC_UNCONFIRMED_SPIRITS3{
	int16 packetType;
	uint32 GID;
	uint16 amount;
} __attribute__((packed));

struct PACKET_ZC_ENTRY_QUEUE_INIT {
	int16 packetType;
} __attribute__((packed));

struct PACKET_CZ_UNCONFIRMED_RODEX_RETURN{
	int16 packetType;
	uint32 msgId;
} __attribute__((packed));

struct PACKET_CZ_REQ_STYLE_CLOSE{
	int16 packetType;
} __attribute__((packed));

#ifdef ENABLE_VENDING_ASSISTANT
struct PACKET_ZC_OPEN_ASSISTANT_STORE {
	int16 PacketType;
	uint16 PacketLength;
	uint8 NumItem;
	uint16 availableIndices[];
} __attribute__((packed));
DEFINE_PACKET_HEADER(ZC_OPEN_ASSISTANT_STORE, 0x0A7E);

/*
struct PACKET_CZ_REQ_OPEN_BUYING_STORE {
	int16 packetType;
	int16 packetLength;
	uint32 zenyLimit;
	uint8 result;
	char storeName[MESSAGE_SIZE];
	struct PACKET_CZ_REQ_OPEN_BUYING_STORE_sub items[];
} __attribute__((packed));
*/
struct PACKET_CZ_OPEN_ASSISTANT_STORE_sub {
	uint16 index;
	uint16 amount;
	uint32 value;
} __attribute__((packed));

struct PACKET_CZ_OPEN_ASSISTANT_STORE {
	int16 PacketType;
	uint16 PacketLength;
	char ShopName[MESSAGE_SIZE];
	int16 xPos;
	int16 yPos;
	struct PACKET_CZ_OPEN_ASSISTANT_STORE_sub entries[];
} __attribute__((packed));
DEFINE_PACKET_HEADER(CZ_OPEN_ASSISTANT_STORE, 0x0A7F);

struct PACKET_CZ_REQ_CLOSE_ASSISTANT_SHOP
{
	int16 PacketType;
	uint32 GID;
} __attribute__((packed));
DEFINE_PACKET_HEADER(CZ_REQ_CLOSE_ASSISTANT_SHOP, 0x0A80);

struct PACKET_ZC_ACK_ASSISTANT_STORE
{
	int16 PacketType;
	uint8 Code;
	uint8 SubCode;
} __attribute__((packed));
DEFINE_PACKET_HEADER(ZC_ACK_ASSITANT_STORE, 0x0A81);

struct PACKET_ZC_ASSISTANT_ENTRY
{
	int16 packetType;
	uint32 GID;
	uint16 job;
	uint16 xPos;
	uint16 yPos;
	uint8 sex;
	uint8 head;
	uint8 headpalette;
	uint32 weapon;
	uint32 shield;
	uint16 accessory;
	uint16 accessory2;
	uint16 accessory3;
	uint16 robe;
	uint32 unknown;
	char name[NAME_LENGTH];
	uint16 bodyPalette;
} __attribute__((packed));
DEFINE_PACKET_HEADER(ZC_ASSISTANT_ENTRY, 0xA89);

struct PACKET_ZC_ASSISTANT_VANISH
{
	int16 packetType;
	uint32 GID;
} __attribute__((packed));
DEFINE_PACKET_HEADER(ZC_ASSISTANT_VANISH, 0xA8A);

struct PACKET_CZ_CANCEL_REQ_OPEN_STORE
{
	int16 packetType;
} __attribute__((packed));
DEFINE_PACKET_HEADER(CZ_CANCEL_REQ_OPEN_STORE, 0x0A8C);


struct PACKET_ZC_ASSISTANT_STORE_ITEMLIST {
	int16 PacketType;
	uint16 PacketLength;
	uint32 GID;
	uint32 MarketId;
	uint8 MyShop;
	uint32 TimeLeftMs;
	PACKET_ZC_PC_PURCHASE_ITEMLIST_FROMMC_sub soldItems[];
} __attribute__((packed));
DEFINE_PACKET_HEADER(ZC_ASSISTANT_STORE_ITEMLIST, 0x0A91);


struct PACKET_ZC_ASSISTANT_STORE_ITEMLIST2 {
	int16 PacketType;
	uint16 PacketLength;
	uint32 GID;
	uint32 MarketId;
	uint8 MyShop;
	uint32 TimeLeftMs;
	PACKET_ZC_PC_PURCHASE_ITEMLIST_FROMMC_sub soldItems[];
} __attribute__((packed));
DEFINE_PACKET_HEADER(ZC_ASSISTANT_STORE_ITEMLIST2, 0x0B62);

struct PACKET_ZC_OPEN_ASSISTANT_BUYINGSTORE
{
	int16 PacketType;
	uint8 NumItem;
} __attribute__((packed));
DEFINE_PACKET_HEADER(ZC_OPEN_ASSISTANT_BUYINGSTORE, 0x0A93);

struct PACKET_ZC_ASSISTANT_ENTRY2
{
	int16 packetType;
	uint32 GID;
	uint32 job;
	uint16 xPos;
	uint16 yPos;
	uint8 sex;
	uint8 hair_style;
	uint8 hair_color;
	uint32 weapon;
	uint32 shield;
	uint16 head_top;
	uint16 head_mid;
	uint16 head_bottom;
	uint16 robe;
	uint32 unknown;
	char name[NAME_LENGTH];
	uint16 cloth_color;
} __attribute__((packed));
DEFINE_PACKET_HEADER(ZC_ASSISTANT_ENTRY2, 0xB05);

struct PACKET_CZ_GM_CLOSE_SHOP
{
	int16 PacketType;
	uint32 GID;
} __attribute__((packed));
DEFINE_PACKET_HEADER(CZ_GM_CLOSE_SHOP, 0xAF9);
#endif

// NetBSD 5 and Solaris don't like pragma pack but accept the packed attribute
#if !defined( sun ) && ( !defined( __NETBSD__ ) || __NetBSD_Version__ >= 600000000 )
	#pragma pack( pop )
#endif

DEFINE_PACKET_HEADER(ZC_NOTIFY_CHAT, 0x8d)
DEFINE_PACKET_HEADER(ZC_BROADCAST, 0x9a)
DEFINE_PACKET_HEADER(ZC_ITEM_ENTRY, 0x9d)
DEFINE_PACKET_HEADER(ZC_MVP_GETTING_ITEM, 0x10a)
DEFINE_PACKET_HEADER(ZC_ACK_TOUSESKILL, 0x110)
DEFINE_PACKET_HEADER(CZ_REQMAKINGITEM, 0x18e)
DEFINE_PACKET_HEADER(ZC_ACK_REQMAKINGITEM, 0x18f)
DEFINE_PACKET_HEADER(CZ_REQ_MAKINGARROW, 0x1ae)
DEFINE_PACKET_HEADER(ZC_BROADCAST2, 0x1c3)
DEFINE_PACKET_HEADER(ZC_SPIRITS, 0x1d0)
#if PACKETVER_MAIN_NUM >= 20200916 || PACKETVER_RE_NUM >= 20200724
	DEFINE_PACKET_HEADER(CZ_REQ_ITEMREPAIR, 0xb66)
#else
	DEFINE_PACKET_HEADER(CZ_REQ_ITEMREPAIR, 0x1fd)
#endif
#if PACKETVER >= 20190724
	DEFINE_PACKET_HEADER(ZC_CHANGE_GUILD, 0x0b47)
#else
	DEFINE_PACKET_HEADER(ZC_CHANGE_GUILD, 0x1b4)
#endif
DEFINE_PACKET_HEADER(ZC_NOTIFY_WEAPONITEMLIST, 0x221)
DEFINE_PACKET_HEADER(ZC_ACK_WEAPONREFINE, 0x223)
DEFINE_PACKET_HEADER(CZ_REQ_MAKINGITEM, 0x25b)
DEFINE_PACKET_HEADER(ZC_CASH_TIME_COUNTER, 0x298)
DEFINE_PACKET_HEADER(ZC_CASH_ITEM_DELETE, 0x299)
DEFINE_PACKET_HEADER(ZC_FAILED_TRADE_BUYING_STORE_TO_SELLER, 0x824)
DEFINE_PACKET_HEADER(CZ_SSILIST_ITEM_CLICK, 0x83c)
DEFINE_PACKET_HEADER(ZC_ENTRY_QUEUE_INIT, 0x90e);
DEFINE_PACKET_HEADER(CZ_REQ_CASH_BARGAIN_SALE_ITEM_INFO, 0x9ac)
DEFINE_PACKET_HEADER(ZC_ACK_CASH_BARGAIN_SALE_ITEM_INFO, 0x9ad)
DEFINE_PACKET_HEADER(CZ_REQ_APPLY_BARGAIN_SALE_ITEM, 0x9ae)
DEFINE_PACKET_HEADER(CZ_REQ_REMOVE_BARGAIN_SALE_ITEM, 0x9b0)
DEFINE_PACKET_HEADER(ZC_NOTIFY_BARGAIN_SALE_SELLING, 0x9b2)
DEFINE_PACKET_HEADER(ZC_NOTIFY_BARGAIN_SALE_CLOSE, 0x9b3)
DEFINE_PACKET_HEADER(ZC_ACK_COUNT_BARGAIN_SALE_ITEM, 0x9c4)
DEFINE_PACKET_HEADER(ZC_ACK_GUILDSTORAGE_LOG, 0x9da)
DEFINE_PACKET_HEADER(CZ_NPC_MARKET_PURCHASE, 0x9d6)
DEFINE_PACKET_HEADER(CZ_REQ_APPLY_BARGAIN_SALE_ITEM2, 0xa3d)
DEFINE_PACKET_HEADER(CZ_REQ_STYLE_CHANGE, 0xa46)
DEFINE_PACKET_HEADER(ZC_STYLE_CHANGE_RES, 0xa47)
DEFINE_PACKET_HEADER(CZ_REQ_STYLE_CLOSE, 0xa48)
DEFINE_PACKET_HEADER(CZ_REQ_STYLE_CHANGE2, 0xafc)
DEFINE_PACKET_HEADER(ZC_REMOVE_EFFECT, 0x0b0d)
DEFINE_PACKET_HEADER(CZ_UNCONFIRMED_TSTATUS_UP, 0x0b24)
DEFINE_PACKET_HEADER(CZ_GUILD_EMBLEM_CHANGE2, 0x0b46)
DEFINE_PACKET_HEADER(ZC_UNCONFIRMED_SPIRITS3, 0xb73)
DEFINE_PACKET_HEADER(CZ_UNCONFIRMED_RODEX_RETURN, 0xb98)

const int16 MAX_INVENTORY_ITEM_PACKET_NORMAL = ( ( INT16_MAX - ( sizeof( struct packet_itemlist_normal ) - ( sizeof( struct NORMALITEM_INFO ) * MAX_ITEMLIST) ) ) / sizeof( struct NORMALITEM_INFO ) );
const int16 MAX_INVENTORY_ITEM_PACKET_EQUIP = ( ( INT16_MAX - ( sizeof( struct packet_itemlist_equip ) - ( sizeof( struct EQUIPITEM_INFO ) * MAX_ITEMLIST ) ) ) / sizeof( struct EQUIPITEM_INFO ) );

const int16 MAX_STORAGE_ITEM_PACKET_NORMAL = ( ( INT16_MAX - ( sizeof( struct ZC_STORE_ITEMLIST_NORMAL ) - ( sizeof( struct NORMALITEM_INFO ) * MAX_ITEMLIST) ) ) / sizeof( struct NORMALITEM_INFO ) );
const int16 MAX_STORAGE_ITEM_PACKET_EQUIP = ( ( INT16_MAX - ( sizeof( struct ZC_STORE_ITEMLIST_EQUIP ) - ( sizeof( struct EQUIPITEM_INFO ) * MAX_ITEMLIST ) ) ) / sizeof( struct EQUIPITEM_INFO ) );

const int16 MAX_GUILD_STORAGE_LOG_PACKET = ( ( INT16_MAX - sizeof( struct PACKET_ZC_ACK_GUILDSTORAGE_LOG ) ) / sizeof( struct PACKET_ZC_ACK_GUILDSTORAGE_LOG_sub ) );

#undef MAX_ITEM_OPTIONS
#undef UNAVAILABLE_STRUCT
#undef MAX_ITEMLIST
#undef MAX_ACHIEVEMENT_DB
#undef MAX_PACKET_POS
#undef DEFINE_PACKET_HEADER

#pragma warning( pop )

#endif /* PACKETS_HPP */
