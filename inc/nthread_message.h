#ifndef NTHREADMESSAGE_H
#define NTHREADMESSAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nthread_message.h
 * @brief 基于固定容量 FIFO、mutex 和 condition variable 的线程安全消息队列。
 *
 * 队列按值复制消息：发送和接收时均复制 msg_size 字节。若消息类型是指针，
 * 队列复制的是指针值，而不是指针指向的对象。
 *
 * 默认情况下，队列满时 send 阻塞，队列空时 recv 阻塞。传入
 * NTHREAD_MESSAGE_NONBLOCK 后，相应操作不会等待，而是返回 -EAGAIN。
 *
 * 资源所有权约定：
 * - send 成功后，消息中需要释放的资源由队列接管；send 失败时仍由发送者持有。
 * - recv 成功后，资源所有权转移给接收者，队列不会调用 free_func。
 * - flush 或 queue_free 丢弃队列中的消息时，才会调用 free_func。
 *
 * 关闭队列时，通常先设置 err_send 阻止继续发送，再设置 err_recv 让接收者
 * 排空已有消息后退出。主线程必须 pthread_join 所有使用该队列的线程，最后才
 * 能调用 nthread_message_queue_free()。
 */

struct nthread_message_queue;

/** 消息队列操作标志。 */
enum nthread_message_flags {
    /** 不等待队列状态变化；当前无法完成时返回 -EAGAIN。 */
    NTHREAD_MESSAGE_NONBLOCK = 1 << 0,
};

/**
 * 创建固定容量的线程消息队列。
 *
 * @param[out] mq       接收新队列指针的地址。
 * @param[in]  msg_cap  队列最多容纳的消息数量，必须大于 0。
 * @param[in]  msg_size 每条消息的字节数，必须大于 0。
 * @return 成功返回 0；参数非法返回 -EINVAL；内存不足返回 -ENOMEM；
 *         pthread 初始化失败时返回对应 pthread 错误码的负值。
 */
int nthread_message_queue_alloc(struct nthread_message_queue **mq,
                                size_t msg_cap,
                                size_t msg_size);

/**
 * 销毁消息队列，并将 *mq 置为 NULL。
 *
 * 销毁前会 flush 队列；若设置了 free_func，所有尚未接收的消息都会通过该
 * 回调释放。调用前必须保证所有使用该队列的线程已经退出，否则销毁仍在使用的
 * mutex、condition variable 或 FIFO 会产生未定义行为。
 *
 * @param[in,out] mq 队列指针的地址；mq 或 *mq 为 NULL 时不执行任何操作。
 */
void nthread_message_queue_free(struct nthread_message_queue **mq);

/**
 * 发送一条消息。
 *
 * 函数将 msg 指向的 msg_size 字节复制到队列。队列满时默认阻塞；设置
 * NTHREAD_MESSAGE_NONBLOCK 后返回 -EAGAIN。send 成功后，消息资源的所有权
 * 交给队列；失败时所有权仍属于调用者。
 *
 * @param[in] mq    消息队列。
 * @param[in] msg   待复制消息的地址，不能为 NULL。
 * @param[in] flags 0 或 NTHREAD_MESSAGE_NONBLOCK。
 * @return 成功返回 0；参数非法返回 -EINVAL；非阻塞且队列满返回 -EAGAIN；
 *         设置 err_send 后返回 err_send。
 */
int nthread_message_queue_send(struct nthread_message_queue *mq,
                               void *msg,
                               unsigned int flags);

/**
 * 接收一条消息。
 *
 * 函数将队头消息的 msg_size 字节复制到 msg，然后将该消息出队。队列空时
 * 默认阻塞；设置 NTHREAD_MESSAGE_NONBLOCK 后返回 -EAGAIN。recv 成功后，
 * 消息资源的所有权转移给接收者。
 *
 * 设置 err_recv 后，队列中的已有消息仍可继续接收；只有队列为空时才返回
 * err_recv。
 *
 * @param[in]  mq    消息队列。
 * @param[out] msg   接收消息的地址，不能为 NULL。
 * @param[in]  flags 0 或 NTHREAD_MESSAGE_NONBLOCK。
 * @return 成功返回 0；参数非法返回 -EINVAL；非阻塞且队列空返回 -EAGAIN；
 *         队列排空后返回 err_recv。
 */
int nthread_message_queue_recv(struct nthread_message_queue *mq,
                               void *msg,
                               unsigned int flags);

/**
 * 设置 send 接口返回的持久错误码，并唤醒所有等待发送的线程。
 *
 * 常用于消费者故障或停止接收时，通知生产者停止产生新消息。err 为 0 时清除
 * 发送错误状态。
 *
 * @param[in] mq  消息队列。
 * @param[in] err send 后续应返回的错误码，通常为负值；0 表示清除。
 */
void nthread_message_queue_set_err_send(struct nthread_message_queue *mq,
                                        int err);

/**
 * 设置 recv 接口返回的持久错误码，并唤醒所有等待接收的线程。
 *
 * 常用于生产者结束或队列关闭。接收者会先排空队列中的已有消息，队列为空后
 * recv 才返回 err。err 为 0 时清除接收错误状态。
 *
 * @param[in] mq  消息队列。
 * @param[in] err recv 在队列为空时应返回的错误码，通常为负值；0 表示清除。
 */
void nthread_message_queue_set_err_recv(struct nthread_message_queue *mq,
                                        int err);

/**
 * 设置丢弃消息时使用的析构回调。
 *
 * free_func 接收的是 FIFO 中一条消息槽位的地址。若队列元素类型是 T *，回调
 * 参数实际应按 T ** 解释。回调仅在 flush 或 queue_free 丢弃未接收消息时调用；
 * 正常 recv 不会调用。
 *
 * 回调在队列 mutex 已锁定的情况下执行，只能释放该消息拥有的资源，不能调用
 * 同一个消息队列的 send、recv、flush、get_msg_count、set_err 或
 * set_free_func，否则会再次申请同一把 mutex 并产生死锁。
 *
 * 建议在发送第一条消息前完成设置。传入 NULL 表示不需要逐条析构。
 *
 * @param[in] mq        消息队列。
 * @param[in] free_func 单条消息析构函数，或 NULL。
 */
void nthread_message_queue_set_free_func(struct nthread_message_queue *mq,
                                         void (*free_func)(void *msg));

/**
 * 获取调用瞬间的队列消息数量快照。
 *
 * 返回后其他线程可能立即改变队列，因此该值只能用于观察和统计，不能用来保证
 * 下一次 send 或 recv 一定不会阻塞。
 *
 * @param[in] mq 消息队列。
 * @return 取得锁时队列中的消息数量。
 */
size_t nthread_message_queue_get_msg_count(struct nthread_message_queue *mq);

/**
 * 丢弃当前队列中的全部消息，并唤醒所有等待发送的线程。
 *
 * 若设置了 free_func，会先逐条调用它释放消息资源，再将消息出队；否则直接将
 * 消息全部出队。该函数不修改 err_send 或 err_recv。
 *
 * @param[in] mq 消息队列。
 */
void nthread_message_flush(struct nthread_message_queue *mq);

#ifdef __cplusplus
}
#endif

#endif /* NTHREADMESSAGE_H */
