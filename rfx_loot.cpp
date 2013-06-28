#include "rfx_api.h"
#include "rfx_modules.h"

#define	LOOT_DROP_NEW		0x1403
#define	LOOT_DROP_HORIZON	0x0f04

class rfx_loot : public rfx_filter {
public:
	rfx_loot() {}

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

	rfx_state* save_state() { return NULL; }
	bool load_state(rfx_state *) { return true; }
};

int
rfx_loot::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	switch (pkt->type) {
	case LOOT_DROP_NEW:
		pkt->desc = "new item on the ground";
		pkt->show = 1;
		break;
	case LOOT_DROP_HORIZON:
		pkt->desc = "new item at the vision range";
		pkt->show = 1;
		break;
	}
	return RFX_DECLINE;
}

#include <stdio.h>
int
rfx_loot::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	if (ev->what == RFXEV_PRIV_SENT) {
		rfx_chat_event *e = (rfx_chat_event*)ev;
		printf("nick = '%s', msg = '%s'\n", e->nick.c_str(), e->msg.c_str());
	}
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_loot();
}
