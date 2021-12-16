#include <iostream>
#include <sstream>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <io.h>
#include <fcntl.h>
#include <regex>

// date (https://github.com/HowardHinnant/date)
#include <date/date.h>

// {fmt} (https://github.com/fmtlib/fmt)
#include <fmt/os.h>
#include <fmt/format.h>

// Crc32 (https://github.com/stbrumme/crc32)
#include <crc32/Crc32.h>

// 1 Mb
const size_t CHUNK_SIZE { 1024 * 1024 };

// Common messages
constexpr const wchar_t * MSG_INFO_VERSION{ L"LazyCRC, {}\n\n" };
constexpr const wchar_t * MSG_INFO_USAGE{ L"usage: lazy_crc <file|directory>\nor\nlazy_crc <path_to_sfv_file> --check\n\nPress enter to exit the program...\n" };
constexpr const wchar_t * MSG_INFO_PROCESSING{ L"Processing '{}'\n" };
constexpr const wchar_t * MSG_INFO_PRESS_ENTER{ L"\nPress enter to exit the program...\n" };
constexpr const wchar_t * MSG_INFO_ELAPSED_TIME{ L"Elapsed time: {}h {}m {}s\n\nPress enter to exit the program...\n" };
constexpr const wchar_t * MSG_INFO_SFV_CREATED{ L"SFV file created '{}'\n" };
constexpr const wchar_t * MSG_INFO_SFV_CHECK_SUCCESS{ L"No errors happened while checking SFV file\n" };
constexpr const wchar_t * MSG_ERROR_FILE_OPEN{ L"Can not open the specified file '{}'\n" };
constexpr const wchar_t * MSG_ERROR_SFV_CHECK_FAILED{ L"Bad files have been detected, more info inside '{}'\n" };
constexpr const wchar_t * MSG_ERROR_NOT_EXIST{ L"The specified file '{}' doesn't exist.\n\nPress enter to exit the program...\n" };
constexpr const wchar_t * MSG_ERROR_FILESIZE{ L"Unable to obtain the file size for {}\n" };
constexpr const wchar_t * MSG_ERROR_RELATIVE_PATH{ L"Unable to obtain the relative path for {}\n" };
constexpr const wchar_t * MSG_ERROR_UNKNOWN_FILE{ L"The specified item is not a regular file or directory.\n\nPress enter to exit the program...\n" };

namespace fs = std::filesystem;
namespace ch = std::chrono;
namespace detail = fmt::v7::detail;

// Files map
std::map<fs::path, std::wstring> m_files{};

// Bad files
std::u16string m_bad_files{};

// Files mutex
std::mutex m_files_mtx;

// Should we check the SFV file instead?
bool m_check_sfv{ false };


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


// Trim the string
inline std::wstring trim_str( std::wstring str, wchar_t trim_char )
{
    while (str.back() == trim_char)
        str.pop_back();

    return str;
}


// Convert u16string to wstring
inline std::wstring u16_to_wstring( std::u16string_view ustr )
{
    return std::wstring( ustr.begin(), ustr.end() );
}


// Append the string with 'bad' files (does not exist, invalid CRC etc)
inline void append_bad_files( std::u16string ustr, std::u16string reason )
{
    m_bad_files += ustr += std::u16string( u" " ) += reason += u"\n";
    msg_write( u16_to_wstring( m_bad_files ) );
}


