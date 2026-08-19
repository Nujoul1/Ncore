#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nfifo.h"

#define AUTO_GROW_DEFAULT_BYTES (1024 * 1024)

#define NMAX(a, b) ((a) > (b) ? (a) : (b))
#define NMIN(a, b) ((a) > (b) ? (b) : (a))

#define NERROR(e) (-(e))   ///< Returns a negative error code from a POSIX error code, to return from library functions.

struct nfifo {
    uint8_t *buffer;

    size_t elem_size;       // 单个元素的大小(Byte)
    size_t elem_capacity;   // fifo的最大元素个数
    size_t offset_r;
    size_t offset_w;
    
    int is_empty;           // 用来区别队满和队空

    unsigned int flags;
    size_t max_capacity;    // 最大扩容元素个数
};

static inline size_t nsub_wrap(size_t lhs, size_t rhs, size_t cap)
{
    return lhs < rhs ? cap - rhs + lhs : lhs - rhs;
}

static inline size_t nadd_wrap(size_t lhs, size_t rhs, size_t cap)
{
    return lhs >= cap - rhs ? lhs - (cap - rhs) : lhs + rhs;
}

static void nfreep(void *p)
{
    void *tmp = NULL;

    if (!p)
        return;
    
    memcpy(&tmp, p, sizeof(tmp));
    memcpy(p, &(void *){ NULL }, sizeof(tmp));
    free(tmp);
}

struct nfifo *nfifo_alloc(size_t elem_capacity, size_t elem_size, unsigned int flags)
{
    struct nfifo *fifo = NULL;
    void *buf = NULL;

    if (!elem_size)   return NULL;

    fifo = calloc(1, sizeof(struct nfifo));
    if (!fifo)  return NULL;

    if (elem_capacity) {
        buf = calloc(elem_capacity, elem_size);
        if (!buf) {
            free(fifo);
            return NULL;
        }
    }

    fifo->buffer = buf;
    fifo->elem_size = elem_size;
    fifo->elem_capacity = elem_capacity;
    fifo->is_empty = 1;
    fifo->max_capacity = NMAX(AUTO_GROW_DEFAULT_BYTES / elem_size, 1);
    fifo->flags = flags;
    
    return fifo;
}

void nfifo_freep(struct nfifo **f)
{
    if (!f || !*f)
        return;

    nfreep(&((*f)->buffer));
    nfreep(f);
}

size_t nfifo_can_read(const struct nfifo *f)
{
    if (!f) return 0;

    if (f->is_empty) return 0;
    if (f->offset_w == f->offset_r) return f->elem_capacity;
    return nsub_wrap(f->offset_w, f->offset_r, f->elem_capacity);
}

size_t nfifo_can_write(const struct nfifo *f)
{
    if (!f) return 0;

    return f->elem_capacity - nfifo_can_read(f);
}

int nfifo_grow(struct nfifo *f, size_t inc)
{
    uint8_t *new_buf = NULL;
    size_t new_capacity;

    if (!f) return NERROR(EINVAL);
    if (!inc)   return 0;
    if (inc > SIZE_MAX - f->elem_capacity)  return NERROR(EOVERFLOW);
    new_capacity = f->elem_capacity + inc;
    if (new_capacity > SIZE_MAX / f->elem_size) return NERROR(EOVERFLOW);

    new_buf = (uint8_t *)realloc(f->buffer, new_capacity * f->elem_size);
    if (!new_buf)   return NERROR(ENOMEM);
    
    // 有写入数据环绕时
    if (f->offset_w <= f->offset_r && !f->is_empty) {
        size_t copy_count = NMIN(f->offset_w, inc);
        memcpy(new_buf + f->elem_capacity * f->elem_size, new_buf, copy_count * f->elem_size);
        if (copy_count < f->offset_w) {     // 还存在部分环绕数据
            memmove(new_buf, new_buf + copy_count * f->elem_size, 
                    (f->offset_w - copy_count) * f->elem_size);
            f->offset_w -= copy_count;
        } else {
            f->offset_w = copy_count == inc ? 0 : f->elem_capacity + copy_count;
        }
    }

    f->buffer = new_buf;
    f->elem_capacity += inc;

    return 0;
}

static int nfifo_check_space(struct nfifo *f, size_t to_write)
{
    size_t can_write, need_grow, can_grow;

    if (!f) return NERROR(EINVAL);

    can_write = nfifo_can_write(f);
    need_grow = to_write > can_write ? to_write - can_write : 0;
    can_grow = f->max_capacity > f->elem_capacity ? 
                f->max_capacity - f->elem_capacity : 0;

    if (!need_grow)
        return 0;

    if (f->flags & N_FIFO_FLAG_AUTO_GROW && need_grow <= can_grow) {
        size_t inc = need_grow <= can_grow / 2 ? 2 * need_grow : can_grow;
        return nfifo_grow(f, inc);
    }

    return NERROR(ENOSPC);
}

