#define	SYSLOG_NAMES
#include "mconf.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdio.h>
#include <inttypes.h>
#include "ini.h"
#include <syslog.h>

struct mconf_block {
	struct mconf_block	*next;
	struct metad_param	*opt;
	const char			*section;
	unsigned			n_opt;
};

struct mconf {
	char				*cfg_path;
	int					argc;
	char				**argv;
	struct mconf_block	*cb;
	unsigned			short_len;			/* precalculated length of string "opt_short" */
	char				*opt_short;
	unsigned			long_count;
	int					long_idx;
	struct option		*opt_long;
	int					help_only:1;
};

int
mconf_set_string(struct metad_param *p, const char *section, const char *value)
{
	if (p->is_set && *(char**)p->ptr)
		free(*(char**)p->ptr);
	*(char**)p->ptr = strdup(value);
	return 0;
}

int
mconf_set_aval(struct metad_param *p, const char *section, const char *value)
{
	if (strcasecmp(value, "auto") == 0) {
		*(int*)p->ptr = -1;
	} else {
		char *ep = NULL;
		long v = strtol(value, &ep, 0);
		if ((v == LONG_MIN || v == LONG_MAX) && errno == ERANGE)
			return -1;
		if (*ep != 0 && !isspace(*ep))
			return -1;
		*(int*)p->ptr = v;
	}
	return 0;
}

int
mconf_set_uint_size(struct metad_param *p, const char *section, const char *value)
{
	char *ep = NULL;
	unsigned long v = strtoul(value, &ep, 0);
	switch (*ep) {
	case 'k':
	case 'K':
		v <<= 10;
		break;
	case 'm':
	case 'M':
		v <<= 20;
		break;
	case 'g':
	case 'G':
		v <<= 30;
	case 0:
	case 9:
	case 10:
	case 13:
	case '#':
	case 0x20:
		break;
	default:
		return -1;
	}
	*(unsigned*)p->ptr = v;
	return 0;
}

int
mconf_set_logopts(struct metad_param *p, const char *section, const char *value)
{
	struct logopts *o = (struct logopts *)p->ptr;
	CODE *c;
	unsigned l;

	memset(o, 0, sizeof(*o));
	/* length of facility name */
	for (l = 0; isalnum(value[l]); l++)
		/* void */;

	for (c = facilitynames; c->c_name; c++)
		if (strlen(c->c_name) == l && memcmp(c->c_name, value, l) == 0) {
			o->facility = c->c_val;
			break;
		}

	if (!c->c_name)
		return -1;

	if (strstr(value, "(stderr)"))
		o->opts = LOG_PERROR;

	return 0;
}

int
mconf_set_loglevel(struct metad_param *p, const char *section, const char *value)
{
	CODE *c;
	unsigned *mask = p->ptr;

	while (*value) {
		unsigned l, m = 0;
		bool unset = false;

		if (*value == '-') {
			unset = true;
			value++;
		}
		for (l = 0; isalnum(value[l]); l++)
			/* void */;
		if (!l && value[l])
			return -1;

		for (c = prioritynames; c->c_name; c++)
			if (strlen(c->c_name) == l && memcmp(c->c_name, value, l) == 0) {
				m = 1 << c->c_val;
				break;
			}
		if (!c->c_name) /* invalid priority */
			return -1;
		value += l;
		while (isspace(*value) || *value == ',')
			value++;

		if (unset)
			*mask &= ~m;
		else
			*mask |= m;
	}
	return 0;
}

int
mconf_get_string(struct metad_param *p, char *buf, int maxlen)
{
	char *s = *(char**)p->ptr;
	if (s) {
		unsigned l;
		for (l = 0; l < maxlen && s[l]; l++)
			buf[l] = s[l];
		return l;
	}
	return 0;
}

int
mconf_get_aval(struct metad_param *p, char *buf, int maxlen)
{
	int l;
	static const char auto_str[] = "auto";
	unsigned v = *(unsigned*)p->ptr;
	if (v == (unsigned)-1) {
		if (maxlen < sizeof(auto_str) - 1)
			return -1;
		memcpy(buf, auto_str, sizeof(auto_str) - 1);
		return sizeof(auto_str) - 1;
	}

	l = snprintf(buf, maxlen, "%u", v); /* TODO: snprintf not the best solution here because of extra '\0' */

	return l < maxlen ? l : -1;
}

