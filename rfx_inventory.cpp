#include "rfx_api.h"
#include "rfx_modules.h"
#include <stdio.h>
#include <map>

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

	int alloc_iid(unsigned code);

	void dump(int filter = -1);
	void reset();

	void save(rfx_state *s) { s->write(items, sizeof(items)); }
	bool load(rfx_state *s) { return s->read(items, sizeof(items)); }
};

/* backpack implementation {{{ */

/* lookup inventory item id for specified code.
 * returns -1 on failure, 0xffff = item needs to be placed to new cell */
int
backpack::alloc_iid(unsigned code)
{
	unsigned empty = 0;
	/* lookup in the new_items (unbound) map */
	for (iid_map_t::const_iterator i = new_items.begin(); i != new_items.end(); ++i) {
		const backpack_item *ii = &i->second;
		if (ii->code == code && ii->count >= 1 && ii->count <= 100)
			return ii->iid;
	}
	/* look inside inventory */
	for (unsigned idx = 0; idx < sizeof(items) / sizeof(*items); idx++) {
		const backpack_item *ii = items + idx;
		if (ii->iid == EMPTY_CELL)
			empty++;
		if (ii->code == code && ii->count >= 1 && ii->count <= 100 &&
				ii->iid != EMPTY_CELL && ii->iid != IID_UNKNOWN)
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
	printf("bind 0x%x => %u\n", iid, cell);
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

class rfx_inventory : public rfx_filter {
	backpack inv;
public:
	rfx_inventory() {}

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

	void save_state(rfx_state *s) { inv.save(s); }
	bool load_state(rfx_state *s) { return inv.load(s); }
};

int
rfx_inventory::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	switch (pkt->type) {
	case 0x0603:
		{
			pkt->show = 1;
			pkt->desc = "inventory list packet";
			inv.reset();
			for (unsigned i = 4; i + 21 <= pkt->len; i += 21) {
				uint8_t *p = pkt->data + i;
				backpack_item ii(GET_INT24(p + 3), IID_UNKNOWN, p[14], GET_INT16(p + 6));
				inv.put(ii);
			}
		}
		break;
	case 0x0407:
		if (pkt->len == 8) {
			/* successful pickup */
			pkt->show = 1;
			pkt->desc = "inventory item update";
			inv.update_count(GET_INT16(pkt->data + 5), pkt->data[7]);
		}
		break;
	case 0x0307:
		pkt->show = 1;
		pkt->desc = "new item in inventory";
		if (pkt->data[4] == 0) {
			inv.new_item(GET_INT16(pkt->data + 0x14) /* iid */,
					GET_INT24(pkt->data + 5) /* code */,
					GET_INT16(pkt->data + 8) /* count */);
		}
		break;
	case 0x100d:
		pkt->show = 1;
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
		pkt->show = 1;
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
			int filter = e->msg.empty() ? -1 : h2i(e->msg.c_str());
			inv.dump(filter);
			e->ignore_source();
			return RFX_BREAK;
		}
	}
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_inventory();
}
