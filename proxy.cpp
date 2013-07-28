#include "proxy.h"
#include <sys/uio.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "util.h"

#define EV_IOMASK (EV_READ | EV_WRITE)

/* send queue {{{ */
sendq::sendq(int dir) : svc(0), sv(svbuf), dir(dir)
{
	pqh_init(&pq);
}

/* send any remaining data to file descriptor.
 * return number of items remaining or -1 in case of error. */
int
sendq::send(int fd)
{
	int count = 0;
	if (!svc) {
		rf_packet_t *pkt;
		sv = svbuf;
		for (pkt = pqh_head(&pq); svc < WRIVCNT && pkt; pkt = pqh_next(pkt)) {
			if (pkt->enqueued) {
				printf("================= WARNING: tried to enqueue already queued packet!\n");
				continue;
			}
			sv[svc].iov_base = pkt->data;
			sv[svc].iov_len = pkt->len;
			svc++;
			pkt->enqueued = 1;
		}
	}
	while (svc) {
		int bytes = writev(fd, sv, svc);
		if (bytes > 0) {
			while (svc && bytes >= (int)sv->iov_len) {
				pqh_discard(&pq);
				bytes -= sv->iov_len;
				sv++;
				svc--;
				count++;
			}
			if (bytes) {
				sv->iov_base = (char*)sv->iov_base + bytes;
				sv->iov_len -= bytes;
			}
		} else {
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
			case EWOULDBLOCK:
#endif
				return svc;
			default:
				log_perror("sendq::send");
				return -1;
			}
		}
		if (!svc) {
			rf_packet_t *pkt;
			sv = svbuf;
			for (pkt = pqh_head(&pq); svc < WRIVCNT && pkt; pkt = pqh_next(pkt)) {
				if (pkt->enqueued) {
					printf("***************** WARNING: tried to enqueue already queued packet!\n");
					continue;
				}
				sv[svc].iov_base = pkt->data;
				sv[svc].iov_len = pkt->len;
				svc++;
				pkt->enqueued = 1;
			}
		}
	}
//	printf(lcc_YELLOW "%d" lcc_NORMAL " packets sent\n", count);
	return svc;
}

/* pull packet from queue */
int
sendq::pull(pqhead_t *src)
{
	if (svc < WRIVCNT) {
		if (sv != svbuf && svc) {
			memmove(svbuf, sv, svc * sizeof(*sv));
			sv = svbuf;
		}
		svc += pqh_pull(src, &pq, dir, svbuf + svc, WRIVCNT - svc, 0);
	}
	return svc;
}
/* }}} */


/* recv queue  {{{ */
recvq::recvq(int dir) : rd(NULL), rd_pos(0), dir(dir)
{
	pqh_init(&pq);
}

/* return value:
 * true - more reads are possible
 * false - eof or error occured, reads from this fd no longer possible.
 */
bool
recvq::recv(int fd)
{
	uint8_t			buf[8192];
	struct iovec	rv[2];
	int bytes;

	while (1) {
		if (rd) {
			rv[0].iov_base = rd->data + rd_pos;
			rv[0].iov_len = rd->len - rd_pos;
		} else {
			rv[0].iov_base = rd_buf + rd_pos;
			rv[0].iov_len = sizeof(rd_buf) - rd_pos;
		}
		rv[1].iov_base = buf;
		rv[1].iov_len = sizeof(buf);

		bytes = readv(fd, rv, 2);
		if (bytes > 0) {
			if (bytes >= (int)rv[0].iov_len) {
				uint8_t *p = buf;
				if (rd) {
					/* packet was fully read */
					pqh_push(&pq, rd);
					rd = NULL;
					rd_pos = 0;
				} else {
					rd = pkt_new(GET_INT16(rd_buf), GET_INT16(rd_buf + 2), dir);
					if (!rd) {
						log_perror("recvq::recv");
						return false;
					}
					rd_pos = sizeof(rd_buf);
					if (rd_pos == rd->len) {
						pqh_push(&pq, rd);
						rd = NULL;
						rd_pos = 0;
					}
				}
				bytes -= rv[0].iov_len;
				/* extract full packets */
				while (bytes) {
					int c;
					if (rd) {
						c = bytes < (int)(rd->len - rd_pos)
							? bytes
							: rd->len - rd_pos;
						memcpy(rd->data + rd_pos, p, c);
					} else if (bytes /*+ rd_pos*/ >= (int)sizeof(rd_buf)) { /* rd_pos is always zero at this point*/
						rd = pkt_new(GET_INT16(p), GET_INT16(p + 2), dir);
						if (!rd) {
							log_perror("recvq::recv");
							return false;
						}
						c = sizeof(rd_buf);
					} else {
						/* there are fewer bytes that minimum packet size */
						memcpy(rd_buf + rd_pos, p, bytes);
						rd_pos += bytes;
						break;
					}
					p += c;
					bytes -= c;
					rd_pos += c;
					if (rd_pos == rd->len) {
						pqh_push(&pq, rd);
						rd = NULL;
						rd_pos = 0;
					}
				}
			} else {
				/* we still need more bytes to read to complete part, so just move pointer */
				rd_pos += bytes;
			}
		} else if (bytes == 0) {
			return false;
		} else if (bytes == -1) {
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
			case EWOULDBLOCK:
#endif
				return true;
			default:
				log_perror("recvq::recv");
				return false;
			}
		}
	}
	return true;
}
/* }}} */

bool
proxy_pipe::done()
{
/*	printf("*** done\n  sndq is %sbroken" lcc_NORMAL " and %sempty" lcc_NORMAL "\n",
			state & PIPE_BROKEN ? lcc_RED : lcc_GREEN "not ",
			sndq.is_empty() ? lcc_YELLOW : lcc_GREEN "not ");
			*/
	if (state & PIPE_BROKEN)
		return true;
/*	printf("  rcvq is %s" lcc_NORMAL " and %sempty" lcc_NORMAL "\n",
			state & PIPE_EOF ? lcc_RED "EOF" : lcc_GREEN "ok",
			rcvq.is_empty() ? lcc_YELLOW : lcc_GREEN "not ");
			*/
	if ((state & PIPE_EOF) && rcvq.is_empty() && sndq.is_empty())
		return true;
	return false;
}

/* read packets from pipe::in to rcvq */
int
proxy_pipe::handle_read_event(EV_P)
{
	if (!rcvq.recv(in.fd)) {
		state |= PIPE_EOF;
		ev_io_stop(EV_A_ &in);
	}

	return 0;
}

/* send packets from sndq to pipe::out */
int
proxy_pipe::handle_write_event(EV_P)
{
	int result = sndq.send(out.fd);
	if (result == -1) {
		state |= PIPE_BROKEN;
		shutdown(in.fd, SHUT_RD);
	}

	if (result <= 0) {
		ev_io_stop(EV_A_ &out);
		if ((state & PIPE_EOF) && rcvq.is_empty())
			shutdown(out.fd, SHUT_WR);
	} else {
		ev_io_start(EV_A_ &out);
	}

	return 0;
}