int
mconf_get_uint_size(struct metad_param *p, char *buf, int maxlen)
{
	int l;
	unsigned v = *(unsigned*)p->ptr;

	/* TODO: and again snprintf gives extra 0 at the end of buffer */
	if ((v & ((1 << 30) - 1)) == 0) {
		l = snprintf(buf, maxlen, "%ug", v >> 30);
	} else if ((v & ((1 << 20) - 1)) == 0) {
		l = snprintf(buf, maxlen, "%um", v >> 20);
	} else if ((v & ((1 << 10) - 1)) == 0) {
		l = snprintf(buf, maxlen, "%uk", v >> 10);
	} else {
		l = snprintf(buf, maxlen, "%u", v);
	}

	return l < maxlen ? l : -1;
}

int
mconf_get_logopts(struct metad_param *p, char *buf, int maxlen)
{
	struct logopts *o = (struct logopts *)p->ptr;
	int l = 0;
	CODE *c;

	for (c = facilitynames; c->c_name; c++)
		if (o->facility == c->c_val) {
			l = strlen(c->c_name);
			if (maxlen <= l)
				return -1;
			memcpy(buf, c->c_name, l + 1);
			break;
		}

	if (o->opts & LOG_PERROR) {
		if (maxlen <= l + sizeof("(stderr)") - 1)
			return -1;
		strcpy(buf + l, "(stderr)");
		l += sizeof("(stderr)") - 1;
	}

	return l;
}

int
mconf_get_loglevel(struct metad_param *p, char *buf, int maxlen)
{
	int l = 0;
	CODE *c;
	unsigned mask = *(unsigned*)p->ptr;

	for (c = prioritynames; c->c_name; c++) {
		unsigned m = 1 << c->c_val;
		if (mask & m) {
			unsigned cl = strlen(c->c_name);
			if (l + cl + 1 >= maxlen)
				return -1;
			if (l)
				buf[l++] = ',';
			memcpy(buf + l, c->c_name, cl + 1);
			l += cl;
		}
		mask &= ~m; /* to supress alias output */
	}
	return l;
}

struct mconf*
mconf_init(const char *cfg_path, int argc, char **argv)
{
	int i;
	struct mconf *cf = calloc(1, sizeof(*cf));
	const char *conf = cfg_path;
	cf->argc = argc;
	cf->argv = argv;
	for (i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "--conf") == 0 || strcmp(argv[i], "-c") == 0) && i + 1 < argc) {
			conf = argv[i + 1];
			break;
		}
		if (memcmp(argv[i], "--conf=", sizeof("--conf=") - 1) == 0) {
			conf = argv[i] + sizeof("--conf=") - 1;
			break;
		}
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			cf->help_only = 1;
			conf = cfg_path;
			break;
		}
	}

	cf->cfg_path = conf ? strdup(conf) : NULL;
	cf->long_idx = 256;

	return cf;
}

static bool
mconf_check_conflict(struct mconf *cf, const char *section, struct metad_param *opt)
{
	struct mconf_block *cb;
	for (cb = cf->cb; cb; cb = cb->next) {
		unsigned i;
		struct metad_param *p = cb->opt;
		for (i = 0; i < cb->n_opt; i++) {
			/* config name conflict */
			if (p[i].conf_name && opt->conf_name && strcmp(p[i].conf_name, opt->conf_name) == 0
					&& strcmp(cb->section, section) == 0)
				return true;
			/* short option conflict */
			if (p[i].opt_short && p[i].opt_short == opt->opt_short)
				return true;
			/* long option conflict */
			if (p[i].opt_long && opt->opt_long && strcmp(p[i].opt_long, opt->opt_long) == 0)
				return true;
		}
	}
	return false;
}

/* Append config option list */
int
mconf_add(struct mconf *cf, const char *section, struct metad_param *opts, unsigned nopt)
{
	unsigned i;
	/* First analyze config block to find collisions */
	for (i = 0; i < nopt; i++) {
		struct metad_param *opt = opts + i;
		if (mconf_check_conflict(cf, section, opt))
			return -1;
		if (opt->opt_short) {
			cf->short_len++;
			if (opt->opt_args == MOPT_REQUIRED_ARGUMENT)
				cf->short_len++;
			else if (opt->opt_args == MOPT_OPTIONAL_ARGUMENT)
				cf->short_len += 2;
		}
		if (opt->opt_long) {
			if (opt->opt_short)
				opt->opt_idx = opt->opt_short;
			else
				opt->opt_idx = cf->long_idx++;
			cf->long_count++;
		}
	}
	struct mconf_block *cb = calloc(1, sizeof(*cb)), **p = &cf->cb;
	while (*p)
		p = &(*p)->next;
	cb->opt = opts;
	cb->n_opt = nopt;
	cb->section = section;
	*p = cb;
	return 0;
}

