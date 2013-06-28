#pragma once
#include "pktq.h"
#include "evq.h"
#include <string>

enum {
	RFXEV_WCHAT_SENT,
	RFXEV_WCHAT_RECV,
	RFXEV_PRIV_SENT,
	RFXEV_PRIV_RECV,
	RFXEV_LOOT_APPEAR,
	RFXEV_LOOT_DISAPPEAR,
	RFXEV_LAST
};

struct rfx_chat_event : public rfx_event {
	std::string			nick;
	std::string			msg;
	rfx_chat_event(int code, const std::string &nick, const std::string &msg, rf_packet_t *pkt) : rfx_event(code, pkt), nick(nick), msg(msg) {}
};

