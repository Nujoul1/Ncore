#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ncore/nfifo.h"

enum { MODEL_CAP = 200000, STEPS = 50000 };

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n",                     \
                    __FILE__, __LINE__, #expr);                                \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (0)

static uint32_t rng_state = 0x12345678u;

static uint32_t next_rand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

struct cb_ctx {
    int *data;
    size_t pos;
    size_t available;
    size_t max_chunk;
    size_t calls;
    size_t fail_call;
    int fail_ret;
};

static int source_cb(void *opaque, void *buf, size_t *nb_elems)
{
    struct cb_ctx *ctx = opaque;
    size_t left;
    size_t n;

    ctx->calls++;
    if (ctx->fail_call && ctx->calls == ctx->fail_call)
        return ctx->fail_ret;

    left = ctx->available - ctx->pos;
    n = *nb_elems;
    if (n > ctx->max_chunk)
        n = ctx->max_chunk;
    if (n > left)
        n = left;

    memcpy(buf, ctx->data + ctx->pos, n * sizeof(int));
    ctx->pos += n;
    *nb_elems = n;
    return 0;
}

static int sink_cb(void *opaque, void *buf, size_t *nb_elems)
{
    struct cb_ctx *ctx = opaque;
    size_t left;
    size_t n;

    ctx->calls++;
    if (ctx->fail_call && ctx->calls == ctx->fail_call)
        return ctx->fail_ret;

    left = ctx->available - ctx->pos;
    n = *nb_elems;
    if (n > ctx->max_chunk)
        n = ctx->max_chunk;
    if (n > left)
        n = left;

    memcpy(ctx->data + ctx->pos, buf, n * sizeof(int));
    ctx->pos += n;
    *nb_elems = n;
    return 0;
}

static int stop_cb(void *opaque, void *buf, size_t *nb_elems)
{
    (void)opaque;
    (void)buf;
    *nb_elems = 0;
    return 0;
}

struct free_ctx {
    size_t count;
};

static int free_items_cb(void *opaque, void *buf, size_t *nb_elems)
{
    struct free_ctx *ctx = opaque;
    void **items = buf;

    for (size_t i = 0; i < *nb_elems; i++) {
        free(items[i]);
        items[i] = NULL;
        ctx->count++;
    }
    return 0;
}

static void test_invalid_arguments_and_zero_capacity(void)
{
    struct nfifo *fifo = NULL;
    int value = 7;
    int output = 0;
    size_t n = 1;

    CHECK(nfifo_alloc(1, 0, 0) == NULL);
    nfifo_freep(NULL);
    nfifo_freep(&fifo);

    fifo = nfifo_alloc(0, sizeof(int), 0);
    CHECK(fifo != NULL);
    CHECK(nfifo_can_read(fifo) == 0);
    CHECK(nfifo_can_write(fifo) == 0);
    CHECK(nfifo_get_elem_size(fifo) == sizeof(int));
    CHECK(nfifo_write(fifo, &value, 1) == -ENOSPC);
    CHECK(nfifo_grow(fifo, 2) == 0);
    CHECK(nfifo_can_write(fifo) == 2);
    CHECK(nfifo_write(fifo, &value, 1) == 0);
    CHECK(nfifo_read(fifo, &output, 1) == 0);
    CHECK(output == value);
    CHECK(nfifo_grow(fifo, SIZE_MAX) == -EOVERFLOW);

    CHECK(nfifo_write(NULL, &value, 1) == -EINVAL);
    CHECK(nfifo_write(fifo, NULL, 1) == -EINVAL);
    CHECK(nfifo_read(NULL, &output, 1) == -EINVAL);
    CHECK(nfifo_read(fifo, NULL, 1) == -EINVAL);
    CHECK(nfifo_peek(NULL, &output, 1, 0) == -EINVAL);
    CHECK(nfifo_peek(fifo, NULL, 1, 0) == -EINVAL);
    CHECK(nfifo_drain(NULL, 0) == -EINVAL);
    CHECK(nfifo_write_from_cb(fifo, NULL, NULL, &n) == -EINVAL);
    CHECK(nfifo_read_to_cb(fifo, NULL, NULL, &n) == -EINVAL);
    CHECK(nfifo_peek_to_cb(fifo, NULL, NULL, &n, 0) == -EINVAL);

    nfifo_freep(&fifo);
    CHECK(fifo == NULL);
}