static inline bool
is_ws(char c)
{
	switch (c) {
	case 0x20:
	case '\t':
	case '\r':
	case '\n':
		return true;
	}
	return false;
}

void
mconf_help_msg(struct mconf *cf, unsigned width)
{
	unsigned i;
	unsigned opt_w = 0;
	const unsigned opt_ind = 2;
	const unsigned opt_spc = 4;
	struct mconf_block *cb;

	/* Find out maximum width of options column */
	for (cb = cf->cb; cb; cb = cb->next) {
		for (i = 0; i < cb->n_opt; i++) {
			unsigned w = 0;
			unsigned comma_spc = 0;
			struct metad_param *p = cb->opt + i;

			if (p->opt_short) {
				w += 2;
				comma_spc = 2;
			}

			if (p->opt_long)
				w += 2 + strlen(p->opt_long) + comma_spc;

			if (p->comment) {
				const char *t = strchr(p->comment, '|');
				if (t)
					w += t - p->comment + 1;
			}

			if (w > opt_w)
				opt_w = w;
		}
	}

	/* Display help line by line */
	for (cb = cf->cb; cb; cb = cb->next) {
		for (i = 0; i < cb->n_opt; i++) {
			const char *s;
			char line[128], *d = line + opt_ind;
			struct metad_param *p = cb->opt + i;

			if (!p->opt_short && ! p->opt_long)
				continue;

			/* Construct option column */
			memset(line, 0x20, opt_ind);

			if (p->opt_short) {
				*d++ = '-';
				*d++ = p->opt_short;
			}

			if (p->opt_long) {
				if (p->opt_short) {
					*d++ = ',';
					*d++ = 0x20;
				}
				*d++ = '-';
				*d++ = '-';

				for (s = p->opt_long; *s; )
					*d++ = *s++;
			}

			if (p->comment) {
				s = strchr(p->comment, '|');
				if (s) {
					*d++ = 0x20;
					memcpy(d, p->comment, s - p->comment);
					d += s - p->comment;
					s++;
				} else {
					s = p->comment;
				}
			} else {
				s = NULL;
			}

			/* Construct description column */
			do {
				unsigned j;
				char *e = line + opt_w + opt_ind + opt_spc;
				while (d < e)
					*d++ = 0x20;

				e = line + width;
				while (d < e && *s) {
					for (j = 0; d + j < e && s[j]; j++) {
						if (is_ws(s[j]))
							break;
						d[j] = s[j];
					}
					if (d + j >= e)
						break;
					d += j;
					s += j;
					if (is_ws(*s))
						*d++ = 0x20;
					while (*s && is_ws(*s))
						s++;
				}
				while (d > line && d[-1] == 0x20)
					*d-- = 0;
				*d = 0;
				puts(line);
				d = line;
			} while (*s);

			if (p->to_string) {
				int l;
				memset(line, 0x20, opt_w + opt_ind + opt_spc);
				d = line + opt_w + opt_ind + opt_spc;
				strcpy(d, "default: ");
				d += sizeof("default: ") - 1;
				l = p->to_string(p, d, line + sizeof(line) - d);
				if (l > 0) {
					d[l] = 0;
					puts(line);
				}
			}
		}
	}
}

