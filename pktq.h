#pragma once
#include <stdint.h>
#include <sys/uio.h>
#include "include/queue.h"

#define	WRIVCNT	128

enum {
	CLI_TO_SRV = 0,
	SRV_TO_CLI = 1
};

typedef struct rf_packet {
	uint16_t				len;
	uint16_t				type;
	uint8_t					dir:2;
	uint8_t					drop:1;			/* packet should be dropped */
	uint8_t					show:1;			/* packet hex dump will be displayed */
	uint8_t					enqueued:1;		/* packet is being send */
	unsigned				refc;
	unsigned				delay;			/* delay before this packet sending (in milliseconds) */
	const char				*desc;			/* description (for debug purposes) */
	TAILQ_ENTRY(rf_packet)	link;
	uint8_t					data[0];		/* this header also includes length and data fields */
} rf_packet_t;

typedef TAILQ_HEAD(, rf_packet) pqhead_t;

#ifdef __cplusplus
extern "C" {
#endif /* C++ */

rf_packet_t		*pkt_new(unsigned len, unsigned type, int dir);
rf_packet_t		*pkt_ref(rf_packet_t *pkt);
void			pkt_unref(rf_packet_t *pkt);
void			pkt_dump(rf_packet_t *pkt);

void			pqh_init(pqhead_t *pqh);
void			pqh_clear(pqhead_t *pqh);
rf_packet_t		*pqh_head(pqhead_t *pqh);
rf_packet_t		*pqh_next(rf_packet_t *pkt);
int				pqh_empty(const pqhead_t *pqh);
rf_packet_t		*pqh_pop(pqhead_t *pqh);
void			pqh_push(pqhead_t *pqh, rf_packet_t *pkt);
void			pqh_discard(pqhead_t *pqh);
unsigned		pqh_pull(pqhead_t *src, pqhead_t *dst, int dir, struct iovec *outv, unsigned outv_sz, unsigned min_delay);

//int				pq_do_read(pkt_queue_t *pq, int fd, int dir);
//int				pq_do_write(pkt_queue_t *pq, int fd, int dir);
#ifdef __cplusplus
}
#endif /* C++ */
