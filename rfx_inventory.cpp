#include "rfx_api.h"
#include "rfx_modules.h"
#include "cconsole.h"
#include "util.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>


static bool auto_farm(unsigned code)
{
	switch (code) {
	case 0x1614:
	case 0x1714:
	//case 0x29408:	// 25/20 atk dodge acc ring
		return true;
	}
	return false;
}

static bool ore[3] = {false, true, true};

static bool auto_ore(unsigned code)
{
	switch (code) {
	/* Ore +3 {{{ */
	case 0x0311:
	case 0x0911:
	case 0x0011:
	case 0x0611:
	case 0x0c11:
	/* }}} */
		return ore[2];
	/* Ore +2 {{{ */
	case 0x0411:
	case 0x0a11:
	case 0x0111:
	case 0x0711:
	case 0x0d11:
	/* }}} */
		return ore[1];
	/* Ore +1 {{{ */
	case 0x0511:
	case 0x0b11:
	case 0x0211:
	case 0x0811:
	case 0x0e11:
	/* }}} */
		return ore[0];
	}
	return false;
}

static unsigned get_cost(unsigned code)
{
	switch (code) {
	case 0x1614:		return 21576;
	case 0x1714:		return 24185; /* TODO */
	default:			return auto_ore(code) ? 1 : 0;
	}
}

static bool auto_loot(unsigned code)
{
	return auto_farm(code);
}

#define M_STEP 100000

#define	LOOT_DROP_NEW		0x1403
#define	LOOT_DROP_HORIZON	0x0f04

#define EMPTY_CELL 0xfffe
#define IID_UNKNOWN		0xffff

struct backpack_item {
	unsigned			code;		/* item id */
	uint16_t			iid;		/* inventory item id */
	uint8_t				cell;		/* inventory cell */
	unsigned			count;		/* quantity */

	backpack_item() : iid(EMPTY_CELL) {}
	backpack_item(unsigned code, uint16_t iid, uint8_t cell, unsigned count) : code(code), iid(iid), cell(cell), count(count) {}
	bool empty() const { return iid != EMPTY_CELL; }
};

typedef std::map<uint16_t, backpack_item>	iid_map_t;
class backpack {
public:
	backpack_item	items[100];
	iid_map_t		new_items;
public:

	void put(const backpack_item &i);
	unsigned iid_from_code(unsigned code);

	backpack_item *find_avail_by_code(unsigned code);
	backpack_item *find_by_iid(uint16_t iid);

	bool update_count(uint16_t iid, unsigned count);
	bool new_item(uint16_t iid, unsigned code, unsigned count);
	bool bind(uint16_t iid, uint8_t cell);
	bool remove(uint16_t iid);
	unsigned clear(unsigned code);

	int alloc_iid(unsigned code);
	uint64_t cost() const;

	void dump(int filter = -1);
	void reset();

	void save(rfx_state *s) { s->write(items, sizeof(items)); }
	bool load(rfx_state *s) { return s->read(items, sizeof(items)); }
};

/* backpack implementation {{{ */
uint64_t
backpack::cost() const
{
	uint64_t result = 0;
	/* walk "new_items" */
	for (iid_map_t::const_iterator i = new_items.begin(); i != new_items.end(); ++i)
		result += get_cost(i->second.code) * i->second.count;
	/* walk backpack */
	for (unsigned idx = 0; idx < sizeof(items) / sizeof(*items); idx++) {
		const backpack_item *ii = items + idx;
		if (ii->iid != EMPTY_CELL)
			result += get_cost(ii->code) * ii->count;
	}
	return result;
}

/* remove items with specified ids from inventory and from new_items */
unsigned
backpack::clear(unsigned code)
{
	unsigned count = 0;
	for (iid_map_t::iterator i = new_items.begin(), next = i; i != new_items.end(); i = next) {
		++next;
		if (i->second.code == code) {
			new_items.erase(i);
			count++;
		}
	}
	for (unsigned idx = 0; idx < sizeof(items) / sizeof(*items); idx++) {
		backpack_item *ii = items + idx;
		if (ii->iid != EMPTY_CELL && ii->code == code) {
			ii->iid = EMPTY_CELL;
			count++;
		}
	}
	return count;
}

