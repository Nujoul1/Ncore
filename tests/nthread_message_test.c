#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nthread_message.h"

struct thread_call {
    struct nthread_message_queue *mq;
    int value;
    int ret;
    atomic_int started;
};

static atomic_int freed_count;

static void *blocking_send(void *opaque)
{
    struct thread_call *call = opaque;

    atomic_store(&call->started, 1);
    call->ret = nthread_message_queue_send(call->mq, &call->value, 0);
    return NULL;
}

static void *blocking_recv(void *opaque)
{
    struct thread_call *call = opaque;

    atomic_store(&call->started, 1);
    call->ret = nthread_message_queue_recv(call->mq, &call->value, 0);
    return NULL;
}

static void wait_started(struct thread_call *call)
{
    while (!atomic_load(&call->started))
        sched_yield();
}

static void free_pointer_message(void *msg)
{
    void **slot = msg;

    free(*slot);
    *slot = NULL;
    atomic_fetch_add(&freed_count, 1);
}

static void test_invalid_arguments(void)
{
    struct nthread_message_queue *mq = NULL;

    assert(nthread_message_queue_alloc(NULL, 1, sizeof(int)) == -EINVAL);
    assert(nthread_message_queue_alloc(&mq, 0, sizeof(int)) == -EINVAL);
    assert(nthread_message_queue_alloc(&mq, 1, 0) == -EINVAL);
    assert(mq == NULL);

    nthread_message_queue_free(NULL);
    nthread_message_queue_free(&mq);
}

static void test_nonblocking(void)
{
    struct nthread_message_queue *mq = NULL;
    int input = 11;
    int output = 0;

    assert(nthread_message_queue_alloc(&mq, 1, sizeof(int)) == 0);
    assert(nthread_message_queue_recv(mq, &output,
                                      NTHREAD_MESSAGE_NONBLOCK) == -EAGAIN);
    assert(nthread_message_queue_send(mq, &input, 0) == 0);
    assert(nthread_message_queue_send(mq, &input,
                                      NTHREAD_MESSAGE_NONBLOCK) == -EAGAIN);
    assert(nthread_message_queue_recv(mq, &output, 0) == 0);
    assert(output == input);
    nthread_message_queue_free(&mq);
    assert(mq == NULL);
}

