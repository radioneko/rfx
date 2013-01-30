#include "rfx_api.h"
#include "rfx_modules.h"
#include <string.h>
#include <stdio.h>

#define	LOOT_DROP_NEW		0x1403
#define	LOOT_DROP_HORIZON	0x0f04
#define	LOOT_DISAPPEAR		0x1c03

#if 0
template<typename T>
struct refptr {
	T	*data;
	unsigned	ref;

	refptr() : data(NULL), ref(1) {}
	refptr(T *data) : data(NULL), ref(1) {}
	~refptr() { unref(); }

	refptr<T>* ref() { ref++; return this; }
	void unref() { if (ref && !(--ref)) { delete data; data = NULL; } }
};
#endif

/* loot mask {{{ */
struct loot_mask {
	uint8_t			mask[8192];

	loot_mask(bool flag) { set(flag); }
	loot_mask(const loot_mask &m) { memcpy(mask, m.mask, sizeof(mask)); }

	void set(bool flag) { memset(mask, flag ? 0xff : 0, sizeof(mask)); }
	bool set(unsigned idx, bool flag);
	bool test(unsigned idx);
	bool operator()(unsigned idx) { return test(idx); }
	loot_mask & operator +=(const unsigned);
	loot_mask & operator -=(const unsigned);
	loot_mask & operator +=(const loot_mask &m);
	loot_mask & operator -=(const loot_mask &m);
	loot_mask & operator =(const loot_mask &m) { memcpy(mask, m.mask, sizeof(mask)); return *this; }
	rfx_state	*save(rfx_state *s) { s->write(mask, sizeof(mask)); return s; }
	bool		restore(rfx_state *s) { return s->read(mask, sizeof(mask)) == sizeof(mask); }
	unsigned	count();

	bool		fsave(const char *name);
	bool		fload(const char *name);
};

unsigned
loot_mask::count()
{
	unsigned i, s = 0;
	for (i = 0; i < sizeof(mask); i++) {
		for (uint8_t j = 0x80; j; j >>= 1)
			if (j & mask[i])
				s++;
	}
	return s;
}

bool
loot_mask::test(unsigned idx)
{
	unsigned i = idx >> 3;
	if (i < sizeof(mask))
		return (0x80 >> (idx & 7)) & mask[i];
	return false;
}

bool
loot_mask::set(unsigned idx, bool flag)
{
	bool result = false;
	unsigned i = idx >> 3;
	if (i < sizeof(mask)) {
		uint8_t m = 0x80 >> (idx & 7);
		result = mask[i] & m;
		if (flag)
			mask[i] |= m;
		else
			mask[i] &= ~m;
	}
	return result;
}

loot_mask&
loot_mask::operator +=(const loot_mask &m)
{
	unsigned i;
	for (i = 0; i < sizeof(mask); i++)
		mask[i] |= m.mask[i];
	return *this;
}

loot_mask&
loot_mask::operator -=(const loot_mask &m)
{
	unsigned i;
	for (i = 0; i < sizeof(mask); i++)
		mask[i] &= ~m.mask[i];
	return *this;
}

bool
loot_mask::fsave(const char *name)
{
	FILE *f = fopen(name, "w");
	if (f) {
		int sz = fwrite(mask, 1, sizeof(mask), f);
		fclose(f);
		return sz == sizeof(mask);
	}
	return false;
}

bool
loot_mask::fload(const char *name)
{
	FILE *f = fopen(name, "r");
	if (f) {
		int sz = fread(mask, 1, sizeof(mask), f);
		fclose(f);
		return sz == sizeof(mask);
	} else {
		printf("Can't ope '%s'\n", name);
	}
	return false;
}
/* }}} */


struct loot_state {
	loot_mask		show;
	loot_mask		pick;

	loot_state() : show(true), pick(false) {}
	loot_state(const loot_state &s) : show(s.show), pick(s.pick) {}

	bool save(rfx_state *s) { show.save(s); pick.save(s); return true; }
	bool restore(rfx_state *s) { return show.restore(s) && pick.restore(s); }
};

class rfx_loot : public rfx_filter {
	loot_state		lm;
public:
	rfx_loot() {}

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

	void save_state(rfx_state *s) { lm.save(s); }
	bool load_state(rfx_state *s) { return lm.restore(s);  }

	std::string loot_cmd(const std::string &msg);
};

int
rfx_loot::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	int id = -1;
	switch (pkt->type) {
	case LOOT_DROP_NEW:
		pkt->desc = "new item on the ground";
		id = GET_INT16(pkt->data + 4);
//		pkt->show = 1;
		break;
	case LOOT_DROP_HORIZON:
		pkt->desc = "new item at the vision range";
		id = GET_INT16(pkt->data + 4);
//		pkt->show = 1;
		break;
	case LOOT_DISAPPEAR:
		pkt->desc = "loot disappear";
		pkt->show = 1;
		break;
	}
	if (id != -1) {
		pkt->drop = !lm.show.test(id);
	}
	return RFX_DECLINE;
}


enum {
	OP_SET,
	OP_ADD,
	OP_SUB
};

static bool
parse_mask(loot_mask &m, const char *n, unsigned nl)
{
	if (nl == 3 && memcmp(n, "all", 3) == 0) {
		m.set(true);
		return true;
	}
	if (nl == 4 && memcmp(n, "none", 4) == 0) {
		m.set(false);
		return true;
	}
	if (nl > 1 && *n == '@') {
		/* file name requested */
		char fn[64];
		snprintf(fn, sizeof(fn), "drop/list-%.*s.bin", nl - 1, n + 1);
		if (!m.fload(fn))
			return false;
		return true;
	}
	if (nl > 2 && n[0] == '0' && n[1] == 'x') {
		unsigned id = 0, i;
		for (i = 2; i < nl && isxdigit(n[i]); i++) {
			id <<= 4;
			id |= hex2i(n[i]);
		}
		m.set(false);
		m.set(id, true);
		return true;
	}
	return false;
}

std::string
rfx_loot::loot_cmd(const std::string &msg)
{
	const char *m;
	char reply[32];
	loot_mask d(false), nm(lm.show);

	if (msg == "info")
		goto reply;

	for (m = msg.c_str(); *m; ) {
		unsigned op = OP_SET, i;

		while (isspace(*m)) m++;
		if (!*m)
			break;
		switch (*m) {
		case '+':	m++; op = OP_ADD; break;
		case '-':	m++; op = OP_SUB; break;
		}
		
		for (i = 0; m[i] && m[i] != '+' && m[i] != '-' && !isspace(m[i]); i++)
			/* void */ ;
		if (!parse_mask(d, m, i))
			return "can't parse loot mask";

		switch (op) {
		case OP_ADD:	nm += d; break;
		case OP_SUB:	nm -= d; break;
		case OP_SET:	nm = d; break;
		}

		m += i;
	}
reply:
	unsigned count = nm.count();
	lm.show = nm;
	snprintf(reply, sizeof(reply), "ok, show = %u, ignore = %u", count, 65536 - count);
	return reply;
}

int
rfx_loot::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	if (ev->what == RFXEV_PRIV_SENT) {
		rfx_chat_event *e = (rfx_chat_event*)ev;
		if (e->nick == "loot") {
			std::string reply = loot_cmd(e->msg);
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, e->nick, reply, NULL));
			ev->drop_source();
			rf_packet_t *pkt = ev->get_source();
			if (pkt) pkt->show = 0;
			return RFX_BREAK;
		}
	}
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_loot();
}
