#include "rfx_modules.h"
#include "cconsole.h"
#include <stdio.h>
#include <string.h>

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

/* loot mask {{{ */
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
	unsigned i = (idx & 0xffff) >> 3;
	if (i < sizeof(mask))
		return (0x80 >> (idx & 7)) & mask[i];
	return false;
}

bool
loot_mask::set(unsigned idx, bool flag)
{
	bool result = false;
	unsigned i = (idx & 0xffff) >> 3;
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
		printf("Can't open '%s'\n", name);
	}
	return false;
}

/* helper funtion that parses exactly one expression */
static bool
parse_lexpr(loot_mask &m, const char *n, unsigned nl)
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

enum {
	OP_SET,
	OP_ADD,
	OP_SUB
};

/* Parse loot set: loot identifiers separated by "+" and "-" signs */
bool
loot_mask::parse(const std::string &cmd)
{
	const char *m;
	for (m = cmd.c_str(); *m; ) {
		loot_mask d(false);
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
		if (!parse_lexpr(d, m, i))
			return false;

		switch (op) {
		case OP_ADD:	*this += d; break;
		case OP_SUB:	*this -= d; break;
		case OP_SET:	*this = d; break;
		}

		m += i;
	}
	return true;
}

/* }}} */