static void test_data_wakeups(void)
{
    struct nthread_message_queue *mq = NULL;
    struct thread_call call = { 0 };
    pthread_t thread;
    int value;

    assert(nthread_message_queue_alloc(&mq, 1, sizeof(int)) == 0);

    call.mq = mq;
    assert(pthread_create(&thread, NULL, blocking_recv, &call) == 0);
    wait_started(&call);
    value = 21;
    assert(nthread_message_queue_send(mq, &value, 0) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(call.ret == 0);
    assert(call.value == 21);

    value = 31;
    assert(nthread_message_queue_send(mq, &value, 0) == 0);
    call = (struct thread_call){ .mq = mq, .value = 32 };
    assert(pthread_create(&thread, NULL, blocking_send, &call) == 0);
    wait_started(&call);
    assert(nthread_message_queue_recv(mq, &value, 0) == 0);
    assert(value == 31);
    assert(pthread_join(thread, NULL) == 0);
    assert(call.ret == 0);
    assert(nthread_message_queue_recv(mq, &value, 0) == 0);
    assert(value == 32);

    nthread_message_queue_free(&mq);
}

static void test_error_wakeups(void)
{
    struct nthread_message_queue *mq = NULL;
    struct thread_call call = { 0 };
    pthread_t thread;
    int value = 41;

    assert(nthread_message_queue_alloc(&mq, 1, sizeof(int)) == 0);
    assert(nthread_message_queue_send(mq, &value, 0) == 0);
    call = (struct thread_call){ .mq = mq, .value = 42 };
    assert(pthread_create(&thread, NULL, blocking_send, &call) == 0);
    wait_started(&call);
    nthread_message_queue_set_err_send(mq, -EPIPE);
    assert(pthread_join(thread, NULL) == 0);
    assert(call.ret == -EPIPE);
    nthread_message_queue_free(&mq);

    mq = NULL;
    call = (struct thread_call){ 0 };
    assert(nthread_message_queue_alloc(&mq, 1, sizeof(int)) == 0);
    call.mq = mq;
    assert(pthread_create(&thread, NULL, blocking_recv, &call) == 0);
    wait_started(&call);
    nthread_message_queue_set_err_recv(mq, -ECANCELED);
    assert(pthread_join(thread, NULL) == 0);
    assert(call.ret == -ECANCELED);
    nthread_message_queue_free(&mq);
}

static void test_recv_drains_before_error(void)
{
    struct nthread_message_queue *mq = NULL;
    int input[] = { 51, 52 };
    int output = 0;

    assert(nthread_message_queue_alloc(&mq, 2, sizeof(int)) == 0);
    assert(nthread_message_queue_send(mq, &input[0], 0) == 0);
    assert(nthread_message_queue_send(mq, &input[1], 0) == 0);
    nthread_message_queue_set_err_recv(mq, -ECANCELED);

    assert(nthread_message_queue_recv(mq, &output, 0) == 0);
    assert(output == 51);
    assert(nthread_message_queue_recv(mq, &output, 0) == 0);
    assert(output == 52);
    assert(nthread_message_queue_recv(mq, &output, 0) == -ECANCELED);

    nthread_message_queue_free(&mq);
}

static void test_flush_without_destructor(void)
{
    struct nthread_message_queue *mq = NULL;
    int values[] = { 61, 62, 63 };

    assert(nthread_message_queue_alloc(&mq, 3, sizeof(int)) == 0);
    for (size_t i = 0; i < 3; i++)
        assert(nthread_message_queue_send(mq, &values[i], 0) == 0);

    nthread_message_flush(mq);
    assert(nthread_message_queue_get_msg_count(mq) == 0);
    nthread_message_queue_free(&mq);
}

static void test_flush_calls_destructor(void)
{
    struct nthread_message_queue *mq = NULL;
    void *messages[3];

    atomic_store(&freed_count, 0);
    assert(nthread_message_queue_alloc(&mq, 3, sizeof(void *)) == 0);
    nthread_message_queue_set_free_func(mq, free_pointer_message);

    for (size_t i = 0; i < 3; i++) {
        messages[i] = malloc(16);
        assert(messages[i]);
        assert(nthread_message_queue_send(mq, &messages[i], 0) == 0);
    }

    nthread_message_flush(mq);
    assert(nthread_message_queue_get_msg_count(mq) == 0);
    assert(atomic_load(&freed_count) == 3);
    nthread_message_queue_free(&mq);
    assert(atomic_load(&freed_count) == 3);
}

static void test_recv_transfers_ownership(void)
{
    struct nthread_message_queue *mq = NULL;
    void *input = malloc(16);
    void *output = NULL;

    assert(input);
    atomic_store(&freed_count, 0);
    assert(nthread_message_queue_alloc(&mq, 1, sizeof(void *)) == 0);
    nthread_message_queue_set_free_func(mq, free_pointer_message);
    assert(nthread_message_queue_send(mq, &input, 0) == 0);
    assert(nthread_message_queue_recv(mq, &output, 0) == 0);
    assert(output == input);
    assert(atomic_load(&freed_count) == 0);

    nthread_message_queue_free(&mq);
    assert(atomic_load(&freed_count) == 0);
    free(output);
}

static void test_queue_free_releases_pending_messages(void)
{
    struct nthread_message_queue *mq = NULL;
    void *messages[2];

    atomic_store(&freed_count, 0);
    assert(nthread_message_queue_alloc(&mq, 2, sizeof(void *)) == 0);
    nthread_message_queue_set_free_func(mq, free_pointer_message);

    for (size_t i = 0; i < 2; i++) {
        messages[i] = malloc(16);
        assert(messages[i]);
        assert(nthread_message_queue_send(mq, &messages[i], 0) == 0);
    }

    nthread_message_queue_free(&mq);
    assert(mq == NULL);
    assert(atomic_load(&freed_count) == 2);
}

int main(void)
{
    test_invalid_arguments();
    test_nonblocking();
    test_data_wakeups();
    test_error_wakeups();
    test_recv_drains_before_error();
    test_flush_without_destructor();
    test_flush_calls_destructor();
    test_recv_transfers_ownership();
    test_queue_free_releases_pending_messages();

    puts("nthread_message tests passed");
    return 0;
}
