#pragma once
#include <stdint.h>
#include <sys/uio.h>
#include "include/queue.h"

#define	WRIVCNT	64

enum {
	CLI_TO_SRV = 0,
	SRV_TO_CLI = 1
};

typedef struct rf_packet {
	uint16_t				len;
	uint16_t				type;
	uint8_t					dir;
	unsigned				refc;
	TAILQ_ENTRY(rf_packet)	link;
	uint8_t					data[0];		/* this header also includes length and data fields */
} rf_packet_t;

typedef TAILQ_HEAD(, rf_packet) pqhead_t;

typedef struct {
	pqhead_t		pq_head;
	rf_packet_t		*rd;					/* incomplete packet being read */
//	char			rd_cache[4];			/* to keep minimal state between pq_do_read() */
//	unsigned		rd_pos;					/* number of bytes read in rd or rd_cache */
//	rf_packet_t		*wr;					/* next packet to write */
//	struct iovec	*wrv;					/* iov of packet being currently written */
//	unsigned		wrvc;					/* number of items in wrvc */
//	struct iovec	wriv[WRIVCNT];			/* filled iovec structures */
} pkt_queue_t;

#ifdef __cplusplus
extern "C" {
#endif /* C++ */

rf_packet_t		*pkt_new(unsigned len, unsigned type, int dir);
rf_packet_t		*pkt_ref(rf_packet_t *pkt);
void			pkt_unref(rf_packet_t *pkt);

void			pqh_init(pqhead_t *pqh);
void			pqh_clear(pqhead_t *pqh);
rf_packet_t		*pqh_head(pqhead_t *pqh);
rf_packet_t		*pqh_next(rf_packet_t *pkt);
int				pqh_empty(const pqhead_t *pqh);
rf_packet_t		*pqh_pop(pqhead_t *pqh);
void			pqh_push(pqhead_t *pqh, rf_packet_t *pkt);
void			pqh_discard(pqhead_t *pqh);
unsigned		pqh_pull(pqhead_t *src, pqhead_t *dst, int dir, struct iovec *outv, unsigned outv_sz);

//int				pq_do_read(pkt_queue_t *pq, int fd, int dir);
//int				pq_do_write(pkt_queue_t *pq, int fd, int dir);
#ifdef __cplusplus
}
#endif /* C++ */
