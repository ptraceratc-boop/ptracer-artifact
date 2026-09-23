/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_ASYNC_MSG_H_
#define _GLUE_ASYNC_MSG_H_

#include <memory>
#include <queue>
#include <mutex>
#include "Glue.h"

namespace glue
{
/**
 * @brief Holds information about an asynchronous message.
 */
struct Async_message_info
{
    /** @brief The message data buffer */
    uint8_t buffer[glue::BUFFER_SIZE];

    /** @brief The rsc massage counter */
    t_rsc_message_counter msgCounter;
    /** @brief The size of the message */
    size_t msgSize;
    /** @brief The client channel ID that will receive this message */
    uint64_t clientChannelId;
};

/** @brief A unique_ptr for Async_message_info with a specialized deleter */
typedef std::unique_ptr< Async_message_info, std::function< void(Async_message_info*) > > t_unique_async_msg;

/**
 * @class Async_msg_pool
 * @brief Manages a pool of pre-allocated async messages for efficient reuse.
 *
 * This class implements a thread-safe message pool to minimize the overhead of
 * dynamic memory allocation and deallocation. It allows for the acquisition
 * and release of messages of a fixed size.
 */
class Async_msg_pool
{
  public:
    /**
     * @brief Constructs a message pool with a specified pool size.
     *
     * Pre-allocates a number of messages of the given size and adds them to the pool.
     *
     * @param [in] poolSize   The number of messages to pre-allocate for the pool.
     */
    Async_msg_pool(size_t poolSize)
    {
        for (size_t i = 0; i < poolSize; ++i)
        {
            messages_.emplace(new Async_message_info());
        }
    }

    /**
     * @brief Acquires a message from the pool.
     *
     * If the pool has available messages, it returns one. If the pool is empty,
     * it returns a nullptr, indicating that no messages are currently available.
     *
     * @return A unique_ptr to the acquired message with a custom deleter that will return it back to the pool,
     *         or nullptr if the pool is empty.
     */
    t_unique_async_msg acquire()
    {
        std::lock_guard< std::mutex > lock(poolMutex_);
        if (messages_.empty())
        {
            return nullptr;
        }
        else
        {
            // Get the unique_ptr from the front of the queue.
            auto message = messages_.front().release();
            messages_.pop();
            // Return the unique_ptr with a custom deleter that will call the release method.
            return t_unique_async_msg(message, [this](Async_message_info* message) { this->release(message); });
        }
    }

  private:
    /**
     * @brief Releases a message back to the pool.
     *
     * The released message is added back to the pool for reuse.  
     * The message should have been obtained from this pool initially and should not be null, 
     * except when intentionally pushing null as a signal for a specific condition during acquisition.
     *
     * @param [in] message The message to release back to the pool.
     */
    void release(Async_message_info* message)
    {
        std::lock_guard< std::mutex > lock(poolMutex_);
        messages_.emplace(message);
    }

    /** @brief Queue of available messages for reuse. */
    std::queue< std::unique_ptr< Async_message_info > > messages_;
    /** @brief Mutex for synchronizing access to the message queue. */
    std::mutex poolMutex_;
};

/**
 * @class Async_msg_queue
 * @brief Manages a queue of asynchronous messages.
 *
 * This class provides a thread-safe queue for storing and retrieving asynchronous messages.
 * It uses a condition variable to allow threads to wait for messages to become available.
 */
class Async_msg_queue
{
  public:
    /**
     * @brief Adds t_unique_async_msg to the back of the queue.
     *
     * This method locks the queue, adds the element to the back, and then notifies
     * one waiting thread that an element is available.
     *
     * @param [in] asyncMsg The t_unique_async_msg to add to the queue.
     */
    void push(t_unique_async_msg&& asyncMsg)
    {
        std::lock_guard< std::mutex > lock(mutex_);
        queue_.push(std::forward< t_unique_async_msg >(asyncMsg));

        cond_.notify_one();
    }

    /**
     * @brief Removes and returns the t_unique_async_msg element from the front of the queue.
     *
     * This method locks the queue and waits for an element to become available if the queue is empty.
     * Once an element is available, it retrieves and removes the element from the front of the queue.
     * If multiple threads are waiting to pop from the queue, only one thread will proceed when an element
     * is pushed into the queue.
     *
     * @return The t_unique_async_msg from the front of the queue.
     */
    t_unique_async_msg pop()
    {
        std::unique_lock< std::mutex > lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty(); });
        auto asyncMsg = std::move(queue_.front());
        queue_.pop();

        return asyncMsg;
    }

  private:
    /** @brief The underlying queue container. */
    std::queue< t_unique_async_msg > queue_;
    /** @brief The mutex used to synchronize access to the queue. */
    std::mutex mutex_;
    /** @brief The condition variable used to wait for an element to become available. */
    std::condition_variable cond_;
};

} // namespace glue

#endif // _GLUE_ASYNC_MSG_H_
