#pragma once
#include "queue.h"
#include "pktq.h"
#include <stddef.h>

class evqhead_t;

struct rfx_event {
	friend class evqhead_t;
private:
	TAILQ_ENTRY(rfx_event)		link;
	rf_packet_t					*source;
public:
	int							what;
	rfx_event(int what, rf_packet_t *source) : source(source), what(what) { if (source) pkt_ref(source); }
	virtual ~rfx_event() { if (source) pkt_unref(source); }

	rf_packet_t*				get_source() { return source; }
	void						drop_source() { if (source) source->drop = 1; }
	void						show_source() { if (source) source->show = 1; }
};


class evqhead_t {
	TAILQ_HEAD(, rfx_event)		qh;
public:
	evqhead_t() { TAILQ_INIT(&qh); }
	~evqhead_t();

	void		push_back(rfx_event *e);
	rfx_event	*pop_front();
};

