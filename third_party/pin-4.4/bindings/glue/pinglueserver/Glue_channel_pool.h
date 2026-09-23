/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_CHANNEL_POOL_H_
#define _GLUE_CHANNEL_POOL_H_

#include <memory>
#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
#include <txll_utils.h>

namespace glue
{
/**
 * @brief A structure holding a txll channel with a dedicated buffer.
 * 
 */
struct Channel
{
    /** @brief the channel address */
    void* handle = nullptr;

    /** @brief a buffer that will used for the channel read/write operations */
    uint8_t buffer[glue::BUFFER_SIZE];
};

/** @brief A structure define a custom deleter for channel */
struct Channel_deleter
{
    void operator()(Channel* channel) const
    {
        (void)tx_close(channel->handle);
        delete channel;
    }
};

/** @brief A unique_ptr for Channel with specialized deleter */
typedef std::unique_ptr< Channel, Channel_deleter > t_unique_channel;

/*! @brief A class for managing txll channel pool for client when each channel has it's own buffer.
*/
class Client_channel_pool
{
  public:
    Client_channel_pool(size_t capacity, uint32_t channelSize) : capacity_(capacity), inUse_(0), channelSize_(channelSize) {}
    ~Client_channel_pool() = default;

    /**
     *  @brief Acquire a channel from the pool.
     *  If pool is not empty, a channel is taken from the pool and returned.
     *  If pool is empty and retrySleepUS is not 0, the function will sleep retrySleepUS and retry.
     * 
     *  @param[in] retrySleepUS the timeout in micro seconds to sleep before retrying.
     * 
     *  @return a unique pointer to a channel with a special deleter, nullptr if pool is empty.
     */
    std::unique_ptr< Channel, std::function< void(Channel*) > > acquire(size_t retrySleepUS = 0)
    {
        auto ret = get();

        if (nullptr == ret && 0 != retrySleepUS)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(retrySleepUS));
            ret = get();
        }

        return ret;
    }

    /**
     *  @brief Add a new channel to the pool.
     *  If current pool size + inUse_ (channels that acquired but not released yet) is smaller than capacity_,
     *  a new channel is added to the pool.
     *  This function usually should called per new thread (there is no logic in the need for more channels than threads).
     * 
     *  @return true if new channel added, false if pool is full.
     */
    bool add()
    {
        std::lock_guard< std::mutex > lock(mutex_);
        if ((channelPool_.size() + inUse_) < capacity_)
        {
            try
            {
                auto newChannel    = std::make_unique< Channel >();

                t_syscall_ret ret = tx_create(channelSize_);
                ASSERT(!IS_PINOS_SYSCALL_ERROR(ret), "ERROR Creating a new channel");
                newChannel->handle = reinterpret_cast< void* >(ret);
                channelPool_.emplace_back(newChannel.release());
            }
            catch (const std::exception& e)
            {
                ASSERTQ("Out Of Memory");
            }

            return true;
        }

        return false; // Pool is full
    }

  private:
    size_t capacity_;
    size_t inUse_;
    uint32_t channelSize_;
    std::deque< t_unique_channel > channelPool_;
    std::mutex mutex_;

    std::unique_ptr< Channel, std::function< void(Channel*) > > get()
    {
        std::lock_guard< std::mutex > lock(mutex_);
        if (!channelPool_.empty())
        {
            auto channel = channelPool_.front().release();
            channelPool_.pop_front();
            ++inUse_;

            return std::unique_ptr< Channel, std::function< void(Channel*) > >(channel,
                                                                               [this](Channel* channel) { release(channel); });
        }

        return nullptr; // Pool is empty
    }

    // Custom deleter for releasing the channel back to the pool
    void release(Channel* channel)
    {
        std::lock_guard< std::mutex > lock(mutex_);
        ASSERT((channelPool_.size() + inUse_ <= capacity_), "ERROR! releasing a channel while the channelPool is already full!");
        channelPool_.emplace_back(channel);
        --inUse_;
    }
};

} // namespace glue

#endif // _GLUE_CHANNEL_POOL_H_
