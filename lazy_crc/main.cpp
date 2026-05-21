#include <windows.h>
#include <iostream>
#include <cwctype>
#include <thread>
#include <chrono>
#include <filesystem>
#include <format>
#include <mutex>
#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <cstdio>
#include <io.h>
#include <fcntl.h>

// Crc32 (https://github.com/stbrumme/crc32)
#include <crc32/Crc32.h>

// Hash helper functions.
#include <hash_utils.h>

// Blake3 (https://github.com/BLAKE3-team/BLAKE3/tree/master/c)
#include "blake3/blake3.h"
#include "blake3/blake3_impl.h"

// Aligned buffer wrapper.
#include <aligned_buffer.h>

// Overlapped I/O wrapper.
#include "overlapped.h"

// Thread pool.
#include <thread_pool.h>

namespace fs = std::filesystem;
namespace ch = std::chrono;

// Version.
static constexpr const wchar_t * LAZYCRC_VERSION = L"2.0.0";

// Console messages.
static constexpr const wchar_t * MSG_INFO_VERSION = L"LazyCRC, {}\n\n";
static constexpr const wchar_t * MSG_INFO_USAGE = L"Usage:\n\nlazy_crc <file|directory> [--md5|--sha256|--sha512|--blake3] [--single] [--output <path>]\nor\nlazy_crc --files <file1> <file2> ... [--md5|--sha256|--sha512|--blake3] [--single] [--output <path>]\nor\nlazy_crc <path_to_checksum_file> --check\n\nPress 'Enter' to exit the program...\n";
static constexpr const wchar_t * MSG_INFO_PROCESSING = L"Processing '{}'";
static constexpr const wchar_t * MSG_INFO_PRESS_ENTER = L"\nPress 'Enter' to exit the program...\n";
static constexpr const wchar_t * MSG_INFO_ELAPSED_TIME = L"Elapsed time: {}h {}m {}s {}ms\n\nPress 'Enter' to exit the program...\n";
static constexpr const wchar_t * MSG_INFO_SFV_CREATED = L"SFV file created '{}'\n";
static constexpr const wchar_t * MSG_INFO_SFV_CHECK_OK = L"No errors happened while checking SFV file\n";
static constexpr const wchar_t * MSG_ERROR_FILE_OPEN = L"Can not open the specified file '{}'\n";
static constexpr const wchar_t * MSG_ERROR_SFV_FAILED = L"Bad files have been detected, more info inside '{}'\n";
static constexpr const wchar_t * MSG_ERROR_NOT_EXIST = L"The specified file '{}' doesn't exist.\n\nPress 'Enter' to exit the program...\n";
static constexpr const wchar_t * MSG_ERROR_FILESIZE = L"Unable to obtain the file size for {}\n";
static constexpr const wchar_t * MSG_ERROR_RELPATH = L"Unable to obtain the relative path for {}\n";
static constexpr const wchar_t * MSG_ERROR_UNKNOWN_FILE = L"The specified item is not a regular file or directory.\n\nPress 'Enter' to exit the program...\n";
static constexpr const wchar_t * MSG_NOT_FILE_OR_DIRECTORY = L"'{}' is not a regular file or directory\n\nPress 'Enter' to exit the program...\n";
static constexpr const wchar_t * MSG_OUTPUT_PATH_EMPTY = L"'--output' requires a path argument\n\nPress 'Enter' to exit the program...\n";
static constexpr const wchar_t * MSG_UNKNOWN_FLAG = L"Unknown flag '{}'\n\nPress 'Enter' to exit the program...\n";
static constexpr const wchar_t * MSG_MULTIPLE_POSITIONAL_ARGS = L"Multiple positional arguments. Use '--files' to hash several paths.\n\nPress 'Enter' to exit the program...\n";
static constexpr const wchar_t * MSG_FILES_ITEMS_EMPTY = L"'--files' requires at least one file or directory path\n\nPress 'Enter' to exit the program...\n";

// Global state.
struct PathHash
{
    // Hash a path by hashing its native string representation.
    std::size_t operator()( const fs::path & path ) const noexcept
    {
        return std::hash<std::wstring>{}(path.native());
    }
};

static std::unordered_map<fs::path, std::wstring, PathHash> FILES {};
static std::wstring BAD_FILES {};
static std::mutex BAD_MTX {};
static bool CHECK_SFV { false };
static bool SINGLE_THREAD { false };

// Hash algorithm selection.
enum class HashAlgo
{
    CRC32,
    MD5,
    SHA256,
    SHA512,
    BLAKE3
};

static HashAlgo HASH_ALGO { HashAlgo::CRC32 }; // Default to CRC32.


// Write a formatted wide string to stdout.
template <typename... Args>
inline void MsgWrite(
    std::wformat_string<Args...> fmt_str,
    Args&&... args
)
{
    ::_putws( std::format( fmt_str, std::forward<Args>( args )... ).c_str() );
}


// Convert integer to uppercase hex string.
template <typename T>
[[nodiscard]] inline std::wstring ToHex(
    T val,
    std::size_t width = sizeof( T ) * 2
)
{
    return std::format( L"{:0>{}X}", static_cast<std::make_unsigned_t<T>>(val), width );
}


// Convert a raw byte array to an uppercase hex wstring.
[[nodiscard]] inline std::wstring BytesToHex(
    const std::uint8_t * bytes,
    std::size_t count
)
{
    std::wstring out;
    out.reserve( count * 2 );

    for (std::size_t i = 0; i < count; ++i)
        out += std::format( L"{:02X}", bytes[i] );

    return out;
}


