#pragma once
#include "pktq.h"
#include <ev.h>

void log_perror(const char *what, ...);

class proxy_pipe;

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

#define	PIPE_EOF	1			/* eof on read side of the pipe */
#define	PIPE_BROKEN	2			/* error occured on the write side of the pipe */

typedef void (*pipe_handler)(EV_P_ ev_io *io, int revents);

struct proxy_pipe {
	int			dir;
	ev_io		in;
	ev_io		out;
	int			state;
	sendq		sndq;			/* queue of packets to send */
	recvq		rcvq;			/* queue of packets to filter */

	proxy_pipe(int fd_in, int fd_out, int dir, pipe_handler h, void *arg) : dir(dir), state(0), sndq(dir), rcvq(dir) {
		ev_io_init(&in, h, fd_in, EV_READ);
		ev_io_init(&out, h, fd_out, EV_WRITE);
		in.data = out.data = arg;
	}

	int			handle_read_event(EV_P);
	int			handle_write_event(EV_P);
	void 		run(EV_P)						{ ev_io_start(EV_A_ &in); }
	void 		stop(EV_P)						{ ev_io_stop(EV_A_ &in); ev_io_stop(EV_A_ &out); }
	rf_packet_t *pop_in()						{ return pqh_pop(&rcvq.pq); }
	void		push_out(rf_packet_t *p)		{ pqh_push(&sndq.pq, p); }
	void		inject(pqhead_t *pqh)			{ sndq.pull(pqh); }
	bool		done();
	bool		running()						{ return !sndq.is_empty(); }
};