static void test_fixed_capacity_and_wrap(void)
{
    const int first[] = { 1, 2, 3 };
    const int second[] = { 4, 5, 6 };
    const int expected[] = { 3, 4, 5, 6 };
    int output[4] = { 0 };
    struct nfifo *fifo = nfifo_alloc(4, sizeof(int), 0);

    CHECK(fifo != NULL);
    CHECK(nfifo_write(fifo, first, 3) == 0);
    CHECK(nfifo_can_read(fifo) == 3);
    CHECK(nfifo_can_write(fifo) == 1);
    CHECK(nfifo_read(fifo, output, 2) == 0);
    CHECK(output[0] == 1 && output[1] == 2);

    CHECK(nfifo_write(fifo, second, 3) == 0);
    CHECK(nfifo_can_read(fifo) == 4);
    CHECK(nfifo_can_write(fifo) == 0);
    CHECK(nfifo_write(fifo, second, 1) == -ENOSPC);
    CHECK(nfifo_peek(fifo, output, 4, 0) == 0);
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
    CHECK(nfifo_can_read(fifo) == 4);
    CHECK(nfifo_read(fifo, output, 4) == 0);
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
    CHECK(nfifo_can_read(fifo) == 0);
    CHECK(nfifo_drain(fifo, 1) == -EINVAL);

    CHECK(nfifo_write(fifo, first, 3) == 0);
    nfifo_reset(fifo);
    CHECK(nfifo_can_read(fifo) == 0);
    CHECK(nfifo_can_write(fifo) == 4);

    nfifo_freep(&fifo);
    CHECK(fifo == NULL);
}

static struct nfifo *make_wrapped_full_fifo(void)
{
    const int first[] = { 1, 2, 3 };
    const int second[] = { 4, 5, 6 };
    int discarded[2];
    struct nfifo *fifo = nfifo_alloc(4, sizeof(int), 0);

    CHECK(fifo != NULL);
    CHECK(nfifo_write(fifo, first, 3) == 0);
    CHECK(nfifo_read(fifo, discarded, 2) == 0);
    CHECK(nfifo_write(fifo, second, 3) == 0);
    CHECK(nfifo_can_read(fifo) == 4);
    return fifo;
}

static void check_wrapped_grow(size_t increment)
{
    const int expected[] = { 3, 4, 5, 6 };
    int output[4] = { 0 };
    struct nfifo *fifo = make_wrapped_full_fifo();

    CHECK(nfifo_grow(fifo, increment) == 0);
    CHECK(nfifo_can_read(fifo) == 4);
    CHECK(nfifo_can_write(fifo) == increment);
    CHECK(nfifo_read(fifo, output, 4) == 0);
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
    nfifo_freep(&fifo);
}

static void test_growth(void)
{
    const int input[] = { 10, 11, 12 };
    int output[3] = { 0 };
    struct nfifo *fifo = nfifo_alloc(0, sizeof(int), NFIFO_FLAG_AUTO_GROW);

    CHECK(fifo != NULL);
    CHECK(nfifo_write(fifo, input, 3) == 0);
    CHECK(nfifo_can_read(fifo) == 3);
    CHECK(nfifo_read(fifo, output, 3) == 0);
    CHECK(memcmp(output, input, sizeof(input)) == 0);
    nfifo_freep(&fifo);

    check_wrapped_grow(1);
    check_wrapped_grow(2);
    check_wrapped_grow(3);
}

static void test_callbacks(void)
{
    int input[] = { 11, 12, 13, 14, 15 };
    int output[5] = { 0 };
    struct cb_ctx source = {
        .data = input, .available = 5, .max_chunk = 2
    };
    struct cb_ctx sink = {
        .data = output, .available = 3, .max_chunk = 2
    };
    struct nfifo *fifo = nfifo_alloc(2, sizeof(int), NFIFO_FLAG_AUTO_GROW);
    size_t n = 8;

    CHECK(fifo != NULL);
    CHECK(nfifo_write_from_cb(fifo, source_cb, &source, &n) == 0);
    CHECK(n == 5);
    CHECK(source.calls == 4);
    CHECK(nfifo_can_read(fifo) == 5);

    n = 5;
    CHECK(nfifo_read_to_cb(fifo, sink_cb, &sink, &n) == 0);
    CHECK(n == 3);
    CHECK(memcmp(output, input, 3 * sizeof(int)) == 0);
    CHECK(nfifo_can_read(fifo) == 2);

    memset(output, 0, sizeof(output));
    sink = (struct cb_ctx){
        .data = output, .available = 5, .max_chunk = 1
    };
    n = 2;
    CHECK(nfifo_peek_to_cb(fifo, sink_cb, &sink, &n, 0) == 0);
    CHECK(n == 2);
    CHECK(output[0] == 14 && output[1] == 15);
    CHECK(nfifo_can_read(fifo) == 2);

    nfifo_freep(&fifo);
    CHECK(fifo == NULL);
}