// Remove trailing occurrences of 'trim_char' from string.
[[nodiscard]] inline std::wstring TrimStr(
    std::wstring str,
    wchar_t trim_char
)
{
    while (!str.empty() && str.back() == trim_char)
        str.pop_back();

    return str;
}


// Convert string to uppercase [ASCII letters only, locale-independent].
[[nodiscard]] inline std::wstring StrToUppercase(
    std::wstring str
)
{
    std::ranges::transform( str, str.begin(),
        [] ( wchar_t c )
    {
        return static_cast<wchar_t>(std::toupper( c ));
    } );

    return str;
}


// Build a 'YYYYMMDD-HHMMSS' timestamp from the current local time.
// Used to suffix default named output files when '--output' is not provided.
[[nodiscard]] static std::wstring MakeTimestamp()
{
    const auto now = ch::system_clock::now();
    const auto now_t = ch::system_clock::to_time_t( now );

    std::tm tm {};
    ::localtime_s( &tm, &now_t );

    return std::format( L"{:04}{:02}{:02}-{:02}{:02}{:02}",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec );
}


// Append to bad files log and echo to console.
inline void AppendBadFiles(
    std::wstring_view path,
    std::wstring_view reason
)
{
    std::wstring entry;
    entry.reserve( path.size() + 1 + reason.size() + 1 );
    entry += path;
    entry += L' ';
    entry += reason;
    entry += L'\n';

    std::lock_guard guard( BAD_MTX );
    BAD_FILES += entry;
    ::_putws( entry.c_str() );
}


// Convert a wide string to UTF-8.
[[nodiscard]] static std::string WideToUtf8(
    std::wstring_view wide
)
{
    if (wide.empty())
        return {};

    const int len = ::WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        nullptr, 0,
        nullptr, nullptr );

    std::string result( static_cast<std::size_t>(len), '\0' );

    ::WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        result.data(), len,
        nullptr, nullptr );

    return result;
}


// Append wide string as UTF-8 directly into 'out', no intermediate allocation.
static inline void AppendWideAsUtf8(
    std::string & out,
    std::wstring_view wide
)
{
    if (wide.empty())
        return;

    const int len = ::WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        nullptr, 0,
        nullptr, nullptr );

    if (len <= 0)
        return;

    const std::size_t old_size = out.size();
    out.resize( old_size + static_cast<std::size_t>(len) );

    ::WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        out.data() + old_size, len,
        nullptr, nullptr );
}


// Write one SFV entry as UTF-8.
static void WriteUtf8Line(
    FILE * f,
    std::wstring_view path,
    std::wstring_view hash
)
{
    const std::string line = WideToUtf8( path ) + ' ' + WideToUtf8( hash ) + '\n';
    std::fwrite( line.data(), 1, line.size(), f );
}


// Plain CRT open.
[[nodiscard]] static FILE * TryOpenFile(
    const fs::path & file_path
)
{
    FILE * file = nullptr;
    errno_t err = _wfopen_s( &file, file_path.c_str(), L"rb" );

    if (err != 0)
    {
        MsgWrite( MSG_ERROR_FILE_OPEN, file_path.c_str() );
        return nullptr;
    }

    return file;
}


[[nodiscard]] static HANDLE TryOpenFileSeq(
    const fs::path & file_path,
    bool overlapped = false // 'false' means 'FILE_FLAG_SEQUENTIAL_SCAN' [synchronous], 'true' adds 'NO_BUFFERING | OVERLAPPED'.
)
{
    const DWORD flags = overlapped
        ? (FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED)
        : FILE_FLAG_SEQUENTIAL_SCAN;

    HANDLE handle = ::CreateFileW(
        file_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        flags,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE)
        MsgWrite( MSG_ERROR_FILE_OPEN, file_path.c_str() );

    return handle;
}


[[nodiscard]] static std::size_t GetFileSize(
    const fs::path & file_path,
    FILE * file_in
)
{
    std::error_code ec;
    auto size = static_cast<std::size_t>(fs::file_size( file_path, ec ));

    if (ec)
    {
        MsgWrite( MSG_ERROR_FILESIZE, file_path.c_str() );
        std::fclose( file_in );
        return static_cast<std::size_t>(-1);
    }

    return size;
}


[[nodiscard]] static fs::path GetRelativePath(
    const fs::path & file_path,
    const fs::path & dir_path,
    FILE * file_in
)
{
    std::error_code ec;
    fs::path relative = fs::relative( file_path, dir_path, ec );

    if (ec)
    {
        MsgWrite( MSG_ERROR_RELPATH, file_path.c_str() );
        std::fclose( file_in );
        return {};
    }

    return relative;
}


