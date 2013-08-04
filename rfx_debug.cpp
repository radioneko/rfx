#include "rfx_api.h"
#include "rfx_modules.h"
#include <stdio.h>

#define	LOOT_DROP_NEW		0x1403
#define	LOOT_DROP_HORIZON	0x0f04

class rfx_debug : public rfx_filter {
	rf_packet_t		*grab;
	int				grab_id;
	bool			dbg_show;
public:
	rfx_debug() : grab(NULL), grab_id(-1) {}
	~rfx_debug() { }

	void ungrab_pkt();

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

#define svars(s, XX) do { XX(s, dbg_show); XX(s, grab_id); XX(s, grab); } while (0)
	void save_state(rfx_state *s) { svars(s, sdw); }
	bool load_state(rfx_state *s) { ungrab_pkt(); svars(s, sdr); return true; }
#undef svars
};

void rfx_debug::ungrab_pkt()
{
	if (grab) {
		pkt_unref(grab);
		grab = NULL;
		grab_id = -1;
	}
}

static bool
find_int16(rf_packet_t *pkt, unsigned what)
{
	unsigned i;
	for (i = 4; i + 1 < pkt->len; i++) {
		if ((pkt->data[i] == (what & 0xff)) &&
				pkt->data[i + 1] == (what >> 8))
			return true;
	}
	return false;
}

int
rfx_debug::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
#if 1
	switch (pkt->type) {
	case 0x1304: // cli -> srv: move
	case 0x0104: // cli -> srv: move?
	case 0x0204: // cli -> srv: move?
	case 0x1604: //???
	case 0x0404: // srv -> cli
	case 0x1404: // srv -> cli
	case 0x0c0b: // srv -> cli
	case 0x0a04:
	case 0x0504:
	case 0x0266:
	case 0x020b:
	case 0x0366:
	case 0x990b:
	case 0x1e04:
	case 0x970b:
	case 0x0b04:
	case 0x0107:
		/* dig */
	//case 0x10e: // client: dig request (data[5] = 0x11 to dig ore+4)
	case 0x020e: // server: dig started
	case 0x060e: // server: dig result
		pkt->show = 0;
		break;
	case 0x10e: /* digging */
		if (dbg_show) {
			//pkt->show = dbg_show;
			pkt->data[5] = 0x11;
		}
		break;
	default:
		pkt->show = dbg_show;
	}
#endif
	return RFX_DECLINE;
#if 1
	static uint8_t baff_removed[] = {
		0x0b, 0x00, 0x11, 0x11,
		0x00, 0x10, 0x05, 0x00, 0x00,
		/* baff position */
		0xff, 0xff
	};
#else
	static uint8_t baff_removed[] = {
	 0x0a, 0x00, 0x11, 0x0b, 0x08, 0x10, 0x10, 0x05, 0x00, 0x00
	};
#endif

	if (pkt->type == grab_id) {
		pkt->desc = "[ grabbed packet ]";
		pkt->show = dbg_show;
		grab_id = -1;
		ungrab_pkt();
		grab = pkt_ref(pkt);
	}
	/* magic timeout */
	if (pkt->type == 0x0211 && pkt->data[4] != 0x00 && grab) {
		pkt->drop = 1;
		pkt->delay = 1000;
		pqh_push(post, pkt_ref(grab));
	}
	if (pkt->len == sizeof(baff_removed) && grab && memcmp(pkt->data, baff_removed, sizeof(baff_removed)) == 0) {
		pkt->desc = "[ TRIGGER ]";
		pkt->show = dbg_show;
		pkt->delay = 0;
//		evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, "debug", "rebaf", NULL));
		printf("************************************ buff request trigger\n");
		pqh_push(post, pkt_ref(grab));
	}
#if 0
	if (pkt->dir == SRV_TO_CLI) {
//		if (find_int16(pkt, 0x0f) && find_int16(pkt, 0x26) && find_int16(pkt, 0x13)) {
		if (find_int16(pkt, 0x41)) {
			printf("**** MATCH!!!!!\n");
			pkt->desc = "MATCHED!!!";
			pkt->show = dbg_show;
		}
	}