static void test_callback_stop_and_error(void)
{
    int input[] = { 21, 22, 23, 24, 25 };
    int output[5] = { 0 };
    size_t n;
    struct nfifo *fifo = nfifo_alloc(8, sizeof(int), 0);
    struct cb_ctx source = {
        .data = input, .available = 5, .max_chunk = 2,
        .fail_call = 2, .fail_ret = -EIO
    };
    struct cb_ctx sink;

    CHECK(fifo != NULL);
    n = 5;
    CHECK(nfifo_write_from_cb(fifo, source_cb, &source, &n) == -EIO);
    CHECK(n == 2);
    CHECK(nfifo_can_read(fifo) == 2);
    CHECK(nfifo_read(fifo, output, 2) == 0);
    CHECK(output[0] == 21 && output[1] == 22);

    n = 3;
    CHECK(nfifo_write_from_cb(fifo, stop_cb, NULL, &n) == 0);
    CHECK(n == 0);
    CHECK(nfifo_can_read(fifo) == 0);

    CHECK(nfifo_write(fifo, input, 5) == 0);
    sink = (struct cb_ctx){
        .data = output, .available = 5, .max_chunk = 2,
        .fail_call = 2, .fail_ret = -EIO
    };
    n = 5;
    CHECK(nfifo_read_to_cb(fifo, sink_cb, &sink, &n) == -EIO);
    CHECK(n == 2);
    CHECK(nfifo_can_read(fifo) == 3);
    CHECK(nfifo_read(fifo, output, 3) == 0);
    CHECK(memcmp(output, input + 2, 3 * sizeof(int)) == 0);

    nfifo_freep(&fifo);
}

static void test_mutable_read_callback(void)
{
    void *items[3];
    struct free_ctx ctx = { 0 };
    struct nfifo *fifo = nfifo_alloc(3, sizeof(void *), 0);
    size_t n = 3;

    CHECK(fifo != NULL);
    for (size_t i = 0; i < 3; i++) {
        items[i] = malloc(16);
        CHECK(items[i] != NULL);
        memset(items[i], (int)i, 16);
    }

    CHECK(nfifo_write(fifo, items, 3) == 0);
    CHECK(nfifo_read_to_cb(fifo, free_items_cb, &ctx, &n) == 0);
    CHECK(n == 3);
    CHECK(ctx.count == 3);
    CHECK(nfifo_can_read(fifo) == 0);
    nfifo_freep(&fifo);
}

static void test_randomized_operations(void)
{
    struct nfifo *fifo = nfifo_alloc(3, sizeof(int), NFIFO_FLAG_AUTO_GROW);
    int *model = malloc(MODEL_CAP * sizeof(*model));
    size_t model_len = 0;
    int next_value = 1;

    CHECK(fifo != NULL);
    CHECK(model != NULL);
    rng_state = 0x12345678u;

    for (size_t step = 0; step < STEPS; step++) {
        unsigned op = next_rand() % 5u;

        if (op <= 1 && model_len + 8 < MODEL_CAP) {
            size_t n = next_rand() % 8u + 1u;
            int input[8];

            for (size_t i = 0; i < n; i++) {
                input[i] = next_value++;
                model[model_len + i] = input[i];
            }

            CHECK(nfifo_write(fifo, input, n) == 0);
            model_len += n;
        } else if (op == 2 && model_len) {
            size_t n = next_rand() % model_len + 1u;
            int *output = malloc(n * sizeof(*output));

            CHECK(output != NULL);
            CHECK(nfifo_read(fifo, output, n) == 0);
            CHECK(memcmp(output, model, n * sizeof(*output)) == 0);
            memmove(model, model + n, (model_len - n) * sizeof(*model));
            model_len -= n;
            free(output);
        } else if (op == 3 && model_len) {
            size_t offset = next_rand() % model_len;
            size_t n = next_rand() % (model_len - offset) + 1u;
            int *output = malloc(n * sizeof(*output));

            CHECK(output != NULL);
            CHECK(nfifo_peek(fifo, output, n, offset) == 0);
            CHECK(memcmp(output, model + offset, n * sizeof(*output)) == 0);
            free(output);
        } else if (op == 4 && model_len) {
            size_t n = next_rand() % (model_len + 1u);

            CHECK(nfifo_drain(fifo, n) == 0);
            memmove(model, model + n, (model_len - n) * sizeof(*model));
            model_len -= n;
        }

        CHECK(nfifo_can_read(fifo) == model_len);
    }

    if (model_len) {
        int *output = malloc(model_len * sizeof(*output));

        CHECK(output != NULL);
        CHECK(nfifo_read(fifo, output, model_len) == 0);
        CHECK(memcmp(output, model, model_len * sizeof(*output)) == 0);
        free(output);
    }

    CHECK(nfifo_can_read(fifo) == 0);
    nfifo_freep(&fifo);
    CHECK(fifo == NULL);
    free(model);
}

int main(void)
{
    test_invalid_arguments_and_zero_capacity();
    test_fixed_capacity_and_wrap();
    test_growth();
    test_callbacks();
    test_callback_stop_and_error();
    test_mutable_read_callback();
    test_randomized_operations();
    puts("nfifo tests passed");
    return 0;
}
