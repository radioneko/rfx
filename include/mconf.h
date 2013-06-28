#pragma once

#define MOPT_NO_ARGUMENT		0
#define MOPT_REQUIRED_ARGUMENT	1
#define MOPT_OPTIONAL_ARGUMENT	2

struct metad_param {
	char			opt_short;
	const char		*opt_long;
	const char		*conf_name;
	int				(*set)(struct metad_param *p, const char *section, const char *value);
	int				(*to_string)(struct metad_param *p, char *buf, int maxlen);
	void			*ptr;
	unsigned		is_set:1;
	unsigned		opt_args:2;
	unsigned		has_default:1;
	const char		*comment;			/* "name|comment" */
	int				opt_idx;
};

struct logopts {
	int facility;
	int opts;
};

/* MCONF_TYPE(_DLF)? (value, conf_option_name, short_option, long_option, comment) */
#define MCONF_STRING(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_string, NULL, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_STRING_DFL(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_string, mconf_get_string, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_AVAL(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_aval, NULL, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_AVAL_DFL(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_aval, mconf_get_aval, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_UINT(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_aval, NULL, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_UINT_DFL(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_aval, mconf_get_aval, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_SIZE(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_uint_size, NULL, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_SIZE_DFL(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_uint_size, mconf_get_uint_size, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_VFS_SIZE_DFL(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_vfs_size, mconf_get_vfs_size, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_LOGOPTS(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_logopts, NULL, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

#define MCONF_LOGOPTS_DFL(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_logopts, mconf_get_logopts, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

/* `v' is unsigned - bitmask */
#define MCONF_LOGLEVEL_DFL(v, cn, o, ol, comm) \
	{o, ol, cn, mconf_set_loglevel, mconf_get_loglevel, &v, 0, MOPT_REQUIRED_ARGUMENT, 0, comm }

/* option with callback */
#define MCONF_CB(cb, arg, o, ol, comm) \
	{o, ol, NULL, cb, NULL, arg, 0, MOPT_NO_ARGUMENT, 0, comm}

/* custom parameter (same as MCONF_CB, but can be only set from config parameter but not from the command line) */
#define MCONF_CUSTOM(cb, arg, cn, comm) \
	{0, NULL, cn, cb, NULL, arg, 0, 0, 0, comm}

#ifdef __cplusplus
extern "C" {
#endif /* C++ */

/* Interprets ptr as char** */
int mconf_set_string(struct metad_param *p, const char *section, const char *value);
/* Interprets ptr as int*; value "auto" means "-1" */
int mconf_set_aval(struct metad_param *p, const char *section, const char *value);
/* Interprets ptr as unsigned int*; accepts suffixes 'k', 'M', 'G' */
int mconf_set_uint_size(struct metad_param *p, const char *section, const char *value);
int mconf_set_vfs_size(struct metad_param *p, const char *section, const char *value);
int mconf_set_logopts(struct metad_param *p, const char *section, const char *value);
int mconf_set_loglevel(struct metad_param *p, const char *section, const char *value);

int mconf_get_string(struct metad_param *p, char *buf, int maxlen);
int mconf_get_aval(struct metad_param *p, char *buf, int maxlen);
int mconf_get_uint_size(struct metad_param *p, char *buf, int maxlen);
int mconf_get_uint_size(struct metad_param *p, char *buf, int maxlen);
int mconf_get_vfs_size(struct metad_param *p, char *buf, int maxlen);
int mconf_get_logopts(struct metad_param *p, char *buf, int maxlen);
int mconf_get_loglevel(struct metad_param *p, char *buf, int maxlen);

struct mconf;

struct mconf *mconf_init(const char *cfg_path, int argc, char **argv);
void mconf_help_msg(struct mconf *cf, unsigned width);
int mconf_add(struct mconf *cf, const char *section, struct metad_param *opt, unsigned nopt);
int mconf_parse(struct mconf *cf);
void mconf_free(struct mconf *cf);

#ifdef __cplusplus
}
#endif /* C++ */
