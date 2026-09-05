#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include "nfifo.h"
#include "ncore/nthread_message.h"

#define NERROR(e) (-(e))   ///< Returns a negative error code from a POSIX error code, to return from library functions.

struct nthread_message_queue {
    struct nfifo *fifo;
    pthread_mutex_t lock;
    pthread_cond_t cond_recv;
    pthread_cond_t cond_send;
    int err_send;
    int err_recv;
    size_t msg_size;
    void (*free_func)(void *msg);
};

int nthread_message_queue_alloc(struct nthread_message_queue **mq,
                                  size_t msg_cap,
                                  size_t msg_size)
{
    struct nthread_message_queue *rmq = NULL;
    int ret = 0;

    if (!mq || !msg_cap || !msg_size)
        return NERROR(EINVAL);

    if (msg_cap > SIZE_MAX / msg_size)
        return NERROR(EINVAL);
    
    if (!(rmq = calloc(1, sizeof(*rmq)))) {
        return NERROR(ENOMEM);
    }

    if ((ret = pthread_mutex_init(&rmq->lock, NULL))) {
        free(rmq);
        return NERROR(ret);
    }

    if ((ret = pthread_cond_init(&rmq->cond_send, NULL))) {
        pthread_mutex_destroy(&rmq->lock);
        free(rmq);
        return NERROR(ret);
    }

    if ((ret = pthread_cond_init(&rmq->cond_recv, NULL))) {
        pthread_cond_destroy(&rmq->cond_send);
        pthread_mutex_destroy(&rmq->lock);
        free(rmq);
        return NERROR(ret);
    }

    if (!(rmq->fifo = nfifo_alloc(msg_cap, msg_size, 0))) {
        pthread_cond_destroy(&rmq->cond_recv);
        pthread_cond_destroy(&rmq->cond_send);
        pthread_mutex_destroy(&rmq->lock);
        free(rmq);
        return NERROR(ENOMEM);
    }
    rmq->msg_size = msg_size;
    *mq = rmq;
    return 0;
}

void nthread_message_queue_free(struct nthread_message_queue **mq)
{
    struct nthread_message_queue *rmq;

    if (!mq || !*mq)
        return;

    rmq = *mq;
    nthread_message_flush(rmq);
    nfifo_freep(&rmq->fifo);
    pthread_cond_destroy(&rmq->cond_recv);
    pthread_cond_destroy(&rmq->cond_send);
    pthread_mutex_destroy(&rmq->lock);
    *mq = NULL;
    free(rmq);
}

static int nthread_message_queue_send_locked(struct nthread_message_queue *mq,
                                 void *msg,
                                 unsigned int flags)
{
    while (!mq->err_send && !nfifo_can_write(mq->fifo)) {
        if ((flags & NTHREAD_MESSAGE_NONBLOCK))
            return NERROR(EAGAIN);
        pthread_cond_wait(&mq->cond_send, &mq->lock);
    }
    
    if (mq->err_send)      // 消费者出错直接退出
        return mq->err_send;

    nfifo_write(mq->fifo, msg, 1);
    pthread_cond_signal(&mq->cond_recv);

    return 0;
}

int nthread_message_queue_send(struct nthread_message_queue *mq,
                                 void *msg,
                                 unsigned int flags)
{
    int ret = 0;

    if (!mq || !msg)
        return NERROR(EINVAL);

    pthread_mutex_lock(&mq->lock);
    ret = nthread_message_queue_send_locked(mq, msg, flags);
    pthread_mutex_unlock(&mq->lock);
    return ret;
}

static int nthread_message_queue_recv_locked(struct nthread_message_queue *mq,
                                 void *msg,
                                 unsigned int flags)
{
    while (!mq->err_recv && !nfifo_can_read(mq->fifo)) {
        if ((flags & NTHREAD_MESSAGE_NONBLOCK))
            return NERROR(EAGAIN);
        pthread_cond_wait(&mq->cond_recv, &mq->lock);
    }

    if (!nfifo_can_read(mq->fifo))
        return mq->err_recv;

    nfifo_read(mq->fifo, msg, 1);   // 不论生产者是否出错, 都需要把队列数据输出
    pthread_cond_signal(&mq->cond_send);
    return 0;
}

int nthread_message_queue_recv(struct nthread_message_queue *mq,
                                 void *msg,
                                 unsigned int flags)
{
    int ret = 0;

    if (!mq || !msg)
        return NERROR(EINVAL);

    pthread_mutex_lock(&mq->lock);
    ret = nthread_message_queue_recv_locked(mq, msg, flags);
    pthread_mutex_unlock(&mq->lock);
    return ret;
}

void nthread_message_queue_set_err_send(struct nthread_message_queue *mq,
                                          int err)
{
    pthread_mutex_lock(&mq->lock);
    mq->err_send = err;
    pthread_cond_broadcast(&mq->cond_send);
    pthread_mutex_unlock(&mq->lock);
}

void nthread_message_queue_set_err_recv(struct nthread_message_queue *mq,
                                          int err)
{
    pthread_mutex_lock(&mq->lock);
    mq->err_recv = err;
    pthread_cond_broadcast(&mq->cond_recv);
    pthread_mutex_unlock(&mq->lock);
}

void nthread_message_queue_set_free_func(struct nthread_message_queue *mq,
                                           void (*free_func)(void *msg))
{
    pthread_mutex_lock(&mq->lock);
    mq->free_func = free_func;
    pthread_mutex_unlock(&mq->lock);
}

size_t nthread_message_queue_get_msg_count(struct nthread_message_queue *mq)
{
    size_t can_read;
    pthread_mutex_lock(&mq->lock);
    can_read = nfifo_can_read(mq->fifo);
    pthread_mutex_unlock(&mq->lock);
    return can_read;
}

static int free_func_wrap(void *opaque, void *buf, size_t *nb_elems)
{
    struct nthread_message_queue *mq = opaque;
    uint8_t *msg = buf;

    for (size_t i = 0; i < *nb_elems; i++)
        mq->free_func(msg + i * mq->msg_size);

    return 0;
}

void nthread_message_flush(struct nthread_message_queue *mq)
{
    size_t can_read;

    pthread_mutex_lock(&mq->lock);
    can_read = nfifo_can_read(mq->fifo);
    if (mq->free_func) {
        nfifo_read_to_cb(mq->fifo, free_func_wrap, mq, &can_read);
    } else {
        nfifo_drain(mq->fifo, can_read);
    }
    // 唤醒所有生产者
    pthread_cond_broadcast(&mq->cond_send);
    pthread_mutex_unlock(&mq->lock);
}