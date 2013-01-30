#include "rfx_api.h"
#include "rfx_modules.h"

#define	WHITE_CHAT_OUT	0x0202
#define	PRIVMSG_OUT		0x0302

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
		nick = "<UNKNOWN>";
		msg = "<UNSEEN>";
		break;
	case PRIVMSG_OUT:
		pkt->desc = "private message send from client";
		code = RFXEV_PRIV_SENT;
		nick = (char*)pkt->data + 4;
		msg = NULL;
		break;
	}

	/* if msg is NULL, try to find ":\x020" sequence after nick */
	if (code) {
		const char *p, *end = (const char*)pkt->data + pkt->len;
		unsigned nl = 0;

		for (p = nick; p < end && *p && *p != ':'; p++)
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
}

int
rfx_chat::process(rfx_event *ev, pqhead_t *pre, pqhead_t *post, evqhead_t *evq)
{
	return RFX_DECLINE;
}

extern "C" rfx_filter *API_FILTER_ID()
{
	return new rfx_chat();
}
