#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

// Tells worker threads to stop. A bare condition_variable notify is lost if the thread is not
// waiting at that moment (busy in a dbus call, or still starting up), and the thread then never
// stops, so joining it hangs. Here the request is remembered, so it can't be missed.
class QuitSignal
{
    std::mutex mutex;
    std::condition_variable cv;
    bool quit = false;
public:
    void request()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            quit = true;
        }
        cv.notify_all();
    }

    // Waits up to timeout, returns true if a stop was requested
    template<class Rep, class Period>
    bool wait_for(const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [this]{ return quit; });
    }
};
