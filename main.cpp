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
#include "proxy.h"

static const char
	*bind_addr = "0.0.0.0:1234",
	*rf_addr = "127.0.0.1:27780";
//	*rf_addr = "79.165.127.244:27780";

static void proxy_run(EV_P_ int cli, int srv);

static void dump_pkt(rf_packet_t *pkt)
{
	const char *state = "", *dir;

	if (pkt->drop)
		state = lcc_RED " [DROPPED]";
	if (pkt->dir == SRV_TO_CLI)
		dir = lcc_PURPLE "==>> s2c";
	else
		dir = lcc_CYAN "<<== c2s";

	printf(lcc_YELLOW "\n%s%s" lcc_NORMAL " packet " lcc_GREEN "0x%04x" lcc_NORMAL ", len = %u " lcc_NORMAL "\n",
			dir, state, pkt->type, pkt->len);
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

	ignore_signal(SIGPIPE);

	ev_io_init(&io, cli_connect_cb, sock, EV_READ);
	ev_io_start(loop, &io);
	ev_loop(loop, 0);

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
		if (pkt->show)
			dump_pkt(pkt);
		if (!pkt->drop) {
			p.push_out(pkt);
		} else {
			pkt_unref(pkt);
		}
		s2c.inject(&post);
		c2s.inject(&post);
		/* actually pre & post queues should be empty */
		pqh_clear(&pre);
		pqh_clear(&post);
	}
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
	set_nodelay(cli, 1);
	set_nodelay(srv, 1);
	rf_session *s = new rf_session(cli, srv);
	s->run(EV_A);
}