// CRC32 only. Conservative tiers tuned for cache friendly streaming.
[[nodiscard]] static constexpr std::size_t ChooseBlockSizeCRC32(
    std::size_t file_size
) noexcept
{
    if (file_size < 65'536)
        return 4'096; // 4 KiB

    if (file_size < 1'048'576)
        return 65'536; // 64 KiB

    if (file_size < 268'435'456)
        return 131'072; // 128 KiB

    if (file_size < 524'288'000)
        return 262'144; // 256 KiB

    if (file_size < 1'073'741'824)
        return  1'048'576; // 1 MiB

    return  4'194'304; // 4 MiB
}


// MD5, SHA-256, SHA-512 and BLAKE3. Larger tiers.
[[nodiscard]] static constexpr std::size_t ChooseBlockSizeHash(
    std::size_t file_size
) noexcept
{
    if (file_size < 65'536)
        return 4'096; // 4 KiB

    if (file_size < 1'048'576)
        return 65'536; // 64 KiB

    if (file_size < 268'435'456)
        return 524'288; // 512 KiB

    if (file_size < 1'073'741'824)
        return  4'194'304; // 4 MiB

    if (file_size < 4'294'967'296)
        return  8'388'608; // 8 MiB

    return 16'777'216; // 16 MiB
}


// Stream a file through various hashes.
[[nodiscard]] static std::wstring CalculateHash(
    HANDLE handle,
    std::size_t file_size
)
{
    static constexpr std::size_t SECTOR = 4096;

    std::size_t block_size = (HASH_ALGO == HashAlgo::CRC32)
        ? (file_size == 0 ? SECTOR : ChooseBlockSizeCRC32( file_size ))
        : (file_size == 0 ? SECTOR : ChooseBlockSizeHash( file_size ));

    block_size = (block_size + SECTOR - 1) & ~(SECTOR - 1);

    switch (HASH_ALGO)
    {
        case HashAlgo::CRC32:
        {
            std::uint32_t crc = 0;

            if (file_size >= OVERLAPPED_THRESHOLD)
            {
                AlignedBuffer bufs[2] = { AlignedBuffer( block_size ), AlignedBuffer( block_size ) };

                if (!bufs[0].Ok() || !bufs[1].Ok())
                    return {};

                OverlappedReadLoop( handle, file_size, static_cast<DWORD>(block_size), bufs,
                    [&] ( void * ptr, std::size_t n )
                {
                    crc = crc32_2x16bytes_prefetch( ptr, n, crc );
                } );
            }
            else
            {
                AlignedBuffer abuf( block_size );

                if (!abuf.Ok())
                    return {};

                std::size_t left = file_size;

                while (left > 0)
                {
                    const DWORD req = static_cast<DWORD>(std::min( block_size, left ));
                    const DWORD aligned = (req + SECTOR - 1) & ~(DWORD)(SECTOR - 1);

                    DWORD got = 0;
                    if (!::ReadFile( handle, abuf.m_Ptr, aligned, &got, nullptr ))
                        break;

                    const std::size_t num = std::min( static_cast<std::size_t>(got),
                        static_cast<std::size_t>(req) );

                    if (num == 0)
                        break;

                    crc = crc32_2x16bytes_prefetch( abuf.m_Ptr, num, crc );
                    left -= num;
                }
            }

            return ToHex( crc );
        }

        case HashAlgo::MD5:
        case HashAlgo::SHA256:
        case HashAlgo::SHA512:
        {
            const EvpAlgo evp_algo =
                (HASH_ALGO == HashAlgo::SHA256) ? EvpAlgo::SHA256 :
                (HASH_ALGO == HashAlgo::SHA512) ? EvpAlgo::SHA512 :
                EvpAlgo::MD5;

            const std::size_t digest_size =
                (HASH_ALGO == HashAlgo::SHA256) ? 32 :
                (HASH_ALGO == HashAlgo::SHA512) ? 64 : 16;

            EvpHashCtx ctx( evp_algo );

            if (file_size >= OVERLAPPED_THRESHOLD)
            {
                AlignedBuffer bufs[2] = { AlignedBuffer( block_size ), AlignedBuffer( block_size ) };

                if (!bufs[0].Ok() || !bufs[1].Ok())
                    return {};

                OverlappedReadLoop( handle, file_size, static_cast<DWORD>(block_size), bufs,
                    [&] ( void * ptr, std::size_t n )
                {
                    ctx.Update( ptr, n );
                } );
            }
            else
            {
                AlignedBuffer abuf( block_size );

                if (!abuf.Ok())
                    return {};

                std::size_t left = file_size;

                while (left > 0)
                {
                    const DWORD req = static_cast<DWORD>(std::min( block_size, left ));
                    const DWORD aligned = (req + SECTOR - 1) & ~(DWORD)(SECTOR - 1);

                    DWORD got = 0;
                    if (!::ReadFile( handle, abuf.m_Ptr, aligned, &got, nullptr ))
                        break;

                    const std::size_t num = std::min(
                        static_cast<std::size_t>(got),
                        static_cast<std::size_t>(req) );

                    if (num == 0)
                        break;

                    ctx.Update( abuf.m_Ptr, num );
                    left -= num;
                }
            }

            std::uint8_t digest[64] {};
            ctx.Final( digest );
            return BytesToHex( digest, digest_size );
        }
    }

    return {};
}


// BLAKE3. Double-buffered overlapped I/O for large files.
// 4 MiB per buffer = 4096 chunks.
[[nodiscard]] static std::wstring CalculateHashBLAKE3(
    HANDLE handle,
    std::size_t file_size
)
{
    static constexpr DWORD BUF_SIZE = 4 * 1024 * 1024;

    blake3_hasher ctx;
    blake3_hasher_init( &ctx );

    if (file_size >= OVERLAPPED_THRESHOLD)
    {
        AlignedBuffer bufs[2] = { AlignedBuffer( BUF_SIZE ), AlignedBuffer( BUF_SIZE ) };

        if (!bufs[0].Ok() || !bufs[1].Ok())
            return {};

        OverlappedReadLoop( handle, file_size, BUF_SIZE, bufs,
            [&] ( void * ptr, std::size_t n )
        {
            blake3_hasher_update( &ctx, ptr, n );
        } );
    }
    else
    {
        AlignedBuffer abuf( BUF_SIZE );

        if (!abuf.Ok())
            return {};

        while (true)
        {
            DWORD got = 0;
            if (!::ReadFile( handle, abuf.m_Ptr, BUF_SIZE, &got, nullptr ))
                break;

            if (got == 0)
                break;

            blake3_hasher_update( &ctx, abuf.m_Ptr, static_cast<std::size_t>(got) );
        }
    }

    std::uint8_t digest[BLAKE3_OUT_LEN];
    blake3_hasher_finalize( &ctx, digest, BLAKE3_OUT_LEN );
    return BytesToHex( digest, BLAKE3_OUT_LEN );
}


// Insert one result. Either single-file or SFV check mode.
// Always performed on the main thread.
static void InsertFile(
    const fs::path & file,
    std::wstring_view crc
)
{
    FILES.try_emplace( file, crc );
}


// Process a single file in directory mode.
// Called per file by the thread pool's dynamic scheduler.
static void ProcessOneFile(
    const FileEntry & entry,
    const fs::path & path_dir,
    std::vector<FileResult> & out,
    PrintQueue & pq
)
{
    // Build the log line.
    pq.Push( std::format( MSG_INFO_PROCESSING, entry.m_Path.c_str() ) );

    const bool use_overlapped = (entry.m_Size >= OVERLAPPED_THRESHOLD);
    HANDLE handle = TryOpenFileSeq( entry.m_Path, use_overlapped );

    if (handle == INVALID_HANDLE_VALUE)
        return;

    fs::path relative = entry.m_Path.lexically_relative( path_dir );

    if (relative.empty() || relative.native().starts_with( L".." ))
    {
        ::_putws( std::format( MSG_ERROR_RELPATH, entry.m_Path.c_str() ).c_str() );
        ::CloseHandle( handle );
        return;
    }

    const auto crc = (HASH_ALGO == HashAlgo::BLAKE3)
        ? CalculateHashBLAKE3( handle, static_cast<std::size_t>(entry.m_Size) )
        : CalculateHash( handle, static_cast<std::size_t>(entry.m_Size) );

    ::CloseHandle( handle );

    out.push_back( { std::move( relative ), crc } );
}


// Each item carries both the absolute path to open
// and the relative path to write into the checksum file.
struct ListItem
{
    fs::path m_Abs;
    fs::path m_Relative;
    std::uintmax_t m_Size;
};


static void ProcessListItem(
    const ListItem & item,
    std::vector<FileResult> & out,
    PrintQueue & pq
)
{
    pq.Push( std::format( MSG_INFO_PROCESSING, item.m_Abs.c_str() ) );

    const bool use_overlapped = (item.m_Size >= OVERLAPPED_THRESHOLD);
    HANDLE handle = TryOpenFileSeq( item.m_Abs, use_overlapped );

    if (handle == INVALID_HANDLE_VALUE)
        return;

    const auto crc = (HASH_ALGO == HashAlgo::BLAKE3)
        ? CalculateHashBLAKE3( handle, static_cast<std::size_t>(item.m_Size) )
        : CalculateHash( handle, static_cast<std::size_t>(item.m_Size) );

    ::CloseHandle( handle );

    out.push_back( { item.m_Relative, crc } );
}


// Process a single file in single file mode.
static void ProcessFileSingle(
    const fs::path & path_file,
    std::vector<char> & buf
)
{
    MsgWrite( MSG_INFO_PROCESSING, path_file.c_str() );

    std::error_code ec;
    const auto size = static_cast<std::size_t>(fs::file_size( path_file, ec ));

    if (ec)
    {
        MsgWrite( MSG_ERROR_FILESIZE, path_file.c_str() );
        return;
    }

    const bool use_overlapped = (size >= OVERLAPPED_THRESHOLD);
    HANDLE handle = TryOpenFileSeq( path_file, use_overlapped );

    if (handle == INVALID_HANDLE_VALUE)
        return;

    const auto crc = (HASH_ALGO == HashAlgo::BLAKE3)
        ? CalculateHashBLAKE3( handle, size )
        : CalculateHash( handle, size );

    ::CloseHandle( handle );

    InsertFile( path_file.filename(), crc );
}


// SFV check mode.
// Parse an SFV file and verify each listed file.
static void ProcessSFVCheck(
    const fs::path & path_file
)
{
    MsgWrite( MSG_INFO_PROCESSING, path_file.c_str() );

    FILE * raw_sfv = TryOpenFile( path_file );

    if (!raw_sfv)
        return;

    const auto parent_path = path_file.parent_path();
    const auto ext = StrToUppercase( path_file.extension().wstring() );

    // Read all lines first.
    struct SFVEntry
    {
        fs::path m_Path;
        std::wstring m_Hash;
        std::uintmax_t m_Size { 0 };
    };

    std::vector<SFVEntry> sfv_entries;
    std::string utf8_line;

    auto read_utf8_line = [&] ( FILE * f, std::string & out ) -> bool
    {
        out.clear();
        int c;

        while ((c = std::fgetc( f )) != EOF)
        {
            if (c == '\r')
                continue;

            if (c == '\n')
                return true;

            out += static_cast<char>(c);
        }

        return !out.empty();
    };

    auto utf8_to_wide = [] ( const std::string & utf8 ) -> std::wstring
    {
        if (utf8.empty())
            return {};

        const int len = ::MultiByteToWideChar( CP_UTF8, 0,
            utf8.data(), static_cast<int>(utf8.size()), nullptr, 0 );

        std::wstring wide( static_cast<std::size_t>(len), L'\0' );

        ::MultiByteToWideChar( CP_UTF8, 0,
            utf8.data(), static_cast<int>(utf8.size()), wide.data(), len );

        return wide;
    };

    auto parse_line = [](
        const std::wstring & line,
        std::wstring & out_path,
        std::wstring & out_hash ) -> bool
    {
        const auto pos = line.rfind( L' ' );

        if (pos == std::wstring::npos || pos == 0)
            return false;

        const std::wstring_view hash = std::wstring_view( line ).substr( pos + 1 );
        const std::size_t size = hash.size();

        if (size != 8 && size != 32 && size != 64 && size != 128)
            return false;

        for (wchar_t c : hash)
            if (!std::iswxdigit( c ))
                return false;

        out_path = line.substr( 0, pos );
        out_hash = hash;
        return true;
    };

    bool algo_detected = false;

    while (read_utf8_line( raw_sfv, utf8_line ))
    {
        if (utf8_line.empty() || utf8_line.front() == ';')
            continue;

        std::wstring line = utf8_to_wide( utf8_line );
        std::wstring path_str, hash_str;

        if (!parse_line( line, path_str, hash_str ))
            continue;

        // Auto detect once.
        if (!algo_detected && HASH_ALGO == HashAlgo::CRC32)
        {
            switch (hash_str.size())
            {
                case 32:
                    HASH_ALGO = HashAlgo::MD5;
                    break;

                case 64:
                    HASH_ALGO = (ext == L".B3SUM") ? HashAlgo::BLAKE3 : HashAlgo::SHA256;
                    break;

                case 128:
                    HASH_ALGO = HashAlgo::SHA512;
                    break;
            }

            algo_detected = true;
        }

        fs::path full_path = parent_path / path_str;

        std::error_code ec;
        const auto size = static_cast<std::uintmax_t>(fs::file_size( full_path, ec ));

        if (ec)
        {
            AppendBadFiles( path_str, L"Unable to obtain the file size" );
            continue;
        }

        sfv_entries.push_back( { std::move( full_path ), StrToUppercase( hash_str ), size } );
    }

    std::fclose( raw_sfv );

    if (sfv_entries.empty())
        return;

    // Build 'FileEntry' list and run through thread pool.
    std::vector<FileEntry> file_entries;
    file_entries.reserve( sfv_entries.size() );

    for (const auto & entry : sfv_entries)
        file_entries.push_back( { entry.m_Path, entry.m_Size } );

    // Build expected hash lookup.
    std::unordered_map<fs::path, std::wstring, PathHash> expected;
    expected.reserve( sfv_entries.size() );

    for (const auto & entry : sfv_entries)
        expected.emplace( entry.m_Path, entry.m_Hash );

    PrintQueue print_queue;

    const auto hw = std::thread::hardware_concurrency();
    const std::size_t threads_count = SINGLE_THREAD ? 1u : (hw > 2u ? hw - 1u : 1u);

    // Worker that hashes and compares.
    auto check_worker = [](
        const FileEntry & entry,
        const fs::path &,
        std::vector<FileResult> & out,
        PrintQueue & pq ) -> void
    {
        pq.Push( std::format( MSG_INFO_PROCESSING, entry.m_Path.c_str() ) );

        const bool use_overlapped = (entry.m_Size >= OVERLAPPED_THRESHOLD);
        HANDLE handle = TryOpenFileSeq( entry.m_Path, use_overlapped );

        if (handle == INVALID_HANDLE_VALUE)
        {
            out.push_back( { entry.m_Path, L"__OPEN_FAILED__" } );
            return;
        }

        const auto crc = (HASH_ALGO == HashAlgo::BLAKE3)
            ? CalculateHashBLAKE3( handle, entry.m_Size )
            : CalculateHash( handle, entry.m_Size );

        ::CloseHandle( handle );
        out.push_back( { entry.m_Path, crc } );
    };

    ThreadPool pool( threads_count );
    auto results = pool.Run( file_entries, parent_path, check_worker, print_queue );

    print_queue.Stop();

    // Compare results against expected.
    for (const auto & result : results)
    {
        const auto it = expected.find( result.m_Relative );

        if (it == expected.end())
            continue;

        if (result.m_CRC == L"__OPEN_FAILED__")
            AppendBadFiles( result.m_Relative.wstring(), L"Unable to open the file" );

        else if (result.m_CRC != it->second)
            AppendBadFiles( result.m_Relative.wstring(), L"CRC does not match" );
    }
}


// Write the SFV file with the calculated CRCs.
static void WriteSFV(
    const fs::path & path_sfv,
    const fs::path & exe_name
)
{
    if (CHECK_SFV)
    {
        if (!BAD_FILES.empty())
        {
            const auto bad_path = path_sfv.parent_path() / L"LazyCRC_BadFiles.log";
            FILE * bad_file = nullptr;

            if (_wfopen_s( &bad_file, bad_path.c_str(), L"wb" ) == 0 && bad_file)
            {
                std::string utf8;
                utf8.reserve( BAD_FILES.size() );

                AppendWideAsUtf8( utf8, BAD_FILES );

                std::fwrite( utf8.data(), 1, utf8.size(), bad_file );
                std::fclose( bad_file );
            }

            MsgWrite( MSG_ERROR_SFV_FAILED, bad_path.c_str() );
        }
        else
        {
            MsgWrite( MSG_INFO_SFV_CHECK_OK );
        }

        return;
    }

    if (FILES.empty())
        return;

    const auto sfv_name = path_sfv.filename();

    // Single pass.
    // Filter out SFV and executable, collect pointers, and
    // calculate how many bytes the output buffer will need.
    using Entry = std::pair<const fs::path, std::wstring>;

    std::vector<const Entry *> sorted;
    sorted.reserve( FILES.size() );

    std::size_t wide_chars_total = 0;

    for (const auto & kv : FILES)
    {
        const auto name = kv.first.filename();

        if (name == sfv_name || name == exe_name)
            continue;

        sorted.push_back( &kv );

        // Each line will have the file path, a space, the hash, and a newline.
        wide_chars_total += kv.first.native().size() * 3 + kv.second.size() + 2;
    }

    if (sorted.empty())
        return;

    std::ranges::sort( sorted, [] ( const Entry * a, const Entry * b )
    {
        return a->first.native() < b->first.native();
    } );

    std::string out_buf;
    out_buf.reserve( wide_chars_total );

    for (const Entry * entry : sorted)
    {
        AppendWideAsUtf8( out_buf, entry->first.native() );
        out_buf.push_back( ' ' );

        AppendWideAsUtf8( out_buf, entry->second );
        out_buf.push_back( '\n' );
    }

    FILE * out = nullptr;
    if (_wfopen_s( &out, path_sfv.c_str(), L"wb" ) != 0 || !out)
        return;

    // Skip stdio's internal buffering.
    ::setvbuf( out, nullptr, _IONBF, 0 );
    std::fwrite( out_buf.data(), 1, out_buf.size(), out );
    std::fclose( out );

    MsgWrite( MSG_INFO_SFV_CREATED, path_sfv.c_str() );
}


int wmain(
    int argc,
    wchar_t ** argv
)
{
    #pragma warning(push)
    #pragma warning(disable: 6031)
    _setmode( _fileno( stdout ), _O_U16TEXT );
    #pragma warning(pop)

    MsgWrite( MSG_INFO_VERSION, LAZYCRC_VERSION );

    if (argc < 2)
    {
        MsgWrite( MSG_INFO_USAGE );
        static_cast<void>( std::getchar() );
        return -1;
    }

    // Default hash file extension. Adjusted by '--md5', '--sha256', '--sha512', '--blake3'.
    std::wstring sfv_ending = L".sfv";

    // Items collected from '--files' arguments. Each carries the absolute path
    // to open and the relative path to write into the checksum file.
    std::vector<ListItem> files_items;
    bool files_mode = false;

    // The first '--files' argument. Used to derive the
    // default output name '{parent_folder_name}-{timestamp}'.
    fs::path files_first_arg;

    // User supplied output path via '--output'. Empty means use default naming.
    fs::path output_arg;
    bool has_output = false;

    // Positional non flag argument, used when '--files' is not present.
    // Holds the index into argv so we can resolve it after parsing flags.
    int positional_idx = 0;

    // Classify an argv element as 'known flag' so '--files' knows
    // where its list of paths ends.
    auto is_known_flag = [] ( const wchar_t * s ) -> bool
    {
        return std::wcscmp( s, L"--check" ) == 0
            || std::wcscmp( s, L"--single" ) == 0
            || std::wcscmp( s, L"--md5" ) == 0
            || std::wcscmp( s, L"--sha256" ) == 0
            || std::wcscmp( s, L"--sha512" ) == 0
            || std::wcscmp( s, L"--blake3" ) == 0
            || std::wcscmp( s, L"--output" ) == 0
            || std::wcscmp( s, L"--files" ) == 0;
    };

    // Resolve a single '--files' argument [file or folder] into one or more
    // 'ListItem' entries. Folders are walked recursively, with relative paths
    // anchored at the folder's parent so the folder name itself appears in
    // the checksum file.
    auto resolve_files_arg = [](
        const fs::path & raw,
        std::vector<ListItem> & out
        ) -> bool
    {
        const fs::path abs = fs::absolute( raw );

        if (!fs::exists( abs ))
        {
            MsgWrite( MSG_ERROR_NOT_EXIST, abs.c_str() );
            return false;
        }

        if (fs::is_regular_file( abs ))
        {
            std::error_code ec;
            const auto size = fs::file_size( abs, ec );

            if (ec)
            {
                MsgWrite( MSG_ERROR_FILESIZE, abs.c_str() );
                return false;
            }

            out.push_back( { abs, abs.filename(), size } );
            return true;
        }

        if (fs::is_directory( abs ))
        {
            // Walk the folder and anchor relative paths at its parent so the
            // folder's own name is part of every entry [e.g. 'mydir/sub/x.bin'].
            const fs::path anchor = abs.parent_path().empty() ? abs : abs.parent_path();

            for (auto & entry : fs::recursive_directory_iterator(
                abs, fs::directory_options::skip_permission_denied ))
            {
                if (!entry.is_regular_file())
                    continue;

                if (entry.path().filename() == L"$RECYCLE.BIN")
                    continue;

                fs::path relative = entry.path().lexically_relative( anchor );

                if (relative.empty())
                    relative = entry.path().filename();

                out.push_back( { entry.path(), std::move( relative ), entry.file_size() } );
            }

            return true;
        }

        MsgWrite( MSG_NOT_FILE_OR_DIRECTORY,
            abs.c_str() );
        return false;
    };

    // Parse flags. '--files' and '--output' both consume the next argument(s).
    for (int i = 1; i < argc; ++i)
    {
        if (std::wcscmp( argv[i], L"--check" ) == 0)
        {
            CHECK_SFV = true;
            continue;
        }

        if (std::wcscmp( argv[i], L"--single" ) == 0)
        {
            SINGLE_THREAD = true;
            continue;
        }

        if (std::wcscmp( argv[i], L"--md5" ) == 0)
        {
            sfv_ending = L".md5";
            HASH_ALGO = HashAlgo::MD5;
            continue;
        }

        if (std::wcscmp( argv[i], L"--sha256" ) == 0)
        {
            sfv_ending = L".sha256";
            HASH_ALGO = HashAlgo::SHA256;
            continue;
        }

        if (std::wcscmp( argv[i], L"--sha512" ) == 0)
        {
            sfv_ending = L".sha512";
            HASH_ALGO = HashAlgo::SHA512;
            continue;
        }

        if (std::wcscmp( argv[i], L"--blake3" ) == 0)
        {
            sfv_ending = L".b3sum";
            HASH_ALGO = HashAlgo::BLAKE3;
            continue;
        }

        if (std::wcscmp( argv[i], L"--output" ) == 0)
        {
            if (i + 1 >= argc)
            {
                MsgWrite( MSG_OUTPUT_PATH_EMPTY );
                static_cast<void>(std::getchar());
                return -1;
            }

            output_arg = fs::absolute( fs::path( argv[++i] ) );
            has_output = true;
            continue;
        }

        if (std::wcscmp( argv[i], L"--files" ) == 0)
        {
            files_mode = true;

            // Consume subsequent arguments until the next known flag.
            // Each argument may be a file [hashed as-is] or a folder
            // [walked recursively].
            while (i + 1 < argc && !is_known_flag( argv[i + 1] ))
            {
                const fs::path raw( argv[++i] );

                if (files_first_arg.empty())
                    files_first_arg = fs::absolute( raw );

                if (!resolve_files_arg( raw, files_items ))
                {
                    static_cast<void>(std::getchar());
                    return -1;
                }
            }

            continue;
        }

        // Anything else is a positional argument. Only one is allowed
        // [in non '--files' mode]. Reject anything that looks like an
        // unknown flag to catch typos like '--md55'.
        if (std::wcsncmp( argv[i], L"--", 2 ) == 0)
        {
            MsgWrite( MSG_UNKNOWN_FLAG, argv[i] );
            static_cast<void>(std::getchar());
            return -1;
        }

        if (positional_idx != 0)
        {
            MsgWrite( MSG_MULTIPLE_POSITIONAL_ARGS );
            static_cast<void>(std::getchar());
            return -1;
        }

        positional_idx = i;
    }

    if (files_mode && files_items.empty())
    {
        MsgWrite( MSG_FILES_ITEMS_EMPTY );
        static_cast<void>(std::getchar());
        return -1;
    }

    if (!files_mode && positional_idx == 0)
    {
        MsgWrite( MSG_INFO_USAGE );
        static_cast<void>(std::getchar());
        return -1;
    }

    // Resolve input path and default output path.
    // 
    // 'path_file' is the single positional argument
    // 'path_sfv' is the destination for the checksum file.
    fs::path path_file;
    fs::path path_sfv;

    if (files_mode)
    {
        // The default output filename uses '{parent_folder_name}-{timestamp}':
        // 
        // '--files myfolder' produces 'myfolder-<ts>.<ext>' next to myfolder.
        // '--files a.txt b.txt' produces '<parent dir name>-<ts>.<ext>' next to those files.
        // 
        // 'anchor_dir' is where the output file is written.
        // 'anchor_name' is the folder name used in the filename.
        fs::path anchor_dir;
        std::wstring anchor_name;

        if (fs::is_directory( files_first_arg ))
        {
            anchor_dir = files_first_arg.has_parent_path()
                ? files_first_arg.parent_path() : files_first_arg;

            anchor_name = files_first_arg.filename().empty()
                ? std::wstring( L"files" )
                : files_first_arg.filename().wstring();
        }
        else
        {
            anchor_dir = files_first_arg.parent_path();
            anchor_name = anchor_dir.filename().empty()
                ? std::wstring( L"files" )
                : anchor_dir.filename().wstring();
        }

        if (has_output)
        {
            if (fs::is_directory( output_arg ))
                path_sfv = output_arg / (anchor_name + sfv_ending);
            else
                path_sfv = output_arg;
        }
        else
        {
            // '{parent_folder_name}-{timestamp}{extension}'.
            path_sfv = anchor_dir / (anchor_name + L"-" + MakeTimestamp() + sfv_ending);
        }
    }
    else
    {
        // Resolve the single positional argument.
        path_file = fs::absolute( fs::path( argv[positional_idx] ) );

        if (!fs::exists( path_file ))
        {
            MsgWrite( MSG_ERROR_NOT_EXIST, path_file.c_str() );
            static_cast<void>(std::getchar());
            return -1;
        }

        if (has_output)
        {
            if (fs::is_directory( output_arg ))
                path_sfv = output_arg / (path_file.filename().wstring() + sfv_ending);
            else
                path_sfv = output_arg;
        }
        else
        {
            // Default is '<input_name>-<timestamp><extension>' alongside the input.
            const fs::path parent = path_file.parent_path();
            path_sfv = parent / (path_file.filename().wstring() + L"-" + MakeTimestamp() + sfv_ending);
        }
    }

    if (!CHECK_SFV)
    {
        switch (HASH_ALGO)
        {
            case HashAlgo::CRC32:
                MsgWrite( L"Calculating CRC32\n\n" );
                break;

            case HashAlgo::MD5:
                MsgWrite( L"Calculating MD5\n\n" );
                break;

            case HashAlgo::SHA256:
                MsgWrite( L"Calculating SHA-256\n\n" );
                break;

            case HashAlgo::SHA512:
                MsgWrite( L"Calculating SHA-512\n\n" );
                break;

            case HashAlgo::BLAKE3:
            {
                const std::size_t degree = blake3_simd_degree();

                // Report active SIMD tier.
                const wchar_t * tier =
                    degree >= 16 ? L"AVX-512 [degree 16]" :
                    degree >= 8 ? L"AVX2 [degree 8]" :
                    degree >= 4 ? L"SSE4.1 [degree 4]" :
                    degree >= 2 ? L"SSE2 [degree 2]" : L"portable [degree 1]";

                MsgWrite( L"Calculating BLAKE3, SIMD: {}\n\n", tier );
                break;
            }
        }
    }

    wchar_t exe_path_buf[MAX_PATH] {};
    ::GetModuleFileNameW( nullptr, exe_path_buf, MAX_PATH );
    const fs::path exe_abs { exe_path_buf };
    const fs::path exe_name = exe_abs.filename();

    ch::steady_clock::time_point time_start, time_end;

    if (files_mode)
    {
        PrintQueue print_queue;
        time_start = ch::steady_clock::now();

        const std::size_t total = files_items.size();

        const auto hw = std::thread::hardware_concurrency();
        const std::size_t threads_count = SINGLE_THREAD
            ? 1u
            : (hw > 2u ? static_cast<std::size_t>(hw) - 1u : 1u);

        if (threads_count <= 1u)
        {
            std::vector<FileResult> results;
            results.reserve( total );

            for (const auto & item : files_items)
                ProcessListItem( item, results, print_queue );

            FILES.reserve( results.size() );

            for (auto & result : results)
                FILES.try_emplace( std::move( result.m_Relative ), std::move( result.m_CRC ) );
        }
        else
        {
            // Hand rolled parallel loop.
            std::atomic<std::size_t> next_idx { 0 };
            std::vector<std::vector<FileResult>> slots( threads_count );

            std::vector<std::thread> workers;
            workers.reserve( threads_count );

            for (std::size_t t = 0; t < threads_count; ++t)
            {
                workers.emplace_back( [&, t]
                {
                    while (true)
                    {
                        const std::size_t i = next_idx.fetch_add(
                            1, std::memory_order_relaxed );

                        if (i >= total)
                            break;

                        ProcessListItem( files_items[i], slots[t], print_queue );
                    }

                    print_queue.FlushLocal();
                } );
            }

            for (auto & worker : workers)
                worker.join();

            print_queue.Drain();

            std::size_t total_out = 0;

            for (const auto & slot : slots)
                total_out += slot.size();

            FILES.reserve( total_out );

            for (auto & slot : slots)
                for (auto & result : slot)
                    FILES.try_emplace( std::move( result.m_Relative ), std::move( result.m_CRC ) );
        }

        print_queue.Stop();
        time_end = ch::steady_clock::now();
    }
    else if (fs::is_directory( path_file ) && !fs::is_empty( path_file ))
    {
        // Collect all entries before starting workers.
        std::vector<FileEntry> entries;

        PrintQueue print_queue;

        time_start = ch::steady_clock::now();

        for (auto & entry : fs::recursive_directory_iterator(
            path_file, fs::directory_options::skip_permission_denied ))
        {
            if (!entry.is_regular_file())
                continue;

            if (entry.path().filename() == L"$RECYCLE.BIN") // Skip Recycle Bin, it can cause access issues.
                continue;

            if (entry.path() == exe_abs) // Exclude the executable itself.
                continue;

            entries.push_back( { entry.path(), entry.file_size() } );
        }

        if (SINGLE_THREAD)
        {
            std::vector<FileResult> results;
            results.reserve( entries.size() );

            for (const auto & entry : entries)
                ProcessOneFile( entry, path_file, results, print_queue );

            FILES.reserve( results.size() );

            for (auto & result : results)
                FILES.try_emplace( std::move( result.m_Relative ), std::move( result.m_CRC ) );

            print_queue.Stop();
        }
        else
        {
            const auto hw = std::thread::hardware_concurrency();
            const std::size_t threads_count = hw > 2u ? hw - 1u : 1u;

            ThreadPool pool( threads_count );
            auto results = pool.Run( entries, path_file, ProcessOneFile, print_queue );

            FILES.reserve( results.size() );

            for (auto & result : results)
                FILES.try_emplace( std::move( result.m_Relative ), std::move( result.m_CRC ) );

            print_queue.Stop();
        }

        time_end = ch::steady_clock::now();
    }
    else if (fs::is_regular_file( path_file ))
    {
        std::vector<char> buf;
        time_start = ch::steady_clock::now();

        if (CHECK_SFV)
            ProcessSFVCheck( path_file );
        else
            ProcessFileSingle( path_file, buf );

        time_end = ch::steady_clock::now();
    }
    else
    {
        MsgWrite( MSG_ERROR_UNKNOWN_FILE );
        static_cast<void>(std::getchar());
        return -1;
    }

    WriteSFV( path_sfv, exe_name );

    // Elapsed time.
    const auto elapsed = time_end - time_start;
    const auto hours = ch::duration_cast<ch::hours>(elapsed);
    const auto minutes = ch::duration_cast<ch::minutes>(elapsed - hours);
    const auto seconds = ch::duration_cast<ch::seconds>(elapsed - hours - minutes);
    const auto ms = ch::duration_cast<ch::milliseconds>(elapsed - hours - minutes - seconds);

    MsgWrite( MSG_INFO_ELAPSED_TIME,
        hours.count(), minutes.count(), seconds.count(), ms.count() );

    static_cast<void>(std::getchar());
    return 0;
}