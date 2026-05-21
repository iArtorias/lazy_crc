#pragma once

#include <Windows.h>
#include <cstdint>

// Minimum file size for double-buffered overlapped I/O.
static constexpr std::size_t OVERLAPPED_THRESHOLD = 64 * 1024 * 1024;

// Double-buffered overlapped read + hash pipeline.
// Two overlapped structs let one read be in-flight while the CPU hashes
// the previous buffer.
template <typename HashFn>
static void OverlappedReadLoop(
    HANDLE handle,
    std::size_t file_size,
    DWORD buf_size,
    AlignedBuffer bufs[2],
    HashFn hash_chunk
)
{
    static constexpr std::size_t SECTOR = 4096;

    OVERLAPPED ov[2] {};
    ov[0].hEvent = ::CreateEventW( nullptr, TRUE, FALSE, nullptr );
    ov[1].hEvent = ::CreateEventW( nullptr, TRUE, FALSE, nullptr );

    if (!ov[0].hEvent || !ov[1].hEvent)
    {
        if (ov[0].hEvent)
            ::CloseHandle( ov[0].hEvent );

        if (ov[1].hEvent)
            ::CloseHandle( ov[1].hEvent );
        return;
    }

    std::size_t file_offset = 0;

    // Issue one async read into bufs. Returns logical bytes requested, '0' at EOF.
    auto issue = [&] ( int idx ) -> std::size_t
    {
        if (file_offset >= file_size)
            return 0;

        const std::size_t logical = std::min(
            static_cast<std::size_t>(buf_size), file_size - file_offset );

        const DWORD padded = static_cast<DWORD>(
            (logical + SECTOR - 1) & ~(SECTOR - 1));

        ov[idx].Offset = static_cast<DWORD>(file_offset);
        ov[idx].OffsetHigh = static_cast<DWORD>(file_offset >> 32);

        ::ResetEvent( ov[idx].hEvent );

        const BOOL ok = ::ReadFile( handle, bufs[idx].m_Ptr, padded, nullptr, &ov[idx] );

        if (!ok && ::GetLastError() != ERROR_IO_PENDING)
            return 0;

        file_offset += logical;
        return logical;
    };

    // Wait for in-flight read.
    auto wait = [&] ( int idx, std::size_t logical ) -> std::size_t
    {
        DWORD got = 0;

        if (!::GetOverlappedResult( handle, &ov[idx], &got, TRUE ))
            return 0;

        return std::min( static_cast<std::size_t>(got), logical );
    };

    std::size_t pending[2] = { issue( 0 ), 0 };
    int cur = 0;

    if (pending[0] == 0)
    {
        ::CloseHandle( ov[0].hEvent );
        ::CloseHandle( ov[1].hEvent );
        return;
    }

    while (true)
    {
        const int nxt = 1 - cur;

        // Issue next read before waiting for current.
        pending[nxt] = issue( nxt );

        const std::size_t ready = wait( cur, pending[cur] );

        if (ready == 0)
            break;

        hash_chunk( bufs[cur].m_Ptr, ready );

        if (pending[nxt] == 0)
            break;

        cur = nxt;
    }

    // Hash any final in-flight buffer.
    {
        const int nxt = 1 - cur;

        if (pending[nxt] > 0)
        {
            const std::size_t ready = wait( nxt, pending[nxt] );

            if (ready > 0)
                hash_chunk( bufs[nxt].m_Ptr, ready );
        }
    }

    ::CloseHandle( ov[0].hEvent );
    ::CloseHandle( ov[1].hEvent );
}