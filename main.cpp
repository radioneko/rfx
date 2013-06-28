#include "util.h"
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <ev.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "cconsole.h"
#include "pktq.h"

static const char
	*bind_addr = "0.0.0.0:1234",
	*rf_addr = "127.0.0.1:27780";
//	*rf_addr = "79.165.127.244:27780";

void dump_pkt(rf_packet_t *pkt)
{
	printf(lcc_YELLOW "***" lcc_NORMAL " packet " lcc_GREEN "0x%04x" lcc_NORMAL ", len = %u " lcc_YELLOW "***" lcc_NORMAL "\n",
			pkt->type, pkt->len);
	hexdump(stdout, 0, pkt->data, pkt->len);
}

static unsigned rf_conn_timeout = 15;

static addr_t rf_sa;

void log_perror(const char *what, ...)
{
	va_list ap;
	int err = errno;
	va_start(ap, what);
	fwrite(lcc_RED, sizeof(lcc_RED) - 1, 1, stderr);
	vfprintf(stderr, what, ap);
	va_end(ap);
	fprintf(stderr, ":%s %s\n", lcc_NORMAL, strerror(err));
	errno = err;
}

static void proxy_run(EV_P_ int cli, int srv);

struct bootstrap {
	ev_io		io;				/**< */
	ev_timer	timeout;		/**< */
	int			cli;			/**< connected client */
	int			srv;			/**< connection to server being established */


	static void io_cb(EV_P_ ev_io *io, int revents);
	static void tmr_cb(EV_P_ ev_timer *tmr, int revents);

	bootstrap(int cli_fd, int srv_fd) : cli(cli_fd), srv(srv_fd) {
		ev_io_init(&io, io_cb, srv, EV_WRITE);
		ev_timer_init(&timeout, tmr_cb, rf_conn_timeout, 0.0);
		io.data = timeout.data = this;
	}

};

void bootstrap::io_cb(EV_P_ ev_io *io, int revents)
{
	int err = -1;
	socklen_t l = sizeof(err);
	bootstrap *b = (bootstrap*)io->data;
	ev_io_stop(EV_A_ &b->io);
	ev_timer_stop(EV_A_ &b->timeout);
	if (getsockopt (io->fd, SOL_SOCKET, SO_ERROR, &err, &l) != 0) {
		log_perror("rf_connect");
		goto failure;
	}
	if (err != 0) {
		errno = err;
		log_perror("rf_connect");
		goto failure;
	}
	proxy_run(EV_A_ b->cli, b->srv);
	delete b;
	return;
failure:
	close(b->cli);
	close(b->srv);
	delete b;
}

void bootstrap::tmr_cb(EV_P_ ev_timer *tmr, int revents)
{
	bootstrap *b = (bootstrap*)tmr->data;
	if (ev_is_pending(&b->io))
		return;
	errno = ETIMEDOUT;
	ev_io_stop(EV_A_ &b->io);
	ev_timer_stop(EV_A_ &b->timeout);
	close(b->cli);
	close(b->srv);
	log_perror("rf_connect");
	delete b;
}


static void
cli_connect_cb(EV_P_ ev_io *io, int revents)
{
	int fd;

	while ((fd = accept(io->fd, NULL, 0)) != -1) {
		/* establish */
		int srv = socket(rf_sa.name.sa_family, SOCK_STREAM, 0);
		if (srv == -1) {
			log_perror("socket");
			close(fd);
			continue;
		}
		if (set_nonblock(srv, 1) == -1) {
			log_perror("set_nonblock");
			close(fd);
			close(srv);
			continue;
		}
		if (connect(srv, &rf_sa.name, rf_sa.namelen) == -1) {
			if (errno == EINPROGRESS) {
				bootstrap *b = new bootstrap(fd, srv);
				ev_io_start(EV_A_ &b->io);
				ev_timer_start(EV_A_ &b->timeout);
			} else {
				log_perror("rf_connect");
				close(fd);
				close(srv);
			}
		} else {
			/* connected immediately - wow! */
			proxy_run(EV_A_ fd, srv);
		}
	}
}

static int make_listen_socket(addr_t *sa)
{
	int sock;
	int yes = 1;

	free(sa->a_addr);
	sock = socket(sa->name.sa_family, SOCK_STREAM, 0);
	if (sock == -1)
		perror_fatal("socket");

	if (set_nonblock(sock, 1) == -1)
		perror_fatal("set_nonblock");

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	if (bind(sock, &sa->name, sa->namelen) == -1)
		perror_fatal("bind");

	if (listen(sock, 100) == -1)
		perror_fatal("listen");

	return sock;
}

int main(int argc, char **argv)
{
	int sock;
	addr_t bind_sa;
	ev_io io;
	struct ev_loop *loop = ev_default_loop(0);

	if (init_addr(&bind_sa, bind_addr) != 0)
		perror_fatal("Can't parse bind addr: `%s'", bind_addr);
	if (init_addr(&rf_sa, rf_addr) != 0)
		perror_fatal("Can't resolve rf addr: `%s'", rf_addr);


	sock = make_listen_socket(&bind_sa);

	ev_io_init(&io, cli_connect_cb, sock, EV_READ);
	ev_io_start(loop, &io);
	ev_run(loop, 0);

	return 0;
}

class proxy_pipe;

/* send queue {{{ */
class sendq {
	friend class proxy_pipe;
	pqhead_t		pq;
	unsigned		svc;
	struct iovec	*sv;
	struct iovec	svbuf[WRIVCNT];
	int				dir;

public:
	sendq(int dir);
	~sendq() { pqh_clear(&pq); }

