#pragma once

#define ring_buffer_is_empty(r) ((r)->head == (r)->tail)
#define ring_buffer_is_full(r) (((r)->head + 1) % ((r)->capacity + 1) == r->tail)
//#define ring_buffer_free(r) free(r)

/* Get number of bytes stored in ring_buffer */
#define ring_buffer_bytes(r) ((r)->head >= (r)->tail ? (r)->head - (r)->tail : (r)->capacity + 1 + (r)->head - (r)->tail)

/* Get exactly one character from non-empty ring_buffer */
#define ring_buffer_getchar(r) ({char c = (r)->buf[(r)->tail]; (r)->tail = ((r)->tail + 1) % ((r)->capacity + 1); c;})

/* Construct struct iovec suitable to fill ring_buffer with data (tail & head must be null) */
#define ring_buffer_iov(r, v) do { (v)->iov_base = (r)->buf; (v)->iov_len = (r)->capacity; } while (0)

/* Set ring buffer's head and tail pointers */
#define ring_buffer_set(r, skip, total) do { (r)->head = total; (r)->tail = skip; } while (0)

/* Inform ring buffer that it actually received nbytes bytes data */
#define ring_buffer_adjust(r, nbytes) (r)->head += nbytes

struct ring_buffer {
	unsigned			head;
	unsigned			tail;
	unsigned			capacity;
	char				*buf;
};

void ring_buffer_init(struct ring_buffer *b, void *buf, unsigned size);
//struct ring_buffer* ring_buffer_alloc(unsigned capacity);
int ring_buffer_recv(int fd, struct ring_buffer *b);
unsigned ring_buffer_move_to(struct ring_buffer *b, void *buf, unsigned size);
unsigned ring_buffer_move_till(struct ring_buffer *b, void *buf, unsigned size, char c);
unsigned ring_buffer_skip(struct ring_buffer *b, unsigned size);
//int ring_buffer_send(int fd, struct ring_buffer *b, unsigned capacity);