static int nfifo_write_common(struct nfifo *f, const uint8_t *buf, size_t *elem_count,
                             nfifo_cb read_cb, void *opaque)
{
    int ret = 0;
    size_t to_write;

    if (!f || !elem_count) return NERROR(EINVAL);

    to_write = *elem_count;
    ret = nfifo_check_space(f, to_write);
    if (ret < 0) return ret;

    while (to_write > 0) {
        size_t len = NMIN(to_write, f->elem_capacity - f->offset_w);
        uint8_t *w_ptr = f->buffer + f->offset_w * f->elem_size;

        if (read_cb) {
            ret = read_cb(opaque, w_ptr, &len);
            if (ret < 0 || 0 == len) {
                break;
            }
        } else {
            memcpy(w_ptr, buf, len * f->elem_size);
            buf += len * f->elem_size;
        }
        f->offset_w = nadd_wrap(f->offset_w, len, f->elem_capacity);
        to_write -= len;
    }

    if (to_write != *elem_count)
        f->is_empty = 0;
    *elem_count -= to_write;

    return ret;
}

int nfifo_write(struct nfifo *f, const void *buf, size_t elem_count)
{
    if (!f || !buf) return NERROR(EINVAL);

    return nfifo_write_common(f, buf, &elem_count, NULL, NULL);
}

int nfifo_write_from_cb(struct nfifo *f, nfifo_cb read_cb, void *opaque, size_t *elem_count)
{
    if (!f || !elem_count || !read_cb) return NERROR(EINVAL);

    return nfifo_write_common(f, NULL, elem_count, read_cb, opaque);
}

static int nfifo_peek_common(const struct nfifo *f, uint8_t *buf, size_t *elem_count,
                            size_t offset, nfifo_cb write_cb, void *opaque)
{
    int ret = 0;
    size_t can_read, to_read, offset_r;

    if (!f || !elem_count)   return NERROR(EINVAL);

    can_read = nfifo_can_read(f);
    to_read = *elem_count;

    if ((offset > can_read) || (to_read > (can_read - offset))) {
        *elem_count = 0;
        return NERROR(EINVAL);
    }

    offset_r = nadd_wrap(f->offset_r, offset, f->elem_capacity);

    while (to_read > 0) {
        size_t len = NMIN(to_read, f->elem_capacity - offset_r);
        uint8_t *r_ptr = f->buffer + offset_r * f->elem_size;
        
        if (write_cb) {
            ret = write_cb(opaque, r_ptr, &len);
            if (ret < 0 || 0 == len)
                break;
        } else {
            memcpy(buf, r_ptr, len * f->elem_size);
            buf += len * f->elem_size;
        }

        offset_r = nadd_wrap(offset_r, len, f->elem_capacity);
        to_read -= len;
    }

    *elem_count -= to_read;

    return ret;
}

void nfifo_drain(struct nfifo *f, size_t elem_count)
{
    size_t can_read;

    if (!f) return;

    can_read = nfifo_can_read(f);

    if (elem_count > can_read) return;

    if (elem_count == can_read) {
        f->is_empty = 1;
    }

    f->offset_r = nadd_wrap(f->offset_r, elem_count, f->elem_capacity);
}

int nfifo_read(struct nfifo *f, void *buf, size_t elem_count)
{
    if (!f || !buf) return NERROR(EINVAL);

    int ret = nfifo_peek_common(f, buf, &elem_count, 0, NULL, NULL);
    nfifo_drain(f, elem_count);
    return ret;
}

int nfifo_read_to_cb(struct nfifo *f, nfifo_cb write_cb, void *opaque, size_t *elem_count)
{
    if (!f || !elem_count || !write_cb) return NERROR(EINVAL);

    int ret = nfifo_peek_common(f, NULL, elem_count, 0, write_cb, opaque);
    nfifo_drain(f, *elem_count);
    return ret;
}

int nfifo_peek(struct nfifo *f, void *buf, size_t elem_count, size_t offset)
{
    if (!f || !buf) return NERROR(EINVAL);

    int ret = nfifo_peek_common(f, buf, &elem_count, offset, NULL, NULL);
    return ret;
}

int nfifo_peek_to_cb(struct nfifo *f, nfifo_cb write_cb, void *opaque, size_t *elem_count, size_t offset)
{
    if (!f || !elem_count || !write_cb) return NERROR(EINVAL);

    int ret = nfifo_peek_common(f, NULL, elem_count, offset, write_cb, opaque);
    return ret;
}

void nfifo_set_auto_grow_capacity(struct nfifo *f, size_t capacity)
{
    if (!f) return;

    f->max_capacity = capacity;
}

size_t nfifo_get_elem_size(const struct nfifo *f)
{
    if (!f) return 0;

    return f->elem_size;
}

void nfifo_reset(struct nfifo *f)
{
    if (!f) return;

    f->offset_r = f->offset_w = 0;
    f->is_empty = 1;
}