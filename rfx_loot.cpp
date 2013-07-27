#include "rfx_api.h"
#include "rfx_modules.h"
#include <string.h>
#include <stdio.h>
#include <map>

#define	LOOT_DROP_NEW		0x1403
#define	LOOT_DROP_HORIZON	0x0f04
#define	LOOT_DISAPPEAR		0x1c03

#define	ITEM_PICK			0x0207
#define PICK_RESPONSE		0x0407

#if 0
template<typename T>
struct refptr {
	T	*data;
	unsigned	ref;

	refptr() : data(NULL), ref(1) {}
	refptr(T *data) : data(NULL), ref(1) {}
	~refptr() { unref(); }

	refptr<T>* ref() { ref++; return this; }
	void unref() { if (ref && !(--ref)) { delete data; data = NULL; } }
};
#endif


/* ground drop set {{{ */
struct ground_item {
	unsigned		id;			/* hex id of the item */
	unsigned		ground_id;	/* identifier of the item layin on ground */
	rf_packet_t		*source;	/* packet that generated that item */

	ground_item() : id(0), ground_id(0), source(NULL) {}
	ground_item(unsigned id, unsigned ground_id, rf_packet_t *source) : id(id), ground_id(ground_id), source(source) {
		if (source)
			pkt_ref(source);
	}
	ground_item(const ground_item &gi) : source(NULL) { *this = gi; }
	~ground_item() { if (source) pkt_unref(source); }

	ground_item& operator=(const ground_item &gi) {
		id = gi.id;
		ground_id = gi.ground_id;
		if (gi.source)
			pkt_ref(gi.source);
		if (source)
			pkt_unref(source);
		source = gi.source;
		return *this;
	}
};

typedef std::map<unsigned, ground_item> ground_items;
/* }}} */

static rf_packet_t*
make_pick_request(unsigned ground_id, unsigned inventory_cell)
{
	rf_packet_t *pkt = pkt_new(8, ITEM_PICK, CLI_TO_SRV);
	uint8_t *p = pkt->data + 4;
	*p++ = ground_id & 0xff;
	*p++ = (ground_id >> 8) & 0xff;
	*p++ = inventory_cell & 0xff;
	*p++ = (inventory_cell >> 8) & 0xff;
	pkt->desc = "[INJECT] pick request";
	//pkt_dump(pkt);
	return pkt;
}

struct loot_state {
	loot_mask		show;
	loot_mask		pick;
	unsigned		inv_cell;

	loot_state() : show(true), pick(false), inv_cell(0xffff) {}
	loot_state(const loot_state &s) : show(s.show), pick(s.pick) {}

	bool save(rfx_state *s) { show.save(s); pick.save(s); s->write(&inv_cell, sizeof(inv_cell)); return true; }
	bool restore(rfx_state *s) { return show.restore(s) && pick.restore(s); s->read(&inv_cell, sizeof(inv_cell)); }
};

class rfx_loot : public rfx_filter {
	loot_state		lm;
	ground_items	gi;
	unsigned		last_pick;
public:
	rfx_loot() {}

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

	void save_state(rfx_state *s) { lm.save(s); }
	bool load_state(rfx_state *s) { return lm.restore(s);  }

	std::string loot_cmd(const std::string &msg, loot_mask &m);
};

int
rfx_loot::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	int id = -1;
	switch (pkt->type) {
	case LOOT_DROP_NEW:
		pkt->desc = "new item on the ground";
		id = GET_INT24(pkt->data + 4);
		pkt->show = 1;
		break;
	case LOOT_DROP_HORIZON:
		pkt->desc = "new item at the vision range";
		id = GET_INT24(pkt->data + 4);
//		pkt->show = 1;
		break;
	case LOOT_DISAPPEAR:
		pkt->desc = "loot disappear";
//		pkt->show = 1;
		if (pkt->len >= 6) {
			unsigned ground_id = GET_INT16(pkt->data + 4);
			gi.erase(ground_id);
		}
		break;
	case ITEM_PICK:
		pkt->desc = "item pick request";
		pkt->show = 1;
		break;
	case PICK_RESPONSE:
		pkt->desc = "loot pick response";
		pkt->show = 1;
		if (pkt->len >= 7 && pkt->data[4] == 0 && !gi.empty()) {
			/* successful pickup */
			ground_items::iterator i = gi.begin();
			last_pick = i->first;
			lm.inv_cell = pkt->data[5];
			pqh_push(post, make_pick_request(last_pick, lm.inv_cell));
		} else if (pkt->data[4] == 9 && !gi.empty()) { // rate limit, huh?
			rf_packet_t *pi;
			ground_items::iterator i = gi.begin();
			last_pick = i->first;
			pi = make_pick_request(last_pick, lm.inv_cell);
			pi->delay = 100; /* 100 ms delay */
			pqh_push(post, pi);
			pkt->drop = 1;
		}
#if 0
		{
			uint8_t code = pkt->data[4];
			unsigned ground_id = GET_INT16(pkt->data + 5);
			uint8_t count = 0xff;
			if (pkt->len >= 8)
				count = pkt->data[7];
			printf("pick: %u, 0x%04x, count = %u\n", code, ground_id, count);
		}
#endif
		break;
	}
	if (id != -1) {
		evq->push_back(new rfx_loot_event(id, 0 /* TODO: get count from packet */,
					GET_INT16(pkt->data + 8) /*ground_id */, pkt));
		if (lm.pick.test(id)) {
			unsigned ground_id = GET_INT16(pkt->data + 8);
			last_pick = ground_id;
			gi[ground_id] = ground_item(id, ground_id, pkt);
		}
		pkt->drop = !lm.show.test(id);
	}
	return RFX_DECLINE;
}


std::string
rfx_loot::loot_cmd(const std::string &msg, loot_mask &m)
{
	char reply[32];
	loot_mask nm(m);

	if (msg == "info")
		goto reply;

	if (!nm.parse(msg))
		return "can't parse loot mask";

reply:
	unsigned count = nm.count();
	m = nm;
	snprintf(reply, sizeof(reply), "ok, show = %u, ignore = %u", count, 65536 - count);
	return reply;
}

int
rfx_loot::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	if (ev->what == RFXEV_PRIV_SENT) {
		rfx_chat_event *e = (rfx_chat_event*)ev;
		if (e->nick == "loot") {
			std::string reply = loot_cmd(e->msg, lm.show);
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, e->nick, reply, NULL));
			ev->ignore_source();
			return RFX_BREAK;
		} else if (e->nick == "pick") {
			std::string reply = loot_cmd(e->msg, lm.pick);
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, e->nick, reply, NULL));
			ev->ignore_source();
			return RFX_BREAK;
		}
//	} else if (ev->what == RFXEV_BACKPACK_PICK) {
		/* we were requested to pick an item */
	}
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_loot();
}
