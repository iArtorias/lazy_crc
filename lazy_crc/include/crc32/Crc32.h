// //////////////////////////////////////////////////////////
// Crc32.h
// Copyright (c) 2011-2019 Stephan Brumme. All rights reserved.
// Slicing-by-16 contributed by Bulat Ziganshin
// Tableless bytewise CRC contributed by Hagai Gold
// see http://create.stephan-brumme.com/disclaimer.html
//
// Modernization and fixes by iArtorias [iartorias.re@gmail.com].

// If running on an embedded system, you might consider shrinking the
// big 'Crc32Lookup' table by undefining these lines:
#define CRC32_USE_LOOKUP_TABLE_BYTE
#define CRC32_USE_LOOKUP_TABLE_SLICING_BY_4
#define CRC32_USE_LOOKUP_TABLE_SLICING_BY_8
#define CRC32_USE_LOOKUP_TABLE_SLICING_BY_16

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

/// Compute CRC32 using the fastest algorithm for large datasets on modern CPUs.
[[nodiscard]] std::uint32_t crc32_fast( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

/// Span overload.
[[nodiscard]] inline std::uint32_t crc32_fast( 
    std::span<const std::byte> data,
    std::uint32_t previousCrc32 = 0
) noexcept
{
    return crc32_fast( data.data(), data.size(), previousCrc32 );
}

/// Merge two CRC32.
[[nodiscard]] std::uint32_t crc32_combine( 
    std::uint32_t crcA, 
    std::uint32_t crcB, 
    std::size_t lengthB 
) noexcept;

/// Compute CRC32 [bitwise algorithm].
[[nodiscard]] std::uint32_t crc32_bitwise( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

/// Compute CRC32 [half-byte algorithm].
[[nodiscard]] std::uint32_t crc32_halfbyte( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

#ifdef CRC32_USE_LOOKUP_TABLE_BYTE

/// Compute CRC32 [standard algorithm].
[[nodiscard]] std::uint32_t crc32_1byte( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

#endif

/// Compute CRC32 [byte algorithm] without lookup tables.
[[nodiscard]] std::uint32_t crc32_1byte_tableless( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

/// Compute CRC32 [byte algorithm] without lookup tables.
[[nodiscard]] std::uint32_t crc32_1byte_tableless2( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

#ifdef CRC32_USE_LOOKUP_TABLE_SLICING_BY_4

/// compute CRC32 (Slicing-by-4 algorithm)
[[nodiscard]] std::uint32_t crc32_4bytes( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

#endif

#ifdef CRC32_USE_LOOKUP_TABLE_SLICING_BY_8

/// compute CRC32 (Slicing-by-8 algorithm)
[[nodiscard]] std::uint32_t crc32_8bytes( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

/// compute CRC32 (Slicing-by-8 algorithm), unroll inner loop 4 times
[[nodiscard]] std::uint32_t crc32_4x8bytes( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

#endif

#ifdef CRC32_USE_LOOKUP_TABLE_SLICING_BY_16

/// Compute CRC32 [Slicing-by-16 algorithm].
[[nodiscard]] std::uint32_t crc32_16bytes( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

[[nodiscard]] std::uint32_t crc32_2x16bytes( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0
) noexcept;

/// Compute CRC32 [Slicing-by-16 algorithm, prefetch upcoming data blocks].
[[nodiscard]] std::uint32_t crc32_16bytes_prefetch( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0,
    std::size_t prefetchAhead = 256
) noexcept;

[[nodiscard]] std::uint32_t crc32_2x16bytes_prefetch( 
    const void * data,
    std::size_t length,
    std::uint32_t previousCrc32 = 0,
    std::size_t prefetchAhead = 256
) noexcept;

#endif
