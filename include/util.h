#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <stddef.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define GET_INT16(p) (((uint8_t*)(p))[0] | (((uint8_t*)(p))[1] << 8))

typedef struct {
	union {
		struct sockaddr		name;
		struct sockaddr_in	in_name;
		struct sockaddr_un	un_name;
	};
	socklen_t	namelen;
	char		*a_addr;
} addr_t;

#define ADDR_T_SIZE offsetof(addr_t, namelen)

#ifdef __cplusplus
extern "C" {
#endif /* C++ */

int set_nonblock(int sock,int value);
int set_nodelay(int sock,int value);
int nonblock_connect(int sock,struct sockaddr *sa,socklen_t sl,int timeout);
int init_addr(addr_t *addr,const char *str);
char *addr2a(const addr_t *a, char *buf, unsigned *size);
void perror_fatal(const char *what, ...);
void daemonize(int do_daemonize, const char *pidfile, const char *run_as);
void close_stdx();
char *read_file_to_string(const char *fname);
uint32_t murmur_hash2(const void *data, unsigned len);

int ignore_signal(int sig);

unsigned clock_diff(const struct timespec *start, const struct timespec *stop);
void hexdump(FILE *out, unsigned rel, const void *data, unsigned size);

int writev_full(int fd, struct iovec *iov, unsigned iov_cnt);

/* read/write with timeout on non-blocking descriptors */
int read_ms(int fd, void *buf, unsigned len, unsigned ms);
int write_ms(int fd, const void *buf, unsigned len, unsigned ms);
int write_full_ms(int fd, const void *buf, unsigned len, unsigned ms);
int writev_full_ms(int fd, struct iovec *iov, unsigned iov_cnt, unsigned ms);

/* misc functions */
unsigned xdigit2i(char c);
unsigned uri_decode(void *dst, unsigned dst_len, const void *src, unsigned src_len);

/* unicode routines */
unsigned u16_foldcase(unsigned c);
unsigned u8_foldcase(const uint8_t *in, uint8_t *out);
int u8_foldcase_str(const char *in, unsigned len, char *out, unsigned *olen);
unsigned u8_strlen(const char *str);
bool u8_is_valid(const char *str);

#ifdef __cplusplus
}
#endif /* C++ */

