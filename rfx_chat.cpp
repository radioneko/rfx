#include "rfx_api.h"
#include "rfx_modules.h"

#define	WHITE_CHAT_OUT	0x0202
#define	PRIVMSG_OUT		0x0302
#define PRIVMSG_IN		0x0a02

class rfx_chat : public rfx_filter {
public:
	rfx_chat() {}

	int process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);
	int process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq);

	void save_state(rfx_state *) { }
	bool load_state(rfx_state *) { return true; }
};

int
rfx_chat::process(rf_packet_t *pkt, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	unsigned code = 0;
	const char *nick, *msg;

	switch (pkt->type) {
	case WHITE_CHAT_OUT:
		pkt->desc = "white chat send from client";
		code = RFXEV_WCHAT_SENT;
		nick = (char*)pkt->data + 5;
		msg = NULL;
		break;
	case PRIVMSG_OUT:
		pkt->desc = "private message send from client";
		code = RFXEV_PRIV_SENT;
		nick = (char*)pkt->data + 4;
		msg = NULL;
		break;
	case PRIVMSG_IN:
		pkt->desc = "incoming chat message";
		break;
	}

	/* if msg is NULL, try to find ":\x020" sequence after nick */
	if (code) {
		const char *p = nick, *end = (const char*)pkt->data + pkt->len;
		unsigned nl = 0;

		if (*p == '[') {
			while (p < end && *p != ']') p++;
			if (p + 1 >= end) {
				/* log format changed! */
				goto protocol_up;
			}
			nick = p + 1;
		}

		for (p = nick; p < end && *p && *p != ':' && *p != 0x20; p++)
			nl++;

		if (!msg) {
			while (p + 1 < end && p[0] != ':' && p[1] != 0x20)
				p++;
			msg = p + 2 < end ? p + 2 : "<ERROR>";
		}

		evq->push_back(new rfx_chat_event(code, std::string(nick, nl), msg, pkt));
		pkt->show = 1;
	}
	return RFX_DECLINE;
protocol_up:
	pkt->desc = "UNKNOWN CHAT MESSAGE";
	pkt->show = 1;
	return RFX_DECLINE;
}

int
rfx_chat::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	switch (ev->what) {
	case RFXEV_SEND_REPLY:
		{
			rfx_chat_event *e = (rfx_chat_event*)ev;
			uint8_t *p;
			unsigned l = e->msg.size() > 0xf0 ? 0xf0 : e->msg.size();
			static const char tmpl[] = {
				0x02, 0x11, 0x05, 0x00, 0x00,		// magic
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	// nick
				0xff, 0xff, 0xff, 0xff,				// ???
				0x00, 0x00, 0x01, 0x00				// ???
				/*0, ':', 0x20*/					// len + msg
			};
			rf_packet_t *pkt = pkt_new(4 + sizeof(tmpl) + 3 + l + 1, PRIVMSG_IN, SRV_TO_CLI);
			memcpy(pkt->data + 4, tmpl, sizeof(tmpl));
			p = pkt->data + 4 + 5;
			strncpy((char*)p, e->nick.c_str(), 10);
			p = pkt->data + sizeof(tmpl) + 4;
			*p++ = l + 2;
			*p++ = ':';
			*p++ = 0x20;
			memcpy(p, e->msg.c_str(), l);
			p[l] = 0;
			pqh_push(post, pkt);
			return RFX_BREAK;
		}
	}
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID(API_FILTER_ARGS)
{
	return new rfx_chat();
}
