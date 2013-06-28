#include "pktq.h"
#include <stdlib.h>
#include <stdio.h>

rf_packet_t*
pkt_new(unsigned len, unsigned type, int dir)
{
	rf_packet_t *pkt = malloc(sizeof(*pkt) + len);

//	printf("pkt_new: type = 0x%04x, len = %u\n", type, len);
	pkt->len = len;
	pkt->type = type;
	pkt->dir = dir;
	pkt->refc = 1;

	pkt->data[0] = len & 0xff;
	pkt->data[1] = (len >> 8) & 0xff;
	pkt->data[2] = type & 0xff;
	pkt->data[3] = (type >> 8) & 0xff;

	return pkt;
}

rf_packet_t*
pkt_ref(rf_packet_t *pkt)
{
	++pkt->refc;
	return pkt;
}

void
pkt_unref(rf_packet_t *pkt)
{
	if (!--pkt->refc)
		free(pkt);
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
pqh_pull(pqhead_t *src, pqhead_t *dst, int dir, struct iovec *outv, unsigned outv_sz)
{
	unsigned i = 0;
	rf_packet_t *p, *tmp;
	TAILQ_FOREACH_SAFE(p, src, link, tmp) {
		if (i >= outv_sz)
			break;
		if (p->dir == dir) {
			outv[i].iov_base = p->data;
			outv[i].iov_len = p->len;
			i++;
			TAILQ_REMOVE(src, p, link);
			TAILQ_INSERT_TAIL(dst, p, link);
		}
	}
	return i;
}

/* Remove and free head */
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