	int send(int fd);
	int pull(pqhead_t *src);
	bool is_full() const { return svc == WRIVCNT; }
	bool is_empty() const {return pqh_empty(&pq); }
};

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
			sv[svc].iov_base = pkt->data;
			sv[svc].iov_len = pkt->len;
			svc++;
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
				sv[svc].iov_base = pkt->data;
				sv[svc].iov_len = pkt->len;
				svc++;
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
		svc += pqh_pull(src, &pq, dir, svbuf + svc, WRIVCNT - svc);
	}
	return svc;
}
/* }}} */


/* recv queue  {{{ */
class recvq {
	friend class proxy_pipe;
	pqhead_t		pq;
	rf_packet_t		*rd;		/**< packet being read */
	uint8_t			rd_buf[4];	/**< minimal read buffer */
	unsigned		rd_pos;		/**< position in rd_buf or packet body */
	int				dir;
public:
	recvq(int dir);
	~recvq() { pqh_clear(&pq); }

	bool recv(int fd);
	bool is_empty() const {return pqh_empty(&pq); }
};

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
	uint8_t			buf[192];
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

#define	PIPE_EOF	1			/* eof on read side of the pipe */
#define	PIPE_BROKEN	2			/* error occured on the write side of the pipe */

typedef void (*handler)(EV_P_ ev_io *io, int revents);

struct proxy_pipe {
	int			dir;
	ev_io		in;
	ev_io		out;
	int			state;
	sendq		sndq;			/* queue of packets to send */
	recvq		rcvq;			/* queue of packets to filter */

	proxy_pipe(int fd_in, int fd_out, int dir, handler h, void *arg) : dir(dir), state(0), sndq(dir), rcvq(dir) {
		ev_io_init(&in, h, fd_in, EV_READ);
		ev_io_init(&out, h, fd_out, EV_WRITE);
		in.data = out.data = arg;
	}

	int handle_read_event(EV_P);
	int handle_write_event(EV_P);
	void run(EV_P) { ev_io_start(EV_A_ &in); }
	void stop(EV_P) {ev_io_stop(EV_A_ &in); ev_io_stop(EV_A_ &out); }
	rf_packet_t *pop_in() { return pqh_pop(&rcvq.pq); }
	void push_out(rf_packet_t *p) {
//		printf("BEFORE: %u\n", sndq.svc);
		pqh_push(&sndq.pq, p);
//		printf("AFTER: %u (head = %p)\n", sndq.svc, pqh_head(&sndq.pq));
	}
	void inject(pqhead_t *pqh) { sndq.pull(pqh); }
	bool done();
	bool running() { return !sndq.is_empty(); }
};

#define EV_IOMASK (EV_READ | EV_WRITE)

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
	printf("handle_read_event: state = %d\n", state);

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
	printf("handle_write_event: state = %d\n", state);

	return 0;
}

class rf_session {
	proxy_pipe		c2s;
	proxy_pipe		s2c;
	static void cli_io_cb(EV_P_ ev_io *io, int revents);
	static void srv_io_cb(EV_P_ ev_io *io, int revents);
public:

	rf_session(int cli, int srv);
	~rf_session() { close(c2s.in.fd); close(c2s.out.fd); }

	bool done(); // { return ((c2s.state & PIPE_EOF) && (s2c.state & PIPE_EOF)) || (c2s.state & PIPE_BROKEN) || (s2c.state & PIPE_BROKEN); }

	void run(EV_P) { c2s.run(EV_A); s2c.run(EV_A); }
	void stop(EV_P) { c2s.stop(EV_A); s2c.stop(EV_A); }
	void filter(int dir);
};

bool rf_session::done()
{
	return c2s.done() && s2c.done();
}


/* Handle read/write event on client descriptor */
void rf_session::cli_io_cb(EV_P_ ev_io *io, int revents)
{
	rf_session *r = (rf_session*)io->data;
	if (revents & EV_READ) {
		r->c2s.handle_read_event(EV_A);
		r->filter(CLI_TO_SRV);
		r->c2s.handle_write_event(EV_A);
		r->s2c.handle_write_event(EV_A);
	}
	if (revents & EV_WRITE) {
		r->c2s.handle_write_event(EV_A);
	}
	
	if (r->done()) {
		r->stop(EV_A);
		delete r;
	}
}

/* Handle read/write event on server descriptor */
void rf_session::srv_io_cb(EV_P_ ev_io *io, int revents)
{
	rf_session *r = (rf_session*)io->data;
	if (revents & EV_READ) {
		r->s2c.handle_read_event(EV_A);
		r->filter(SRV_TO_CLI);
		r->s2c.handle_write_event(EV_A);
		r->c2s.handle_write_event(EV_A);
	}
	if (revents & EV_WRITE) {
		r->s2c.handle_write_event(EV_A);
	}

	if (r->done()) {
		r->stop(EV_A);
		delete r;
	}
}

void
rf_session::filter(int dir)
{
	rf_packet_t *pkt;
	proxy_pipe &p = dir == SRV_TO_CLI ? s2c : c2s;

	while ((pkt = p.pop_in())) {
		pqhead_t pre, post;
		pqh_init(&pre);
		pqh_init(&post);

		s2c.inject(&pre);
		c2s.inject(&pre);
//		dump_pkt(pkt);
		p.push_out(pkt);
		s2c.inject(&post);
		c2s.inject(&post);
	}
//	printf(lcc_CYAN "%d" lcc_NORMAL " packets transferred\n", count);
}

rf_session::rf_session(int cli, int srv)
		: c2s(cli, srv, CLI_TO_SRV, cli_io_cb, this),
		  s2c(srv, cli, SRV_TO_CLI, srv_io_cb, this)
{
}

static void
proxy_run(EV_P_ int cli, int srv)
{
	set_nonblock(cli, 1);
	set_nonblock(srv, 1);
	rf_session *s = new rf_session(cli, srv);
	s->run(EV_A);
}
