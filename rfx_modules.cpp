#include "rfx_modules.h"
#include "cconsole.h"
#include <stdio.h>
#include <string.h>

static void
hexdump(FILE *out, unsigned rel, const void *data, unsigned size)
{
	const int frame = 16;
	unsigned i;
	for (i = 0; i < size; i += frame) {
		const uint8_t *src = (const uint8_t*)data + i;
		char hex[(3 * frame) + 2 * (frame - 1) / 4 + 1];
		char cc[frame + 1];
		const unsigned sz = i + frame < size ? frame : size - i;
		unsigned j, k = 0;
		for (j = 0; j < sz; j++) {
			static const char xd[] = "0123456789abcdef";
			hex[k] = 0x20;
			hex[k + 1] = xd[src[j] >> 4];
			hex[k + 2] = xd[src[j] & 0xf];
			k += 3;
			if ((j + 1) % 4 == 0 && j + 1 != frame) {
				hex[k] = 0x20;
				hex[k + 1] = '|';
				k += 2;
			}
			cc[j] = src[j] >= 0x20 && src[j] <= 127 ? src[j] : '.';
		}
		memset(hex + k, 0x20, sizeof(hex) - k);
		hex[sizeof(hex) - 1] = 0;
		cc[j] = 0;
		fprintf(out, "%08x %s  %s\n", rel + i, hex, cc);
	}
}

void dump_pkt(rf_packet_t *pkt)
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

uint8_t
hex2i(char digit)
{
	if (digit >= '0' && digit <= '9')
		return digit - '0';
	if (digit >= 'a' && digit <= 'f')
		return digit - 'a' + 10;
	if (digit >= 'A' && digit <= 'F')
		return digit - 'A' + 10;
	return 0;
}

