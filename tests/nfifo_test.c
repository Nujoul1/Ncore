#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfifo.h"

enum { MODEL_CAP = 200000, STEPS = 50000 };

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
};

static int source_cb(void *opaque, void *buf, size_t *nb_elems)
{
    struct cb_ctx *ctx = opaque;
    size_t left = ctx->available - ctx->pos;
    size_t n = *nb_elems;

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
    size_t left = ctx->available - ctx->pos;
    size_t n = *nb_elems;

    if (n > ctx->max_chunk)
        n = ctx->max_chunk;
    if (n > left)
        n = left;

    memcpy(ctx->data + ctx->pos, buf, n * sizeof(int));
    ctx->pos += n;
    *nb_elems = n;
    return 0;
}

static void test_randomized_operations(void)
{
    struct nfifo *fifo = nfifo_alloc(3, sizeof(int), N_FIFO_FLAG_AUTO_GROW);
    int *model = malloc(MODEL_CAP * sizeof(*model));
    size_t model_len = 0;
    int next_value = 1;

    assert(fifo);
    assert(model);

    for (size_t step = 0; step < STEPS; step++) {
        unsigned op = next_rand() % 5u;

        if (op <= 1 && model_len + 8 < MODEL_CAP) {
            size_t n = next_rand() % 8u + 1u;
            int input[8];

            for (size_t i = 0; i < n; i++) {
                input[i] = next_value++;
                model[model_len + i] = input[i];
            }

            assert(nfifo_write(fifo, input, n) == 0);
            model_len += n;
        } else if (op == 2 && model_len) {
            size_t n = next_rand() % model_len + 1u;
            int *output = malloc(n * sizeof(*output));

            assert(output);
            assert(nfifo_read(fifo, output, n) == 0);
            assert(memcmp(output, model, n * sizeof(*output)) == 0);

            memmove(model, model + n, (model_len - n) * sizeof(*model));
            model_len -= n;
            free(output);
        } else if (op == 3 && model_len) {
            size_t offset = next_rand() % model_len;
            size_t n = next_rand() % (model_len - offset) + 1u;
            int *output = malloc(n * sizeof(*output));

            assert(output);
            assert(nfifo_peek(fifo, output, n, offset) == 0);
            assert(memcmp(output, model + offset, n * sizeof(*output)) == 0);
            free(output);
        } else if (op == 4 && model_len) {
            size_t n = next_rand() % (model_len + 1u);

            nfifo_drain(fifo, n);
            memmove(model, model + n, (model_len - n) * sizeof(*model));
            model_len -= n;
        }

        assert(nfifo_can_read(fifo) == model_len);
    }

    if (model_len) {
        int *output = malloc(model_len * sizeof(*output));

        assert(output);
        assert(nfifo_read(fifo, output, model_len) == 0);
        assert(memcmp(output, model, model_len * sizeof(*output)) == 0);
        free(output);
    }

    assert(nfifo_can_read(fifo) == 0);
    nfifo_freep(&fifo);
    assert(fifo == NULL);
    free(model);
}

static void test_callbacks(void)
{
    int input[] = { 11, 12, 13, 14, 15 };
    int output[5] = { 0 };
    struct cb_ctx source = { input, 0, 5, 2 };
    struct cb_ctx sink = { output, 0, 3, 2 };
    struct nfifo *fifo = nfifo_alloc(2, sizeof(int), N_FIFO_FLAG_AUTO_GROW);
    size_t n = 8;

    assert(fifo);
    assert(nfifo_write_from_cb(fifo, source_cb, &source, &n) == 0);
    assert(n == 5);
    assert(nfifo_can_read(fifo) == 5);

    n = 5;
    assert(nfifo_read_to_cb(fifo, sink_cb, &sink, &n) == 0);
    assert(n == 3);
    assert(memcmp(output, input, 3 * sizeof(int)) == 0);
    assert(nfifo_can_read(fifo) == 2);

    memset(output, 0, sizeof(output));
    sink = (struct cb_ctx){ output, 0, 5, 1 };
    n = 2;
    assert(nfifo_peek_to_cb(fifo, sink_cb, &sink, &n, 0) == 0);
    assert(n == 2);
    assert(output[0] == 14 && output[1] == 15);
    assert(nfifo_can_read(fifo) == 2);

    nfifo_freep(&fifo);
    assert(fifo == NULL);
}

int main(void)
{
    test_randomized_operations();
    test_callbacks();
    puts("nfifo tests passed");
    return 0;
}
