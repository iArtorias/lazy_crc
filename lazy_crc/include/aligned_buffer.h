#pragma once

#include <windows.h>
#include <cstdint>

// Wrapper for a virtually allocated sector aligned buffer.
struct AlignedBuffer
{
    void * m_Ptr { nullptr };
    std::size_t m_Size { 0 };

    explicit AlignedBuffer( std::size_t n )
        : m_Ptr( ::VirtualAlloc( nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE ) )
        , m_Size( m_Ptr ? n : 0 )
    {}

    ~AlignedBuffer()
    {
        if (m_Ptr)
            ::VirtualFree( m_Ptr, 0, MEM_RELEASE );
    }

    AlignedBuffer( const AlignedBuffer & ) = delete;
    AlignedBuffer & operator=( const AlignedBuffer & ) = delete;

    [[nodiscard]] bool Ok() const noexcept
    {
        return m_Ptr != nullptr;
    }
};