/* lookup inventory item id for specified code.
 * returns -1 on failure, 0xffff = item needs to be placed to new cell */
int
backpack::alloc_iid(unsigned code)
{
	unsigned empty = 0;
	/* lookup in the new_items (unbound) map */
	for (iid_map_t::const_iterator i = new_items.begin(); i != new_items.end(); ++i) {
		const backpack_item *ii = &i->second;
		if (ii->code == code && ii->count >= 1 && ii->count < 99)
			return ii->iid;
	}
	/* look inside inventory */
	for (unsigned idx = 0; idx < sizeof(items) / sizeof(*items); idx++) {
		const backpack_item *ii = items + idx;
		if (ii->iid == EMPTY_CELL)
			empty++;
		if (ii->iid != EMPTY_CELL && ii->iid != IID_UNKNOWN
				&& ii->code == code && ii->count >= 1 && ii->count < 99)
			return ii->iid;
	}
	/* see if we have spare cells */
	if (empty)
		return 0xffff;
	return -1;
}

/* new item in the inventory. it still not bound to any cell */
bool
backpack::new_item(uint16_t iid, unsigned code, unsigned count)
{
	new_items[iid] = backpack_item(code, iid, 0xff, count);
	return true;
}

/* bind item to cell */
bool
backpack::bind(uint16_t iid, uint8_t cell)
{
	//printf("bind 0x%x => %u\n", iid, cell);
	if (cell > 99)
		return false;
	iid_map_t::iterator i = new_items.find(iid);
	if (i == new_items.end()) {
		/* maybe we're moving backpack item from one cell to another */
		backpack_item *ii = find_by_iid(iid);
		if (ii && ii->cell != cell) {
			ii->cell = cell;
			items[cell] = *ii;
			ii->iid = EMPTY_CELL;
			return true;
		}
		return false;
	}
	i->second.cell = cell;
	items[cell] = i->second;
	new_items.erase(i);
	return true;
}

/* remove item from backpack */
bool
backpack::remove(uint16_t iid)
{
	/* just in case */
	new_items.erase(iid);
	backpack_item *i = find_by_iid(iid);
	if (i) {
		i->iid = EMPTY_CELL;
		return true;
	}
	return false;
}

/* reset inventory state */
void
backpack::reset()
{
	for (unsigned i = 0; i < sizeof(items) / sizeof(*items); i++)
		items[i].iid = EMPTY_CELL;
	new_items.clear();
}

/* find backpack slot suitable for picking specified item */
backpack_item*
backpack::find_avail_by_code(unsigned code)
{
	for (unsigned i = 0; i < sizeof(items) / sizeof(*items); i++) {
		if (items[i].code == code && items[i].count > 0 && items[i].count < 99)
			return items + i;
	}
	return NULL;
}
/* find item by its iid */
backpack_item*
backpack::find_by_iid(uint16_t iid)
{
	for (unsigned i = 0; i < sizeof(items) / sizeof(*items); i++) {
		if (items[i].iid == iid)
			return items + i;
	}
	iid_map_t::iterator i = new_items.find(iid);
	return i == new_items.end() ? NULL : &i->second;
}

/* update item count by iid */
bool
backpack::update_count(uint16_t iid, unsigned count)
{
	backpack_item *ii = find_by_iid(iid);
	if (ii) {
		ii->count = count;
		return true;
	} else {
		return false;
	}
}

void
backpack::put(const backpack_item &i)
{
	if (i.cell < 100) {
		items[i.cell] = i;
	} else {
		printf("BAD CELL POSITION: %u\n", i.cell);
	}
}