/* Parse command line */
static int
mconf_parse_cmdline(struct mconf *cf)
{
	unsigned i;
	char *d;
	struct mconf_block *cb;
	struct option *o;
	struct metad_param *cmap[256];

	memset(cmap, 0, sizeof(cmap));
	cf->opt_short = d = malloc(cf->short_len + 1 + 2);
	/* Add default '-c' option to avoid getopt errors */
	*d++ = 'c';
	*d++ = ':';
	cf->opt_long = o = calloc(cf->long_count + 1, sizeof(struct option));
	/* Preparse option array to make input data for getop/getopt_long */
	for (cb = cf->cb; cb; cb = cb->next) {
		for (i = 0; i < cb->n_opt; i++) {
			struct metad_param *p = cb->opt + i;
			/* Fill short options string */
			if (p->opt_short) {
				*d++ = p->opt_short;
				switch (p->opt_args) {
				case MOPT_OPTIONAL_ARGUMENT:
					*d++ = ':';
					/* fallthrough */
				case MOPT_REQUIRED_ARGUMENT:
					*d++ = ':';
				}
				cmap[(unsigned)p->opt_short] = p;
			}
			/* Fill long options array */
			if (p->opt_long) {
				o->name = p->opt_long;
				switch (p->opt_args) {
				case MOPT_NO_ARGUMENT:
					o->has_arg = no_argument;
					break;
				case MOPT_REQUIRED_ARGUMENT:
					o->has_arg = required_argument;
					break;
				case MOPT_OPTIONAL_ARGUMENT:
					o->has_arg = optional_argument;
					break;
				}
				o->val = p->opt_idx;
				o->flag = NULL;
				o++;
			}
		}
	}

	*d = 0;
	memset(o, 0, sizeof(*o));

	/* Parse command line options */
	while (1) {
		int c;
		int option_index = -1;
		c = getopt_long(cf->argc, cf->argv, cf->opt_short, cf->opt_long, &option_index);

		if (c == -1)
			break;

		if (c == '?') {
			fprintf(stderr, "Invalid option\n");
			return -1;
		}

		/* Ignore -c option that was already handled */
		if (c == 'c')
			continue;

		for (cb = cf->cb; cb; cb = cb->next) {
			for (i = 0; i < cb->n_opt; i++) {
				struct metad_param *p = cb->opt + i;
				if ((p->opt_short && p->opt_short == c) ||
						(option_index != -1 && p->opt_long && cf->opt_long[option_index].val == p->opt_idx)) {
					if (p->set(p, cb->section, optarg) == 0)
						p->is_set = 1;
				}
			}
		}
	}

	return 0;
}

/* Callback for inih library */
static int
ini_handler(void *ptr, const char *section, const char *name, const char *value)
{
	struct mconf *cf = ptr;
	struct mconf_block *cb;

	for (cb = cf->cb; cb; cb = cb->next) {
		if (cb->section && section && strcmp(cb->section, section) == 0) {
			unsigned i;
			for (i = 0; i < cb->n_opt; i++) {
				struct metad_param *p = cb->opt + i;
				if (p->conf_name && strcmp(name, p->conf_name) == 0) {
					if (p->set(p, section, value) == 0) {
						p->is_set = 1;
						return 1;
					} else {
						return 0;
					}
				}
			}
		}
	}
	/* unknown option */
	return 0;
}

/* Parse config file with inih */
static int
mconf_parse_conf(struct mconf *cf)
{
	int error;
	error = ini_parse(cf->cfg_path, ini_handler, cf);
	if (error != 0) {
		fprintf(stderr, "Can't parse %s (error in line %d)\n", cf->cfg_path, error);
		return -1;
	}
	return 0;
}

/* First parse config file then apply adjustments from command line parameters */
int
mconf_parse(struct mconf *cf)
{
	if (cf->help_only) {
		/* Ugly hack to show default values when help is requested */
		unsigned i;
		struct mconf_block *cb;
		/* try to locate help handler */
		for (cb = cf->cb; cb; cb = cb->next) {
			for (i = 0; i < cb->n_opt; i++) {
				struct metad_param *p = cb->opt + i;
				if (p->opt_short == 'h') {
					p->ptr = cf;
					p->set(p, NULL, NULL);
					exit(EXIT_SUCCESS);
				}
			}
		}
		mconf_help_msg(cf, 79);
		exit(EXIT_SUCCESS);
	} else {
		if (cf->cfg_path && mconf_parse_conf(cf) != 0)
			return -1;
		return mconf_parse_cmdline(cf);
	}
}

/* mconf_free doesn't deallocate any of parameter values */
void
mconf_free(struct mconf *cf)
{
	struct mconf_block *cb = cf->cb;

	if (cf->opt_short)
		free(cf->opt_short);
	if (cf->opt_long)
		free(cf->opt_long);
	if (cf->cfg_path)
		free(cf->cfg_path);

	while (cb) {
		struct mconf_block *cn = cb->next;
		free(cb);
		cb = cn;
	}

	free(cf);
}

