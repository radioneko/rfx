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
#include <dlfcn.h>
#include <string>
#include "rfx_api.h"
#include <vector>
#include <sys/stat.h>

static const char
	*bind_addr = "0.0.0.0:1234",
	*rf_addr = "127.0.0.1:27780";
//	*rf_addr = "79.165.127.244:27780";

static void proxy_run(EV_P_ int cli, int srv);
static void load_plugins(const char *plugin, ...);
static void reload_plugins();

static void dump_pkt(rf_packet_t *pkt)
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
	if (pkt->desc)
		printf(lcc_PURPLE " (%s)", pkt->desc);
	printf(lcc_NORMAL "\n");

	hexdump(stdout, 0, pkt->data, pkt->len);
}

static unsigned rf_conn_timeout = 15;

static addr_t rf_sa;

static char*
now_asc(char *buf, unsigned bufsz)
{
	struct tm t;
	time_t now = time(NULL);
	localtime_r(&now, &t);
	strftime(buf, bufsz, "%H:%M:%S", &t);
	return buf;
}

void log_perror(const char *what, ...)
{
	va_list ap;
	char tb[64];
	int err = errno;
	va_start(ap, what);
	fprintf(stderr, "[%s] ", now_asc(tb, sizeof(tb)));
	fwrite(lcc_RED, sizeof(lcc_RED) - 1, 1, stderr);
	vfprintf(stderr, what, ap);
	va_end(ap);
	fprintf(stderr, ":%s %s\n", lcc_NORMAL, strerror(err));
	errno = err;
}

void log_error(const char *what, ...)
{
	va_list ap;
	char tb[64];
	int err = errno;
	va_start(ap, what);
	fprintf(stderr, "[%s] ", now_asc(tb, sizeof(tb)));
	fwrite(lcc_RED, sizeof(lcc_RED) - 1, 1, stderr);
	vfprintf(stderr, what, ap);
	va_end(ap);
	fprintf(stderr, "\n" lcc_NORMAL);
	errno = err;
}

void log_info(const char *what, ...)
{
	va_list ap;
	char tb[64];
	int err = errno;
	va_start(ap, what);
	fprintf(stderr, "[%s] ", now_asc(tb, sizeof(tb)));
	fwrite(lcc_CYAN, sizeof(lcc_CYAN) - 1, 1, stderr);
	vfprintf(stderr, what, ap);
	va_end(ap);
	fprintf(stderr, "\n" lcc_NORMAL);
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

static void
plugins_check_cb(EV_P_ ev_timer *t, int revents)
{
	ev_timer_stop(EV_A_ t);
	reload_plugins();
	ev_now_update(EV_A);
	t->repeat = 1;
	ev_timer_again(EV_A_ t);
}

int main(int argc, char **argv)
{
	int sock;
	addr_t bind_sa;
	ev_io io;
	struct ev_timer pt;
	struct ev_loop *loop = ev_default_loop(0);

	load_plugins("dl/rfx_chat.so", NULL);
	if (init_addr(&bind_sa, bind_addr) != 0)
		perror_fatal("Can't parse bind addr: `%s'", bind_addr);
	if (init_addr(&rf_sa, rf_addr) != 0)
		perror_fatal("Can't resolve rf addr: `%s'", rf_addr);


	sock = make_listen_socket(&bind_sa);

	ignore_signal(SIGPIPE);

	ev_io_init(&io, cli_connect_cb, sock, EV_READ);
	ev_io_start(loop, &io);
	ev_timer_init(&pt, plugins_check_cb, 1.0, 1.0);
	ev_timer_start(loop, &pt);
	ev_loop(loop, 0);

	return 0;
}

class rfx_module;
class rfx_filter_chain;

/******* rfx_instance - an initialized instance of rfx_filter, linked to filter chain and module *******/
class rfx_instance {
	friend class rfx_module;
	friend class rfx_filter_chain;
	rfx_state					*st;
	rfx_filter					*flt;
	rfx_filter					*flt_new;
	rfx_module					*module;
	rfx_filter_chain			*chain;
	TAILQ_ENTRY(rfx_instance)	ilink;
	TAILQ_ENTRY(rfx_instance)	clink;
public:
	rfx_instance(rfx_filter *f, rfx_module *m) : st(NULL), flt(f), flt_new(NULL), module(m) {}
	~rfx_instance();
};

/***** rfx_module: class that handles plugin loading and instantiation of filters ********/
class rfx_module {
	std::string					soname;
	void						*dlh;
	rfx_filter_proc				filter_new;
	time_t						tstamp;

	TAILQ_HEAD(, rfx_instance)	ih;				/* instance head pointer */
public:
	rfx_module(const char *dlpath) : soname(dlpath), dlh(NULL) { TAILQ_INIT(&ih); }
	~rfx_module();

	bool load();
	void reload();
	bool need_reload();
	rfx_instance	*new_instance();
	void			detach_instance(rfx_instance *rfi) { TAILQ_REMOVE(&ih, rfi, ilink); }
};

/****** rfx_filter_chain: class that links rfx_filters into chains and invokes them for rf_session ******/
class rfx_filter_chain {
	TAILQ_HEAD(, rfx_instance)	ch;
public:
	rfx_filter_chain() { TAILQ_INIT(&ch); }
	~rfx_filter_chain();

	void			detach_instance(rfx_instance *rfi) { TAILQ_REMOVE(&ch, rfi, clink); }
	void			process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	void			process(evqhead_t *evq, pqhead_t *pre, pqhead_t *post);
	void			add_filter(rfx_instance *rfi);
};

/* rfx_instance implementation {{{ */
rfx_instance::~rfx_instance()
{
	module->detach_instance(this);
	chain->detach_instance(this);
	delete st;
	delete flt;
	delete flt_new;
}
/* }}} */

/* rfx_filter_chain implementation {{{ */
rfx_filter_chain::~rfx_filter_chain()
{
	rfx_instance *i, *tmp;
	TAILQ_FOREACH_SAFE(i, &ch, clink, tmp) {
		delete i;
	}
}

void
rfx_filter_chain::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	rfx_instance *i;
	TAILQ_FOREACH(i, &ch, clink) {
		if (i->flt && i->flt->process(pkt, pre, post, evq) == RFX_BREAK)
			break;
	}
}