// Load the file, read it and calculate the CRC
inline void process_file(
    const fs::path& path_file,
    const fs::path& path_dir = "" )
{
    msg_write( MSG_INFO_PROCESSING, path_file.c_str() );

    // Try to open the required file
    auto try_open_file = [] ( const fs::path& file_path )
    {
        std::ifstream file( file_path, std::ios::binary );

        if (!file.good() || file.fail())
        {
            msg_write( MSG_ERROR_FILE_OPEN, file_path.c_str() );
            return std::ifstream{};
        }

        return file;
    };

    // Get the required file size
    auto get_file_size = [] ( const fs::path& file_path, std::ifstream& file_in ) -> size_t
    {
        std::error_code ec;
        auto size = static_cast<size_t>(fs::file_size( file_path, ec ));

        if (ec)
        {
            msg_write( MSG_ERROR_FILESIZE, file_path.c_str() );
            file_in.close();

            return -1;
        }

        return size;
    };

    // Obtain the relative path
    auto get_relative_path = [] ( const fs::path& file_path, const fs::path& dir_path, std::ifstream& file_in ) -> fs::path
    {
        std::error_code ec;
        auto relative = fs::path( fs::relative( file_path, dir_path, ec ).u16string() );

        if (ec)
        {
            msg_write( MSG_ERROR_RELATIVE_PATH, file_path.c_str() );
            file_in.close();

            return fs::path{};
        }

        return relative;
    };

    // Calculate the CRC hash
    auto calculate_crc = [] ( std::ifstream& file_in, const size_t& file_size )
    {
        uint32_t crc{ 0x0 };
        size_t bytes_processed{ 0x0 };

        while (bytes_processed < file_size)
        {
            auto bytes_left = file_size - bytes_processed;
            auto chunk_size = (CHUNK_SIZE < bytes_left) ? CHUNK_SIZE : bytes_left;

            std::unique_ptr<char[]> buffer( new char[chunk_size] );
            auto const data = buffer.get();

            file_in.read( data, chunk_size );
            crc = crc32_2x16bytes_prefetch( data, chunk_size, crc );
            buffer.reset();

            bytes_processed += chunk_size;
            file_in.seekg( bytes_processed, std::ios::beg );
        }

        return crc;
    };

    // Insert the files to the map (including CRC)
    auto insert_files = [] ( const fs::path& file, std::wstring_view crc  )
    {
        std::lock_guard guard( m_files_mtx );
        m_files.try_emplace( file, crc );
    };

    auto file = try_open_file( path_file );

    if (file)
    {
        auto size = get_file_size( path_file, file );

        if (size != -1)
        {
            if (!path_dir.empty())
            {
                auto relative = get_relative_path( path_file, path_dir, file );

                if (!relative.empty())
                {
                    auto const crc = to_hex( calculate_crc( file, size ) );
                    insert_files( relative, crc );
                }
            }
            else
            {
                if (m_check_sfv)
                {
                    std::basic_ifstream<char16_t> file_sfv( path_file );
                    auto const parent_path = path_file.parent_path();

                    // Read all the SFV file contents, line by line
                    for (std::u16string line; getline( file_sfv, line ); )
                    {
                        if (!line.empty())
                        {
                            if (line.front() != u';') // Exclude comments (QuickSFV style)
                            {
                                // some_fILE Example.bin DEADC0DE
                                std::wregex regex( LR"(^(.* )?([a-fA-F0-9]{8})$)", std::wregex::extended );
                                std::wsmatch regex_match{};

                                // Converted line
                                auto line_conv = u16_to_wstring( line );

                                if (std::regex_search( line_conv, regex_match, regex ))
                                {
                                    fs::path path_in_sfv = fs::path( trim_str( regex_match[1].str(), ' ' ) );
                                    std::wstring crc_in_sfv = regex_match[2].str();

                                    if (!path_in_sfv.empty() && !crc_in_sfv.empty())
                                    {
                                        auto path_in_sfv_full = parent_path / path_in_sfv;
                                        auto file_crc = try_open_file( path_in_sfv_full );

                                        if (!file_crc || file_crc.peek() == std::ifstream::traits_type::eof())
                                            append_bad_files( path_in_sfv.u16string(), u"Unable to open the file" );
                                        else
                                        {
                                            size = get_file_size( path_in_sfv_full, file_crc );

                                            if (size == -1)
                                                append_bad_files( path_in_sfv.u16string(), u"Unable to obtain the file size" );
                                            else
                                            {
                                                auto const crc = to_hex( calculate_crc( file_crc, size ) );

                                                if (crc != crc_in_sfv)
                                                    append_bad_files( path_in_sfv.u16string(), u"CRC does not match" );
                                            }
                                        }

                                        file_crc.close();
                                    }
                                }
                            }
                        }
                    }

                    file_sfv.close();
                }
                else
                {
                    auto const crc = to_hex( calculate_crc( file, size ) );
                    insert_files( path_file.filename(), crc );
                }
            }
        }

        file.close();
    }
}


// Write the output SFV file
inline void write_sfv( 
    const fs::path & path_sfv )
{
    if (m_check_sfv)
    {
        // Write BadFiles.log output if needed
        if (!m_bad_files.empty())
        {
            auto const bad_files_path = path_sfv.parent_path() / "LazyCRC_BadFiles.log";

            std::wofstream out_bad( bad_files_path );
            out_bad << u16_to_wstring( m_bad_files );
            out_bad.close();

            msg_write( MSG_ERROR_SFV_CHECK_FAILED, bad_files_path.c_str() );
        }
        else
            msg_write( MSG_INFO_SFV_CHECK_SUCCESS );
    }
    else
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

                for (auto const& [path, hash] : m_files)
                    data << path.c_str() << ' ' << hash << std::endl;

                auto result = detail::utf16_to_utf8( data.str() );
                file << result.str();
                file.close();

                msg_write( MSG_INFO_SFV_CREATED, path_sfv.c_str() );
            }
        }
    }
}


int wmain( int argc, wchar_t **argv )
{
    #pragma warning( push )
    #pragma warning( disable : 6031)
    _setmode( _fileno( stdout ), _O_U16TEXT );
    #pragma warning( pop ) 

    msg_write( MSG_INFO_VERSION, L"1.3.0" );

    if (argc < 0x2)
    {
        msg_write( MSG_INFO_USAGE );
        static_cast<void>(std::getchar());

        return -1;
    }

    if (argc >= 0x3)
    {
        if (std::wcscmp( argv[2], L"--check" ) == 0x0)
            m_check_sfv = true;
    }

    // Full path to the operated file or directory
    auto path_file = fs::path( fs::path( argv[0x1] ).u16string() );

    // Full path to the output SFV file
    fs::path path_sfv{ path_file.parent_path() / path_file.filename() += ".sfv" };

    if (!fs::exists( path_file ))
    {
        msg_write( MSG_ERROR_NOT_EXIST, path_file.c_str() );
        static_cast<void>(std::getchar());

        return -1;
    }

    ch::steady_clock::time_point time_start, time_end;

    if (fs::is_directory( path_file ) && !fs::is_empty( path_file ))
    {
        path_sfv = path_file / path_file.filename() += ".sfv";
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
        msg_write( MSG_ERROR_UNKNOWN_FILE );
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