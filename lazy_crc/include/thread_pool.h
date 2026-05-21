#pragma once

#include <vector>
#include <span>
#include <thread>
#include <atomic>
#include <filesystem>

namespace fs = std::filesystem;

#include <print_queue.h>

// Input / output types for batch processing.
struct FileEntry
{
    fs::path m_Path;
    std::uintmax_t m_Size;
};

struct FileResult
{
    fs::path m_Relative;
    std::wstring m_CRC;
};

// Per-file worker callback.
using WorkerFn = void (*)(const FileEntry &,
    const fs::path &,
    std::vector<FileResult> &,
    PrintQueue &);

// Fixed size thread pool with dynamic scheduling.
class ThreadPool
{
private:

    std::vector<std::thread> m_Workers;
    std::atomic<std::size_t> m_NextIndex;
    std::atomic<int> m_Done;

public:

    explicit ThreadPool( std::size_t threads_count )
        : m_NextIndex( 0 )
        , m_Done( 0 )
    {
        m_Workers.reserve( threads_count );
    }

    // Process all entries.
    // Returns all results in completion order.
    [[nodiscard]] std::vector<FileResult> Run(
        std::span<const FileEntry> entries,
        const fs::path & root,
        WorkerFn fn,
        PrintQueue & pq
    )
    {
        if (entries.empty())
            return {};

        const std::size_t total = entries.size();
        const std::size_t threads_count = m_Workers.capacity();

        m_NextIndex.store( 0, std::memory_order_relaxed );
        m_Done.store( 0, std::memory_order_relaxed );

        // Per-thread result storage.
        std::vector<std::vector<FileResult>> slots( threads_count );

        for (std::size_t t = 0; t < threads_count; ++t)
        {
            m_Workers.emplace_back( [&, t]
            {
                while (true)
                {
                    // Claim the next file index.
                    const std::size_t i = m_NextIndex.fetch_add(
                        1, std::memory_order_relaxed );

                    if (i >= total)
                        break;

                    fn( entries[i], root, slots[t], pq );
                }

                // Flush any remaining local print batch before signalling done.
                pq.FlushLocal();

                // Signal this thread is done.
                m_Done.fetch_add( 1, std::memory_order_acq_rel );
                m_Done.notify_all();
            } );
        }

        // Wait for all workers to finish.
        while (true)
        {
            const int done = m_Done.load( std::memory_order_acquire );

            if (done == static_cast<int>(threads_count))
                break;

            m_Done.wait( done, std::memory_order_acquire );
        }

        for (auto & worker : m_Workers)
            worker.join();

        m_Workers.clear();

        // Drain print queue before returning so all output is flushed.
        pq.Drain();

        // Merge per-thread result slots into one vector.
        std::vector<FileResult> results;
        results.reserve( total );

        for (auto & slot : slots)
            for (auto & result : slot)
                results.push_back( std::move( result ) );

        return results;
    }
};