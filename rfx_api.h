#pragma once
#include "pktq.h"
#include "evq.h"

#define		API_SIGNATURE_SYM		"rfx_api_ver"
#define		API_FILTER_ID			rfx_filter_new
#define		API_FILTER_SYM			"rfx_filter_new"

extern "C" const char rfx_api_ver[];

enum {
	RFX_DECLINE,			/* proceed to next filter in chain */
	RFX_BREAK				/* stop chain processing */
};

struct rfx_state {
	int			version;

	virtual ~rfx_state() {}
};

class rfx_filter {
public:
	virtual ~rfx_filter() {}

	virtual int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq) = 0;
	virtual int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq) = 0;
	/* save state */
	virtual rfx_state* save_state() = 0;
	/* restore state. return false if state restoration is unsuccessful */
	virtual bool load_state(rfx_state *) = 0;
};

typedef rfx_filter* (*rfx_filter_proc)();
extern "C" rfx_filter *API_FILTER_ID();
