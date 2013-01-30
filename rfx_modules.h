#pragma once
#include "pktq.h"
#include "evq.h"
#include <string>

enum {
	RFXEV_WCHAT_SENT = 1,
	RFXEV_WCHAT_RECV,
	RFXEV_PRIV_SENT,
	RFXEV_PRIV_RECV,
	RFXEV_SEND_REPLY,
	RFXEV_LOOT_APPEAR,
	RFXEV_LOOT_DISAPPEAR,
	RFXEV_LAST
};

#define GET_INT16(p) (((uint8_t*)(p))[0] | (((uint8_t*)(p))[1] << 8))

struct rfx_chat_event : public rfx_event {
	std::string			nick;
	std::string			msg;
	rfx_chat_event(int code, const std::string &nick, const std::string &msg, rf_packet_t *pkt) : rfx_event(code, pkt), nick(nick), msg(msg) {}
};


void dump_pkt(rf_packet_t *pkt);
uint8_t hex2i(char digit);