unsigned
backpack::iid_from_code(unsigned code)
{
	for (unsigned i = 0; i < sizeof(items) / sizeof(*items); i++) {
		if (items[i].code == code && items[i].count < 99)
			return items[i].iid;
	}
	return 0xffff;
}

void
backpack::dump(int filter)
{
	unsigned i, count = 0;
	printf("\nINVENTORY:\n");
	for (i = 0; i < sizeof(items) / sizeof(*items); i++) {
		backpack_item *bi = items + i;
		if (bi->iid == EMPTY_CELL) continue;
		count++;
		if (filter != -1 && (int)bi->code != filter)
			continue;
		printf("    (%u %u %u) => 0x%02x | 0x%04x x %u\n",
				bi->cell / 20 + 1, (bi->cell % 20) / 5 + 1, (bi->cell % 5) + 1,
				bi->iid, bi->code, bi->count);
	}
	printf("====== %u items\n", count);
}
/* }}} */
/* ground item */
struct ground_item {
	uint16_t					gid;		/* ground id */
	unsigned					code;		/* hex code */
	ground_item(uint16_t gid, unsigned code) : gid(gid), code(code) {}
	TAILQ_ENTRY(ground_item)	link;
};

typedef std::map<uint16_t, ground_item> ground_loot_t;
/* FIFO queue of ground items to pick */
class pick_queue {
public:
	enum {FIFO, STACK, RANDOM};
private:
	ground_item						*active;
	ground_loot_t					loot;
	TAILQ_HEAD(gq_t,ground_item)	gq;
public:
	int								pick_mode;
	pick_queue() : active(NULL), pick_mode(FIFO)  { TAILQ_INIT(&gq); }
	/* enqueue item to pick */
	void enqueue(uint16_t gid, unsigned code);
	bool pick_top(uint16_t &gid, unsigned &code);
	void remove(uint16_t gid);
	void set_mode(int mode) { pick_mode = mode; }
	void reset() { TAILQ_INIT(&gq); loot.clear(); active = NULL; }
};

/* pick_queue implementation {{{ */
void
pick_queue::enqueue(uint16_t gid, unsigned code)
{
	ground_loot_t::iterator gi = loot.find(gid);
	if (gi == loot.end()) {
		gi = loot.insert(ground_loot_t::value_type(gid, ground_item(gid, code))).first;
	} else {
		gi->second.gid = gid;
		gi->second.code = code;
		TAILQ_REMOVE(&gq, &gi->second, link);
	}
	TAILQ_INSERT_TAIL(&gq, &gi->second, link);
}

bool
pick_queue::pick_top(uint16_t &gid, unsigned &code)
{
	if (active || TAILQ_EMPTY(&gq))
		return false;
	ground_item *gi;
	if (pick_mode == FIFO)
		gi = TAILQ_FIRST(&gq);
	else if (pick_mode == STACK)
		gi = TAILQ_LAST(&gq, gq_t);
	else {
		unsigned count = 0;
		/* pick random item */
		TAILQ_FOREACH(gi, &gq, link) {
			count++;
		}
		count = random() % count;
		TAILQ_FOREACH(gi, &gq, link) {
			if (!count)
				break;
			count--;
		}
	}
	gid = gi->gid;
	code = gi->code;
	active = gi;
	return true;
}

void
pick_queue::remove(uint16_t gid)
{
	ground_loot_t::iterator gi = loot.find(gid);
	if (gi != loot.end()) {
		if (&gi->second == active) {
			//printf("note: ready triggered\n");
			active = NULL;
		}
		TAILQ_REMOVE(&gq, &gi->second, link);
		loot.erase(gi);
	}
}
/* }}} */

class rfx_inventory : public rfx_filter {
	backpack		inv;
	pick_queue		pq;
	bool			autopick;
	bool			schedule_pick(evqhead_t *evq);
	uint64_t		c_margin;
	timespec		c_tstamp;
	std::string		handle_iq(const std::string &msg, evqhead_t *evq);
public:
	rfx_inventory() : autopick(false), c_margin(0) { clock_gettime(CLOCK_MONOTONIC, &c_tstamp); }

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

