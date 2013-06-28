#include "rfx_api.h"
#include "rfx_modules.h"

class rfx_chat : public rfx_filter {
public:
	rfx_chat() {}

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

	rfx_state* save_state() { return NULL; }
	bool load_state(rfx_state *) { return true; }
};

int
rfx_chat::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	pkt->show = 1;
	return RFX_DECLINE;
}

int
rfx_chat::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_chat();
}
