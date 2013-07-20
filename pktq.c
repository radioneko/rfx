#include "pktq.h"
#include "cconsole.h"
#include "util.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* Create packet. length is suposed to include packet header (len & type) */
rf_packet_t*
pkt_new(unsigned len, unsigned type, int dir)
{
	rf_packet_t *pkt = calloc(1, sizeof(*pkt) + len);

	pkt->len = len;
	pkt->type = type;
	pkt->dir = dir;
	pkt->drop = 0;
	pkt->show = 0;
	pkt->refc = 1;
	pkt->desc = NULL;
	pkt->dyn_desc = NULL;
	pkt->delay = 0;

	pkt->data[0] = len & 0xff;
	pkt->data[1] = (len >> 8) & 0xff;
	pkt->data[2] = type & 0xff;
	pkt->data[3] = (type >> 8) & 0xff;

	return pkt;
}

/* Increase ref count */
rf_packet_t*
pkt_ref(rf_packet_t *pkt)
{
	++pkt->refc;
	return pkt;
}

/* Dereference and free if refc == 0 */
void
pkt_unref(rf_packet_t *pkt)
{
	if (!--pkt->refc) {
		if (pkt->dyn_desc)
			free(pkt->dyn_desc);
		free(pkt);
	}
}

char*
pkt_dsprintf(rf_packet_t *pkt, const char *fmt, ...)
{
	va_list ap;
	char *desc = NULL;
	va_start(ap, fmt);
	if (vasprintf(&desc, fmt, ap)) {
		if (pkt->dyn_desc)
			free(pkt->dyn_desc);
		pkt->dyn_desc = desc;
	}
	va_end(ap);
	return desc;
}

/* Pretty print packet contents */
void pkt_dump(rf_packet_t *pkt)
{
	const char *state = "", *dir;

	if (pkt->drop)
		state = lcc_RED " [DROPPED]";
	if (pkt->dir == SRV_TO_CLI)
		dir = lcc_PURPLE "==>> s2c";
	else
		dir = lcc_CYAN "<<== c2s";

	printf(lcc_YELLOW "\n%s%s" lcc_NORMAL " packet " lcc_GREEN "0x%04x" lcc_NORMAL ", len = %u",
			dir, state, pkt->type, pkt->len);
	if (pkt->dyn_desc)
		printf(lcc_PURPLE " (%s)", pkt->dyn_desc);
	else if (pkt->desc)
		printf(lcc_PURPLE " (%s)", pkt->desc);
	printf(lcc_NORMAL "\n");

	hexdump(stdout, 0, pkt->data, pkt->len);
}

void
pqh_init(pqhead_t *pqh)
{
	TAILQ_INIT(pqh);
}

void
pqh_clear(pqhead_t *pqh)
{
	rf_packet_t *pkt, *tmp;
	TAILQ_FOREACH_SAFE(pkt, pqh, link, tmp) {
		pkt_unref(pkt);
	}
}

/* return true if queue is empty */
int
pqh_empty(const pqhead_t *pqh)
{
	return TAILQ_EMPTY(pqh);
}

/* Remove first packet and return it  */
rf_packet_t*
pqh_pop(pqhead_t *pqh)
{
	rf_packet_t *pkt = TAILQ_FIRST(pqh);
	if (pkt)
		TAILQ_REMOVE(pqh, pkt, link);
	return pkt;
}

/* Append packet to the tail */
void
pqh_push(pqhead_t *pqh, rf_packet_t *pkt)
{
	TAILQ_INSERT_TAIL(pqh, pkt, link);
}

/* Pull count packets from queue to dst list and iovec */
unsigned
pqh_pull(pqhead_t *src, pqhead_t *dst, int dir, struct iovec *outv, unsigned outv_sz, unsigned min_delay)
{
	unsigned i = 0;
	rf_packet_t *p, *tmp;
	TAILQ_FOREACH_SAFE(p, src, link, tmp) {
		if (i >= outv_sz)
			break;
		if (p->delay > min_delay)
			continue;
		if (p->dir == dir) {
			TAILQ_REMOVE(src, p, link);
			if (!p->drop) {
				outv[i].iov_base = p->data;
				outv[i].iov_len = p->len;
				i++;
				TAILQ_INSERT_TAIL(dst, p, link);
			} else {
				pkt_unref(p);
			}
		}
	}
	return i;
}

/* Remove and free first packet */
void
pqh_discard(pqhead_t *pqh)
{
	rf_packet_t *pkt = TAILQ_FIRST(pqh);
	if (pkt) {
		TAILQ_REMOVE(pqh, pkt, link);
		pkt_unref(pkt);
	}
}

rf_packet_t*
pqh_head(pqhead_t *pqh)
{
	return TAILQ_FIRST(pqh);
}

rf_packet_t*
pqh_next(rf_packet_t *pkt)
{
	return TAILQ_NEXT(pkt, link);
}
