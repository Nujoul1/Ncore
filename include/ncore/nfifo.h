#ifndef N_FIFO_H
#define N_FIFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define NFIFO_FLAG_AUTO_GROW       (1 << 0)

/**
 * @brief 在 FIFO 的一段连续内存与外部数据源或接收端之间传输元素。
 *
 * @param[in] opaque        调用者提供的上下文指针
 * @param[in,out] buf       FIFO 当前提供的连续内存段。write_cb 向其中写入元素；
 *                          read_cb 从中读取或处理元素，并允许修改元素内容。
 * @param[in,out] nb_elems  输入为本次最多可处理的元素数，输出为实际处理数。
 *
 * @return 非负值表示成功，负值表示失败；返回值由 FIFO 接口原样返回。
 *
 * @note 回调可能因 FIFO 环绕而被调用多次；buf 仅在本次调用期间有效。
 * @note *nb_elems 不得大于输入值；设为 0 可正常停止本次传输。
 * @warning 返回负值时当前内存段视为未处理，因此不能处理完当前段后再返回
 *          负值；先前调用已处理的元素仍然有效。
 */
typedef int nfifo_cb(void *opaque, void *buf, size_t *nb_elems);

struct nfifo;

struct nfifo *nfifo_alloc(size_t elem_cap, size_t elem_size, unsigned int flags);
void nfifo_freep(struct nfifo **f);

size_t nfifo_can_read(const struct nfifo *f);
size_t nfifo_can_write(const struct nfifo *f);

int nfifo_write(struct nfifo *f, const void *buf, size_t elem_count);
int nfifo_write_from_cb(struct nfifo *f, nfifo_cb write_cb, void *opaque, size_t *elem_count);

int nfifo_read(struct nfifo *f, void *buf, size_t elem_count);
int nfifo_read_to_cb(struct nfifo *f, nfifo_cb read_cb, void *opaque, size_t *elem_count);

int nfifo_peek(const struct nfifo *f, void *buf, size_t elem_count, size_t offset);
int nfifo_peek_to_cb(const struct nfifo *f, nfifo_cb read_cb, void *opaque, size_t *elem_count, size_t offset);

void nfifo_set_max_elem_cap(struct nfifo *f, size_t capacity);
size_t nfifo_get_elem_size(const struct nfifo *f);
int nfifo_grow(struct nfifo *f, size_t inc);

int nfifo_drain(struct nfifo *f, size_t size);
void nfifo_reset(struct nfifo *f);

#ifdef __cplusplus
}
#endif
#endif