#endif
//	if (pkt->len > 80)
//		pkt->show = dbg_show;
//	return RFX_DECLINE;
#if 1
	switch (pkt->type) {
	/* bafs */
	case 0x0211:
		pkt->desc = "baff response";
		pkt->show = dbg_show;
		break;
	case 0x0111: // baff apply request
		pkt->desc = "baff apply";
		pkt->show = dbg_show;
		break;
	case 0x1111: // baff removed
		pkt->desc = "baff remove";
		pkt->show = dbg_show;
		break;
	case 0x200d:
	case 0x1e0d:
		pkt->show = dbg_show;
		break;
	case 0x0a04:
	case 0x0504:
	case 0x0266:
	case 0x020b:
	case 0x0366:
	case 0x990b:
	case 0x1e04:
	case 0x970b:
	/* */
	case 0x6511: // cleaning grenade
	/* character movement */
	case 0x1304: // cli -> srv: move
	case 0x0104: // cli -> srv: move?
	case 0x0204: // cli -> srv: move?
	case 0x1604: //???
	case 0x0404: // srv -> cli
	case 0x1404: // srv -> cli
	case 0x0c0b: // srv -> cli
		pkt->show = 0;
		break;
	default:
		pkt->show = dbg_show;
	}
#else
	switch (pkt->type) {
	case 0x0404:
		break;
		{
			float x = *(float*)(pkt->data + 8), y = *(float*)(pkt->data + 12);
			printf("pos = %.2f, %.2f\n", x, y);
		}
		pkt->show = dbg_show;
		break;
	}
#endif
	return RFX_DECLINE;
}

#include <stdio.h>
int
rfx_debug::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	if (ev->what == RFXEV_PRIV_SENT) {
		rfx_chat_event *e = (rfx_chat_event*)ev;
		if (e->nick == "dbg" || e->nick == "debug") {
			std::string reply = "unknown cmd";
			if (e->msg == "enable" || e->msg == "on" || e->msg == "show") {
				dbg_show = true;
				reply = "enabled";
			} else if (e->msg == "disable" || e->msg == "off" || e->msg == "hide") {
				dbg_show = false;
				reply = "disabled";
			}
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, e->nick, reply, NULL));
			e->ignore_source();
		}
	} else if (ev->what == RFXEV_WCHAT_SENT) {
		rfx_chat_event *e = (rfx_chat_event*)ev;
		if (e->msg == "sp") {
			printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
			e->ignore_source();
		}
		if (e->msg.length() >= 6 && e->msg[0] == '0' && e->msg[1] == 'x') {
			unsigned id = 0;
			for (unsigned i = 0; i < 4 && isxdigit(e->msg[i + 2]); i++)
				id = (id << 4) | hex2i(e->msg[i + 2]);
			grab_id = id ? id : -1;
			e->ignore_source();
			if (grab_id != -1) {
				char msg[32];
				snprintf(msg, sizeof(msg), "grabbing 0x%04x", grab_id);
				evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, "debug", msg, NULL));
			}
		}
		if (e->msg == "u") {
			ungrab_pkt();
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, "debug", "packet ungrabbed", NULL));
			e->ignore_source();
		}
		if (e->msg == "s") {
			char msg[32];
			if (grab) {
				snprintf(msg, sizeof(msg), "replay 0x%04x, refc = %u", grab->type, grab->refc);
				pqh_push(post, pkt_ref(grab));
			} else {
				strcpy(msg, "packet not grabbed yet");
			}
			evq->push_back(new rfx_chat_event(RFXEV_SEND_REPLY, "debug", msg, NULL));
			e->ignore_source();
		}
//		printf("WHITE: nick = '%s', msg = '%s'\n", e->nick.c_str(), e->msg.c_str());
	}
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_debug();
}
