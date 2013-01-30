#pragma once
#include "pktq.h"
#include "evq.h"
#include <stdint.h>
#include <vector>
#include <string.h>

#define		API_SIGNATURE_SYM		"rfx_api_ver"
#define		API_FILTER_ID			rfx_filter_new
#define		API_FILTER_SYM			"rfx_filter_new"

extern "C" const char rfx_api_ver[];

enum {
	RFX_DECLINE,			/* proceed to next filter in chain */
	RFX_BREAK				/* stop chain processing */
};

typedef std::vector<uint8_t> rfx_sstate;

struct rfx_state {
	int			version;

	unsigned	pos;
	rfx_sstate	state; /* serialized state */

	rfx_state(int v) : version(v), pos(0) {}
	unsigned read(void *dst, unsigned len) {
		unsigned l = state.size() - pos < len ? state.size() - pos : len;
		memcpy(dst, &state[0] + pos, l);
		pos += l;
		return l;
	}

	void write(const void *src, unsigned len) {
		unsigned l = state.size();
		state.resize(l + len);
		memcpy(&state[0] + l, src, len);
	}
};

class rfx_filter {
public:
	virtual ~rfx_filter() {}

	virtual int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq) = 0;
	virtual int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq) = 0;
	/* save state */
	virtual void save_state(rfx_state *) = 0;
	/* restore state. return false if state restoration is unsuccessful */
	virtual bool load_state(rfx_state *) = 0;
};

typedef rfx_filter* (*rfx_filter_proc)();
extern "C" rfx_filter *API_FILTER_ID();
