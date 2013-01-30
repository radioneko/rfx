#include "rfx_api.h"
#include "rfx_modules.h"

class rfx_debug : public rfx_filter {
public:
	rfx_debug() {}

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

	void save_state(rfx_state *) { }
	bool load_state(rfx_state *) { return true; }
};

int
rfx_debug::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	return RFX_DECLINE;
}

int
rfx_debug::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_debug();
}