void
rfx_filter_chain::process(evqhead_t *evq, pqhead_t *pre, pqhead_t *post)
{
	rfx_instance *i;
	rfx_event *ev;
	while ((ev = evq->pop_front())) {
		TAILQ_FOREACH(i, &ch, clink) {
			printf("INVOKE\n");
			if (i->flt && i->flt->process(ev, pre, post, evq) == RFX_BREAK)
				break;
		}
		delete ev;
	}
}

void
rfx_filter_chain::add_filter(rfx_instance *rfi)
{
	rfi->chain = this;
	TAILQ_INSERT_TAIL(&ch, rfi, clink);
}
/* }}} */

/* rfx_module implementation {{{ */
rfx_instance*
rfx_module::new_instance()
{
	rfx_filter *f = filter_new ? filter_new() : NULL;
	rfx_instance *rfi = new rfx_instance(f, this);
	TAILQ_INSERT_TAIL(&ih, rfi, ilink);
	return rfi;
}

/* dlopen, dlsym, etc */
bool
rfx_module::load()
{
	void *h = dlopen(soname.c_str(), RTLD_LOCAL | RTLD_NOW);
	void *sym;

	if (!h) {
		log_error("dlopen('%s') failed: %s", soname.c_str(), dlerror());
		return false;
	}

	sym = dlsym(h, API_SIGNATURE_SYM);
	if (!sym) {
		log_error("'%s' is not an RFX module", soname.c_str());
		goto bad_module;
	}

	if (strcmp((char*)sym, rfx_api_ver) != 0) {
		log_error("%s: API version mismatch\n  core:   %s\n  module: %s", soname.c_str(), rfx_api_ver, (char*)sym);
		goto bad_module;
	}

	sym = dlsym(h, API_FILTER_SYM);
	if (!sym) {
		log_error("%s: symbol `%s' not found", soname.c_str(), API_FILTER_SYM);
		goto bad_module;
	}
	struct stat st;
	if (stat(soname.c_str(), &st) == 0)
		tstamp = st.st_mtime;
	filter_new = (rfx_filter_proc)sym;
	dlh = h;
	return true;
bad_module:
	dlclose(h);
	return false;
}

/* reload module */
void
rfx_module::reload()
{
	rfx_instance *i;

	if (dlh) {
		/* save state and destory previous filters */
		TAILQ_FOREACH(i, &ih, ilink) {
			if (i->flt) {
				i->st = i->flt->save_state();
				delete i->flt;
				i->flt = NULL;
			}
		}
		dlclose(dlh);
		dlh = NULL;
		filter_new = NULL;
	}

	if (!load())
		goto failure;

	/* recreate filters */
	TAILQ_FOREACH(i, &ih, ilink) {
		i->flt = filter_new();
		if (i->st) {
			i->flt->load_state(i->st);
			delete i->st;
			i->st = NULL;
		}
	}

	/* unload previous module instance */
	log_info("module %s reloaded", soname.c_str());

	return;
failure:
	log_error("module %s reload failed", soname.c_str());
}

/* return true if module needs reloading */
bool
rfx_module::need_reload()
{
	struct stat st;
	if (!dlh)
		return true;
	if (stat(soname.c_str(), &st) == 0 && st.st_mtime != tstamp && time(NULL) - st.st_mtime > 1)
		return true;
	return false;
}
/* }}} */

/* something like plugin manager {{{ */
typedef std::vector<rfx_module*> plugin_list;
static plugin_list plugins;

static void
load_plugins(const char *plugin, ...)
{
	va_list ap;
	const char *so;
	va_start(ap, plugin);
	for (so = plugin; so; so = va_arg(ap, const char*)) {
		rfx_module *m = new rfx_module(so);
		m->load();
		plugins.push_back(m);
	}
	va_end(ap);
}

static void
reload_plugins()
{
	for (plugin_list::iterator i = plugins.begin(); i != plugins.end(); ++i)
		if ((*i)->need_reload())
			(*i)->reload();
}

static void
build_chain(rfx_filter_chain &c)
{
	for (plugin_list::iterator i = plugins.begin(); i != plugins.end(); ++i) {
		c.add_filter((*i)->new_instance());
	}
}
/* }}} */


/* rf_session {{{ */
class rf_session {
	proxy_pipe		c2s;
	proxy_pipe		s2c;
	rfx_filter_chain flt;
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
		evqhead_t ev;
		pqh_init(&pre);
		pqh_init(&post);

		/* 1. process packet */
		flt.process(pkt, &pre, &post, &ev);

		/* 2. proces all fired events */
		flt.process(&ev, &pre, &post);

		/* 3. enqueue prepended packets */
		s2c.inject(&pre);
		c2s.inject(&pre);

		/* 4. dump packet if debug info requested */
		if (pkt->show)
			dump_pkt(pkt);

		/* 5. append packet itself */
		if (!pkt->drop) {
			p.push_out(pkt);
		} else {
			pkt_unref(pkt);
		}

		/* 6. enqueue appended packets */
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
	build_chain(flt);
}
/* }}} */

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
