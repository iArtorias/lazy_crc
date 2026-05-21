#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <atomic>

// Async print queue.
class PrintQueue
{
private:

    std::thread m_PrintThread;
    std::mutex m_Mutex;
    std::condition_variable m_Cv;
    std::vector<std::wstring> m_Queue;
    std::atomic<bool> m_Stop;

    // Thread local batch.
    static thread_local std::vector<std::wstring> m_LocalBatch;

    void DrainLoop()
    {
        std::vector<std::wstring> local;

        while (true)
        {
            {
                std::unique_lock lock( m_Mutex );

                m_Cv.wait( lock, [this]
                {
                    return !m_Queue.empty() || m_Stop.load( std::memory_order_acquire );
                } );

                if (m_Queue.empty() && m_Stop.load( std::memory_order_acquire ))
                    break;

                std::swap( local, m_Queue );
            }

            // Print outside the lock so workers aren't blocked.
            for (const auto & s : local)
                ::_putws( s.c_str() );

            local.clear();

            m_Cv.notify_all();
        }

        // Final flush after stop signal.
        std::lock_guard lock( m_Mutex );

        for (const auto & s : m_Queue)
            ::_putws( s.c_str() );

        m_Queue.clear();
    }

public:

    static constexpr std::size_t FLUSH_THRESHOLD = 64;

    PrintQueue()
        : m_Stop( false )
    {
        m_PrintThread = std::thread( [this]
        {
            DrainLoop();
        } );
    }

    ~PrintQueue()
    {
        if (!m_Stop.load( std::memory_order_acquire ))
            Stop();
    }

    PrintQueue( const PrintQueue & ) = delete;
    PrintQueue & operator=( const PrintQueue & ) = delete;

    // Push a message from a worker thread.
    // Accumulates locally, only locks when the batch threshold is reached.
    void Push( 
        std::wstring msg
    )
    {
        m_LocalBatch.push_back( std::move( msg ) );

        if (m_LocalBatch.size() >= FLUSH_THRESHOLD)
            FlushLocal();
    }

    // Flush remaining local messages.
    void FlushLocal()
    {
        if (m_LocalBatch.empty())
            return;

        {
            std::lock_guard lock( m_Mutex );

            for (auto & s : m_LocalBatch)
                m_Queue.push_back( std::move( s ) );
        }

        m_LocalBatch.clear();
        m_Cv.notify_one();
    }

    // Block until all queued messages have been printed.
    void Drain()
    {
        FlushLocal();

        std::unique_lock lock( m_Mutex );
        m_Cv.wait( lock, [this]
        {
            return m_Queue.empty();
        } );
    }

	// Stop the print thread and flush all messages.
    void Stop()
    {
        Drain();
        m_Stop.store( true, std::memory_order_release );
        m_Cv.notify_all();

        if (m_PrintThread.joinable())
            m_PrintThread.join();
    }
};

inline thread_local std::vector<std::wstring> PrintQueue::m_LocalBatch;