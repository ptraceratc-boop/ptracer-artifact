/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_TIMER_THREAD_H_
#define _GLUE_TIMER_THREAD_H_

#include <iostream>
#include <thread>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace glue
{
/**
 * @class Timer_thread
 * @brief This class provides a simple timer implementation that executes a user-provided callback function
 *        periodically. The timer runs on its own thread and can be started and stopped as needed.
 */
class Timer_thread
{
  public:
    /** @brief The prototype of the timer callback function. */
    using TIMER_FUNCTION = std::function< void(void*) >;

    /**
     * @brief Constructs a Timer_thread with a callback function, interval, and argument.
     *
     * @param [in] func     The callback function to be executed by the timer.
     * @param [in] interval The interval in milliseconds at which to execute the callback function.
     * @param [in] arg      A pointer to user-defined data that will be passed to the callback function.
     */
    Timer_thread(TIMER_FUNCTION func, unsigned int interval, void* arg)
        : func_(func), interval_(interval), arg_(arg), running_(false), shouldStop_(false)
    {}

    /**
     * @brief Destructor that ensures the timer is stopped.
     */
    ~Timer_thread() { stop(); }

    /**
     * @brief Starts the timer thread. If the timer is already running, this function does nothing.
     * 
     */
    void start()
    {
        if (running_)
        {
            return;
        }

        running_ = true;
        thread_  = std::thread(
            [this]()
            {
                PIN_InitializeInternalThread(); // Allow using TLS from a timer thread
                while (!shouldStop_)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(interval_));
                    if (!shouldStop_)
                    {
                        func_(arg_);
                    }
                }
                running_    = false;
                shouldStop_ = false;
            });
        thread_.detach();
    }

    /**
     * @brief Stops the timer thread.
     * 
     */
    void stop() { shouldStop_ = true; }

    /**
     * @brief Wait for a timer to stop
     * 
     * @param[in] timeout Timeout in milliseconds to wait for the timer. This value can be
     *                    (unsigned)-1 to wait indefinitely.
     * @return true  The timer stopped within the given timeout
     * @return false There was a timeout waiting for the timer
     */
    bool wait(unsigned int timeout)
    {
        constexpr unsigned int INFINITE = -1U;
        auto start                      = std::chrono::high_resolution_clock::now();
        while (running_)
        {
            if (INFINITE != timeout &&
                ((std::chrono::high_resolution_clock::now() - start) >= std::chrono::milliseconds(timeout)))
            {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }

  private:
    /** @brief The callback function to be executed by the timer. */
    TIMER_FUNCTION func_;
    /** @brief The interval in milliseconds at which to execute the callback function. */
    unsigned int interval_;
    /** @brief A pointer to user-defined data that will be passed to the callback function. */
    void* arg_;
    /** @brief Flag indicating whether the timer is running. */
    std::atomic< bool > running_;
    std::atomic< bool > shouldStop_;
    /** @brief The thread on which the timer runs. */
    std::thread thread_;
};

/**
 * @class Timer_thread_manager
 * @brief Manages multiple Timer_thread instances, providing functionality to create, start, and stop timers.
 *
 * @note This class is not thread-safe and is designed to be used in a single-threaded context.
 *       In the current implementation, all operations on Timer_thread_manager are expected to be
 *       called from a Python environment that ensures mutual exclusion through its own locking mechanism.
 *       If this class is to be used in a multi-threaded context in the future, appropriate synchronization
 *       mechanisms (e.g., mutexes) must used to control access to it.
 */
class Timer_thread_manager
{
  public:
    /**
     * @brief Constructs a Timer_thread_manager.
     *
     */
    Timer_thread_manager() : nextId_(0) {}

    /**
     * @brief Adds a new timer to the manager.
     * 
     * To remove the timer call @ref close_timer
     *
     * If the number of created timers has reached the maximum value of size_t, 
     * the function returns std::numeric_limits<size_t>::max().
     *
     * @param [in] func     The callback function to be executed by the timer.
     * @param [in] interval The interval in milliseconds at which to execute the callback function.
     * @param [in] arg      A pointer to user-defined data that will be passed to the callback function.
     * 
     * @return The ID of the new timer,
     *         std::numeric_limits<size_t>::max() if the maximum number of created timers has been reached.
     */
    size_t add_timer(Timer_thread::TIMER_FUNCTION func, unsigned int interval, void* arg)
    {
        if (std::numeric_limits< size_t >::max() == nextId_)
        {
            return std::numeric_limits< size_t >::max();
        }

        size_t id   = nextId_++;
        timers_[id] = std::make_unique< Timer_thread >(func, interval, arg);

        return id;
    }

    /**
     * @brief Start a timer
     * 
     * @param id 
     * @return true The timer was started
     * @return false The id was not found
     */
    bool start_timer(size_t id)
    {
        auto it = timers_.find(id);
        if (it != timers_.end())
        {
            it->second->start();
            return true;
        }

        return false;
    }

    /**
     * @brief Stops a timer with the given ID.
     *
     * @param [in] id The ID of the timer to stop.
     * 
     * @return true if the timer was successfully stopped, false if the timer with the given ID was not found.
     */
    bool stop_timer(size_t id)
    {
        auto it = timers_.find(id);
        if (it != timers_.end())
        {
            it->second->stop();
            return true;
        }

        return false;
    }

    /**
     * @brief Wait for a timer to finish
     * 
     * @param id 
     * @param timeout 
     * @return true The timer was stopped
     * @return false The timer was not stopped or does not exist
     */
    bool wait_timer(size_t id, unsigned timeout)
    {
        auto it = timers_.find(id);
        if (it != timers_.end())
        {
            return it->second->wait(timeout);
        }
        return false;
    }

    /**
     * @brief Stop and close a timer
     * 
     * @param id 
     * @return true The timer was closed
     * @return false The timer was not found
     */
    bool close_timer(size_t id)
    {
        auto it = timers_.find(id);
        if (it != timers_.end())
        {
            timers_.erase(id);
            return true;
        }

        return false;
    }

    /**
     * @brief Destructor that ensures all timers are stopped.
     * 
     */
    ~Timer_thread_manager()
    {
        for (auto& pair : timers_)
        {
            if (pair.second)
            {
                pair.second->stop();
            }
        }
        timers_.clear();
    }

  private:
    /** @brief A map of active timers indexed by their IDs. */
    std::unordered_map< size_t, std::unique_ptr< Timer_thread > > timers_;
    /** @brief The next available timer ID. */
    size_t nextId_;
};
} // namespace glue

#endif // _GLUE_TIMER_THREAD_H_
