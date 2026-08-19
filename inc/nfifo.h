#ifndef N_FIFO_H
#define N_FIFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <errno.h>

#define N_FIFO_FLAG_AUTO_GROW       (1 << 0)

/**
 * Process a contiguous block of FIFO elements.
 *
 * On entry, *nb_elems is the maximum number of elements available in buf.
 * The callback may process fewer elements, but must never increase
 * *nb_elems. Before returning, it must set *nb_elems to the number of
 * elements actually processed.
 *
 * @param opaque    User-provided context passed through unchanged.
 * @param buf       FIFO internal contiguous buffer.
 * @param nb_elems  In: maximum elements available; out: elements processed.
 * @return 0 on success, or a negative error code on failure.
 */
typedef int nfifo_cb(void *opaque, void *buf, size_t *nb_elems);

struct nfifo;

struct nfifo *nfifo_alloc(size_t elem_capacity, size_t elem_size, unsigned int flags);
void nfifo_freep(struct nfifo **f);

size_t nfifo_can_read(const struct nfifo *f);
size_t nfifo_can_write(const struct nfifo *f);

int nfifo_write(struct nfifo *f, const void *buf, size_t elem_count);
int nfifo_write_from_cb(struct nfifo *f, nfifo_cb read_cb, void *opaque, size_t *elem_count);

int nfifo_read(struct nfifo *f, void *buf, size_t elem_count);
int nfifo_read_to_cb(struct nfifo *f, nfifo_cb write_cb, void *opaque, size_t *elem_count);

int nfifo_peek(struct nfifo *f, void *buf, size_t elem_count, size_t offset);
int nfifo_peek_to_cb(struct nfifo *f, nfifo_cb write_cb, void *opaque, size_t *elem_count, size_t offset);

void nfifo_set_auto_grow_capacity(struct nfifo *f, size_t capacity);
size_t nfifo_get_elem_size(const struct nfifo *f);
int nfifo_grow(struct nfifo *f, size_t inc);

void nfifo_drain(struct nfifo *f, size_t size);
void nfifo_reset(struct nfifo *f);

#ifdef __cplusplus
}
#endif
#endif