	void save_state(rfx_state *s) { sdw(s, autopick); sdw(s, pq.pick_mode); inv.save(s); }
	bool load_state(rfx_state *s) { sdr(s, autopick); sdr(s, pq.pick_mode); return inv.load(s); }
};

int
rfx_inventory::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	switch (pkt->type) {
	case 0x0603:
		{
//			pkt->show = 1;
			pkt->desc = "inventory list packet";
			inv.reset();
			for (unsigned i = 4; i + 21 <= pkt->len; i += 21) {
				uint8_t *p = pkt->data + i;
				backpack_item ii(GET_INT24(p + 3), IID_UNKNOWN, p[14], GET_INT16(p + 6));
				inv.put(ii);
			}
			c_margin = inv.cost();
			clock_gettime(CLOCK_MONOTONIC, &c_tstamp);
		}
		break;
	case 0x0407:
		if (pkt->len == 8) {
			/* successful pickup */
//			pkt->show = 1;
			pkt->desc = "inventory item update";
			inv.update_count(GET_INT16(pkt->data + 5), pkt->data[7]);
		}
		break;
	case 0x0307:
//		pkt->show = 1;
		pkt->desc = "new item in inventory";
		if (pkt->data[4] == 0) {
			printf(lcc_YELLOW "+++++ inventory: new item\n");
			inv.new_item(GET_INT16(pkt->data + 0x14) /* iid */,
					GET_INT24(pkt->data + 5) /* code */,
					GET_INT16(pkt->data + 8) /* count */);
		}
		break;
	case 0x100d:
//		pkt->show = 1;
		pkt->desc = "inventory bind";
		if (pkt->data[5] == 0) {
			unsigned n = pkt->data[4]; // data[5] is something like "slot"
			uint8_t *d = pkt->data + 6;
			for (unsigned i = 0; i < n; i++) {
				if (!inv.bind(GET_INT16(d) /* iid */, d[4] /* cell */))
					printf("*** bind failed\n");
				d += 5;
			}
		}
		break;
	case 0x1707:
//		pkt->show = 1;
		pkt->desc = "item disappear from inventory (srv notify)";
		if (pkt->data[4] == 0)
			if (!inv.remove(GET_INT16(pkt->data + 5) /* iid */))
				printf("******* remove failed\n");
		break;
	}
	return RFX_DECLINE;
}

static unsigned
h2i(const char *a)
{
	unsigned val = 0;
	if (a[0] == '0' && a[1] == 'x')
		a += 2;
	while (isxdigit(*a)) {
		val <<= 4;
		val |= hex2i(*a);
		a++;
	}
	return val;
}

#if 0
static void
dump_iq(const char *acode, backpack &b)
{
	unsigned code = h2i(acode);
	printf("*** DUMP_IQ: 0x%04x\n", code);
	for (unsigned i = 0; i < sizeof(b.items) / sizeof(*b.items); i++) {
		backpack_item *bi = b.items + i;
		if (bi->iid == EMPTY_CELL) continue;
		if (bi->code != code) continue;
		printf("    (%u %u %u) => 0x%02x | 0x%04x x %u\n",
				bi->cell / 20 + 1, (bi->cell % 20) / 5 + 1, (bi->cell % 5) + 1,
				bi->iid, bi->code, bi->count);
	}
}
#endif

bool
rfx_inventory::schedule_pick(evqhead_t *evq)
{
	int iid;
	uint16_t gid;
	unsigned code;
	if (autopick && pq.pick_top(gid, code) && (iid = inv.alloc_iid(code)) != -1) {
		//printf(lcc_GREEN "*** enqueued picking 0x%x to 0x%x" lcc_NORMAL "\n", gid, iid);
		evq->push_back(new rfx_pick_do_event(gid, iid, code, iid == 0xffff ? 1000 : 0));
		return true;
	}
	return false;
}

