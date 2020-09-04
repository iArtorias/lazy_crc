#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <io.h>
#include <fcntl.h>

// date (https://github.com/HowardHinnant/date)
#include <date/date.h>

// {fmt} (https://github.com/fmtlib/fmt)
#include <fmt/os.h>
#include <fmt/format.h>

// Crc32 (https://github.com/stbrumme/crc32)
#include <crc32/Crc32.h>

// 4096 bytes
const size_t CHUNK_SIZE { 4 * 1024 };

// Common messages
constexpr const wchar_t * MSG_INFO_VERSION{ L"LazyCRC, {}\n\n" };
constexpr const wchar_t * MSG_INFO_USAGE{ L"usage: lazy_crc <file|directory>\n\nPress enter to exit the program...\n" };
constexpr const wchar_t * MSG_INFO_PROCESSING{ L"Processing '{}'\n" };
constexpr const wchar_t * MSG_INFO_PRESS_ENTER{ L"\nPress enter to exit the program...\n" };
constexpr const wchar_t * MSG_INFO_ELAPSED_TIME{ L"Elapsed time: {}h {}m {}s\n\nPress enter to exit the program...\n" };
constexpr const wchar_t * MSG_INFO_SFV_CREATED{ L"SFV file created '{}'\n" };
constexpr const wchar_t * MSG_ERROR_NOT_EXIST{ L"The specified file '{}' doesn't exist.\n\nPress enter to exit the program...\n" };
constexpr const wchar_t * MSG_ERROR_FILESIZE{ L"Unable to obtain the file size for {}\n" };
constexpr const wchar_t * MSG_ERROR_RELATIVE_PATH{ L"Unable to obtain the relative path for {}\n" };
constexpr const wchar_t * MSG_ERROR_UKNOWN_FILE{ L"The specified item is not a regular file or directory.\n\nPress enter to exit the program...\n" };

namespace fs = std::filesystem;
namespace ch = std::chrono;
namespace detail = fmt::v7::detail;

// Files map
std::map<fs::path, std::wstring> m_files{};

// Files mutex
std::mutex m_files_mtx;


// Write the message to console
template <typename S, typename... Args>
inline void msg_write( const S & format_str, const Args&... args )
{
    fmt::print( format_str, args... );
}


// Convert to the hex format
template <typename T>
inline std::wstring to_hex( T val, size_t width = sizeof( T ) * 2 )
{
    std::wstringstream stream{};

    stream << std::setfill( L'0' ) << std::setw( width ) <<
        std::hex << std::uppercase << (val | 0);

    return stream.str();
}


// Load the file, read it and calculate the CRC
inline void process_file(
    const fs::path & path_file,
    const fs::path & path_dir = "" )
{
    msg_write( MSG_INFO_PROCESSING, path_file.c_str() );

    std::ifstream file( path_file, std::ios::binary );

    if (file.good() || !file.fail())
    {
        std::error_code ec;
        auto const file_size = static_cast<size_t>(fs::file_size( path_file, ec ));

        if (ec)
        {
            msg_write( MSG_ERROR_FILESIZE, path_file.c_str() );
            file.close();

            return;
        }

        std::unique_ptr<char[]> buffer( new char[file_size] );

        uint32_t crc{ 0x0 };
        size_t bytes_processed{ 0x0 };

        while (bytes_processed < file_size)
        {
            auto bytes_left = file_size - bytes_processed;
            auto chunk_size = (CHUNK_SIZE < bytes_left) ? CHUNK_SIZE : bytes_left;

            auto const data = buffer.get() + bytes_processed;

            file.read( data, chunk_size );
            crc = crc32_16bytes_prefetch( data, chunk_size, crc );

            bytes_processed += chunk_size;
        }
        buffer.reset();

        if (!path_dir.empty())
        {
            auto relative = fs::path( fs::relative( path_file, path_dir, ec ).u16string() );

            if (ec)
            {
                msg_write( MSG_ERROR_RELATIVE_PATH, path_file.c_str() );
                file.close();

                return;
            }

            std::lock_guard guard( m_files_mtx );
            m_files.try_emplace( relative, to_hex( crc ) );
        }
        else
        {
            std::lock_guard guard( m_files_mtx );
            m_files.try_emplace( path_file.filename(), to_hex( crc ) );
        }
    }

    file.close();
}


// Write the output SFV file
inline void write_sfv( 
    const fs::path & path_sfv )
{
    if (!m_files.empty())
    {
        // We don't need the output SFV file inside
        auto it = m_files.find( path_sfv.filename() );
        if (it != m_files.end())
            m_files.erase( it );

        std::ofstream file( path_sfv );

        if (file.good() || !file.fail())
        {
            std::wstringstream data{};

            for (auto const & [path, hash] : m_files)
                data << path.c_str() << ' ' << hash << std::endl;

            auto result = detail::utf16_to_utf8( data.str() );
            file << result.str();
            file.close();

            msg_write( MSG_INFO_SFV_CREATED, path_sfv.c_str() );
        }
    }
}


int wmain( int argc, wchar_t **argv )
{
    _setmode( _fileno( stdout ), _O_U16TEXT );

    msg_write( MSG_INFO_VERSION, L"1.0.0" );

    if (argc < 0x2)
    {
        msg_write( MSG_INFO_USAGE );
        static_cast<void>(std::getchar());

        return -1;
    }

    // Full path to the operated file or directory
    auto path_file = fs::path( fs::path( argv[0x1] ).u16string() );

    // Full path to the output SFV file
    auto path_sfv = path_file.parent_path() / path_file.filename() += ".sfv";

    if (!fs::exists( path_file ))
    {
        msg_write( MSG_ERROR_NOT_EXIST, path_file.c_str() );
        static_cast<void>(std::getchar());

        return -1;
    }

    ch::steady_clock::time_point time_start, time_end;

    if (fs::is_directory( path_file ) && !fs::is_empty( path_file ))
    {
        time_start = ch::steady_clock::now();

        for (auto & entry : fs::recursive_directory_iterator( path_file ))
        {
            if (fs::is_regular_file( entry ))
                process_file( entry, path_file );
        }

        time_end = ch::steady_clock::now();
    }
    else if (fs::is_regular_file( path_file ))
    {
        time_start = ch::steady_clock::now();
        process_file( path_file );
        time_end = ch::steady_clock::now();
    }    
    else
    {
        msg_write( MSG_ERROR_UKNOWN_FILE );
        static_cast<void>(std::getchar());

        return -1;
    }

    // Write the output SFV file
    write_sfv( path_sfv );

    // Output the elapsed time
    auto time = date::make_time( time_end - time_start );
    msg_write( MSG_INFO_ELAPSED_TIME, time.hours().count(), time.minutes().count(), time.seconds().count() );
    static_cast<void>(std::getchar());

    return 0x0;
}