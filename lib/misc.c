#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>

void perror_fatal(const char *what, ...)
{
	va_list ap;
	int err = errno;
	va_start(ap, what);
	vfprintf(stderr, what, ap);
	va_end(ap);
	fprintf(stderr, ": %s\n", strerror(err));
	exit(EXIT_FAILURE);
}

int ignore_signal(int sig)
{
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if (sigemptyset(&sa.sa_mask) == -1 || sigaction(sig,  &sa, NULL) == -1)
		return -1;
	return 0;
}

char *
read_file_to_string(const char *fname)
{
	struct stat s;
	if (stat(fname,&s) == 0) {
		int fd, pos;
		char *str = malloc (s.st_size+1);
		fd = open(fname,O_RDONLY);
		for (pos = 0; pos < s.st_size; ) {
			int bytes = read (fd,str,s.st_size);
			if (bytes < 0) {
				if (errno == EAGAIN || errno == EINTR) continue;
				perror ("read_file_to_string: read");
				abort();
			}
			if (bytes == 0) break;
			pos += bytes;
		}
		str[pos] = 0;
		close(fd);
		return str;
	}
	return NULL;
}

/** Returns difference between stop and start in microseconds */
unsigned
clock_diff(const struct timespec *start, const struct timespec *stop)
{
	unsigned ms;
	if (stop->tv_nsec < start->tv_nsec) {
		ms = (stop->tv_sec - start->tv_sec - 1) * 1000000 + (1000000000 + stop->tv_nsec - start->tv_nsec) / 1000;
	} else {
		ms = (stop->tv_sec - start->tv_sec) * 1000000 + (stop->tv_nsec - start->tv_nsec) / 1000;
	}
	return ms;
}

/* Print dump (like hexdump -C) */
void
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

uint32_t
murmur_hash2(const void *data, unsigned len)
{
	const uint8_t *d = (const uint8_t*)data;
	uint32_t  h, k;

	h = 0 ^ len;

	while (len >= 4) {
		k  = d[0];
		k |= d[1] << 8;
		k |= d[2] << 16;
		k |= d[3] << 24;

		k *= 0x5bd1e995;
		k ^= k >> 24;
		k *= 0x5bd1e995;

		h *= 0x5bd1e995;
		h ^= k;

		d += 4;
		len -= 4;
	}

	switch (len) {
	case 3:
		h ^= d[2] << 16;
	case 2:
		h ^= d[1] << 8;
	case 1:
		h ^= d[0];
		h *= 0x5bd1e995;
	}

	h ^= h >> 13;
	h *= 0x5bd1e995;
	h ^= h >> 15;

	return h;
}

/* Convert hex digit character [0-9A-Fa-f] to number */
unsigned
xdigit2i(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return c - 'a' + 10;
}

/* URI decode and copy no more than dst_len bytes to dst.
 * If src string is larger tnat dst buffer, then only dst_len bytes will
 * be copied but still whole length will be returned.
 * So if result >= dst_len, you will need to allocate buffer dynamically and
 * call uri_decode function again. */
unsigned
uri_decode(void *dst, unsigned dst_len, const void *src, unsigned src_len)
{
	char *d = dst, *de = d + dst_len;
	const char *s = src, *se = s + src_len;

	while (s < se) {
		if (d >= de)
			goto skip_rest;
		if (*s == '%' && s + 2 < se && isxdigit(s[1]) && isxdigit(s[2])) {
			*d++ = (xdigit2i(s[1]) << 4) | xdigit2i(s[2]);
			s += 3;
		} else {
			*d++ = *s++;
		}
	}
	return d - (char*)dst;
skip_rest:
	while (s < se) {
		if (*s == '%' && s + 2 < se && isxdigit(s[1]) && isxdigit(s[2])) {
			d++;
			s += 3;
		} else {
			d++; s++;
		}
	}
	return d - (char*)dst;
}