std::string
rfx_inventory::handle_iq(const std::string &msg, evqhead_t *evq)
{
	if (msg == "on") {
		autopick = true;
		schedule_pick(evq);
		return "autopick enabled";
	} else if (msg == "off") {
		autopick = false;
		return "autopick disabled";
	} else if (msg == "rnd") {
		pq.set_mode(pick_queue::RANDOM);
		return "randomizing loot pickups";
	} else if (msg == "seq") {
		pq.set_mode(pick_queue::FIFO);
		return "sequental loot pickup";
	} else if (msg == "inv") {
		pq.set_mode(pick_queue::STACK);
		return "inverted loot pickup";
	} else if (msg == "cost") {
		char reply[64];
		sprintf(reply, "total cost: %.2fkk", inv.cost() / 1000000.0);
		return reply;
	} else if (msg == "reset") {
		pq.reset();
		evq->push_back(new rfx_event(RFXEV_LOOT_PICK_RESET, NULL));
		return "reset autopicker list";
	} else if (msg.size() > 3 && msg.compare(0, 1, "-") == 0 && isxdigit(msg[1])) {
		char reply[64];
		int code = h2i(msg.c_str() + 1);
		sprintf(reply, "%u items with code 0x%x removed", inv.clear(code), code);
		clock_gettime(CLOCK_MONOTONIC, &c_tstamp);
		c_margin = inv.cost();
		return reply;
	} else {
		int filter = msg.empty() ? -1 : h2i(msg.c_str());
		inv.dump(filter);
		return "inventory dumped to stdout";
	}
	return "invalid syntax";
}

int
rfx_inventory::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	if (ev->what == RFXEV_WCHAT_SENT) {
		rfx_chat_event *e = (rfx_chat_event*)ev;
		if (e->msg == "di") {
			inv.dump();
			e->ignore_source();
			return RFX_BREAK;
		}
//	} else if (ev->what == RFXEV_BACKPACK_LOOKUP) {
//		return RFX_BREAK;
	} else if (ev->what == RFXEV_PRIV_SENT) {
		rfx_chat_event *e = (rfx_chat_event*)ev;
		if (e->nick == "iq") {
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, e->nick, handle_iq(e->msg, evq), NULL));
			e->ignore_source();
			return RFX_BREAK;
		}
	} else if (ev->what == RFXEV_LOOT_APPEAR) {
		/* new item on the ground */
		rfx_loot_event *e = (rfx_loot_event*)ev;
		if (auto_loot(e->code)) { /* gli and beam */
			pq.enqueue(e->gid, e->code);
			schedule_pick(evq);
		}
	} else if (ev->what == RFXEV_LOOT_DISAPPEAR) {
		rfx_loot_event *e = (rfx_loot_event*)ev;
//		printf("*remove loot 0x%x\n", e->gid);
		pq.remove(e->gid);
	} else if (ev->what == RFXEV_LOOT_PICK) {
		uint64_t cost = inv.cost();
		rfx_loot_pick_event *e = (rfx_loot_pick_event*)ev;
		//if (e->result == PICK_NORIGHT || e->result == PICK_TOOFAR)
		//	e->drop_source();
		pq.remove(e->gid);
		if (cost >= c_margin + M_STEP) {
			char ntfy[128];
			timespec now;
			double avg, dt;
			clock_gettime(CLOCK_MONOTONIC, &now);
			dt = clock_diff(&c_tstamp, & now) / 1000000.0;
			if (dt > 0)
				avg = (double)(cost - c_margin) / dt / 1000000.0 * 60.0;
			else
				avg = 9999.99;
			snprintf(ntfy, sizeof(ntfy), "cost %.2fkk, %.3fkk/min (1kk in %.2f sec)",
					cost / 1000000.0, avg, dt);
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, "iq", ntfy, NULL));

			c_tstamp = now;
			c_margin = cost;
		}
		schedule_pick(evq);
	}
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_inventory();
}
