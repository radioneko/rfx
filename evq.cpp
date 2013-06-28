#include "evq.h"

evqhead_t::~evqhead_t()
{
	rfx_event *e, *tmp;
	TAILQ_FOREACH_SAFE(e, &qh, link, tmp) {
		delete e;
	}
}

void
evqhead_t::push_back(rfx_event *e)
{
	TAILQ_INSERT_TAIL(&qh, e, link);
}

rfx_event*
evqhead_t::pop_front()
{
	rfx_event *e = TAILQ_FIRST(&qh);
	if (e)
		TAILQ_REMOVE(&qh, e, link);

	return e;
}
