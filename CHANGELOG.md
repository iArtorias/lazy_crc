# Changelog

## [2.0.0](https://github.com/iArtorias/lazy_crc/releases/tag/v2.0.0)

v2.0.0 is a near total rewrite. The tool grew from a single-threaded CRC32 utility into a multi-algorithm, multi-threaded hashing pipeline with overlapped Win32 I/O, sector-aligned buffers, and an asynchronous print queue. Source is also restructured.

### Added

- **MD5 hashing** - pass `--md5` to generate a `.md5` file instead of `.sfv`. Output uses the standard 32-character hex format and is compatible with common MD5 verification tools.
- **SHA-256 hashing** - pass `--sha256` to generate a `.sha256` file. 64-character hex digest, computed through the OpenSSL EVP API.
- **SHA-512 hashing** - pass `--sha512` to generate a `.sha512` file. 128-character hex digest, computed through the OpenSSL EVP API.
- **BLAKE3 hashing** - pass `--blake3` to generate a `.b3sum` file. Uses the official BLAKE3 C implementation with full runtime SIMD dispatch: AVX-512 (degree 16), AVX2 (degree 8), SSE4.1 (degree 4), SSE2 (degree 2), portable (degree 1). Selected automatically based on the host CPU at startup.
- **Single-thread mode** - pass `--single` to disable the thread pool and process all files sequentially on the main thread. Useful for HDDs and network shares where parallel random I/O hurts more than it helps, or for reproducible profiling.
- **Algorithm auto-detection on `--check`** - when verifying an existing hash file, the algorithm is inferred from the hash width in the file (`8` hex chars = CRC32, `32` = MD5, `64` = SHA-256 or BLAKE3, `128` = SHA-512). No need to pass `--md5`, `--sha256`, `--sha512`, or `--blake3` alongside `--check`.
- **Explicit file/folder list via `--files`** - pass any mix of files and folders after `--files` to hash a curated set instead of an entire tree. Folders are walked recursively with their own name preserved as the first component of every relative path, so the checksum file can sit next to the folder and `--check` resolves cleanly. The list runs until the next known flag or the end of the command line.
- **`--output <path>`** - explicit destination for the checksum file. If `<path>` is a directory, the default filename is placed inside it, otherwise `<path>` is used verbatim. When `--output` is given, the timestamp is omitted from the filename.
- **Timestamped default output names** - when `--output` is not specified, the default filename follows `{name}-{YYYYMMDD-HHMMSS}.{ext}` so repeated runs no longer overwrite each other. `{name}` is the input file's name, the directory's name, the first `--files` folder's name, or the parent directory name of the first `--files` file, depending on the input.
- **Thread pool** - fixed-size pool. Workers pull file indices one at a time from a shared counter, so a single large file at the end of the list does not starve the remaining workers. Results are accumulated in per-thread slots and merged at the end (no contention on a shared result container).
- **Async print queue** - dedicated printer thread drains a shared `std::vector<std::wstring>` while workers push messages into a `thread_local` batch that is flushed every 64 entries. Workers never block on stdout, and `_putws` is called only from the printer thread.
- **Double-buffered overlapped I/O** - files at or above 64 MiB are read through two alternating overlapped buffers: while one read is in flight the previous buffer is being hashed, hiding storage latency behind compute.
- **Sector-aligned buffers** - read buffers are allocated with `VirtualAlloc` (4 KiB page aligned) and block sizes are rounded up to 4 KiB sectors.

### Changed

- **Multi-threaded directory processing** - v1.4.0 processed files serially on the main thread. v2.0.0 distributes work across `N-1` worker threads (where `N` is the logical core count), leaving one core free for the main thread and the printer thread.
- **Win32 `CreateFileW` instead of `fopen`** - file opens go through `CreateFileW` with explicit flag control, enabling `FILE_FLAG_SEQUENTIAL_SCAN`, `FILE_FLAG_NO_BUFFERING`, and `FILE_FLAG_OVERLAPPED` paths. The CRT `_wfopen_s` path is retained only for reading SFV files.
- **Block size policies split by algorithm** - `ChooseBlockSizeCRC32` keeps the original conservative tiers (max 4 MiB) suited to the compute-bound CRC32 path. `ChooseBlockSizeHash` uses larger tiers (up to 16 MiB) for the I/O-bound MD5 / SHA-256 / SHA-512 paths. BLAKE3 uses a fixed 4 MiB buffer that aligns with BLAKE3's internal chunk boundaries.
- **Per-call allocation removed from the hash loop** - v1.4.0 allocated a `std::unique_ptr<char[]>` inside the CRC loop on every chunk iteration. v2.0.0 allocates one `AlignedBuffer` (or a pair, for overlapped) per file and reuses it across all chunks.
- **Relative path computation** - switched from `fs::relative()` (which performs filesystem calls) to `path.lexically_relative()` (pure string operation).
- **`"."` as input now names the output file correctly** - `fs::absolute()` is applied to `argv[1]` before any path operations, so passing `.` produces `dirname.sfv` (or `.md5`, `.sha256`, etc.) instead of `.sfv` in the current directory.
- **Console output via `_putws`** - replaced `std::wcout` with `_putws` (direct CRT write), routed through the async print queue. With 16k+ files the per-call `std::wcout` overhead was a measurable serial bottleneck.
- **`--check` parser rewritten** - regex-based line parsing replaced by an explicit, simplified and more efficient parser. Accepts `8`, `32`, `64`, or `128` hex characters to match all five supported algorithms. Verification itself now runs through the thread pool.

### Changed (CRC32 library)

The bundled `Crc32` library (stbrumme/crc32) was modernized for C++20 and MSVC.

### Removed

- **`{fmt}` dependency** - all formatting now uses `std::format` (C++20 standard library).
- **`date` dependency** - elapsed time no longer requires Howard Hinnant's date library.
- **`<sstream>` / `std::wstringstream`** - hex conversion now uses `std::format` with `{:0>8X}` and `{:02X}`.
- **`<fstream>` / `std::wofstream` for SFV output** - replaced with binary-mode `FILE*` writes for correct Unicode handling.
- **`<fstream>` / `std::basic_ifstream<char16_t>` for SFV input** - replaced with a UTF-8 line reader and `MultiByteToWideChar` conversion.
- **`std::map<fs::path, std::wstring>` for results** - replaced with `std::unordered_map` plus a sort step at SFV write time.
- **`u16string` for the bad-files log** - replaced with `std::wstring`.
- **`fs::file_size()` redundant stat in directory mode** - replaced with `entry.file_size()` from the iterator entry, which uses the stat already performed during enumeration.
- **`fmt::v7::detail::utf16_to_utf8` (internal `{fmt}` detail)** - replaced with `WideCharToMultiByte` and a streaming `AppendWideAsUtf8` helper that writes directly into the output buffer with no intermediate allocation.
- **`std::wcout`** - replaced with `_putws` everywhere, routed through the async print queue in multi-threaded paths.

## [1.4.0](https://github.com/iArtorias/lazy_crc/releases/tag/v1.4.0)

- Drastic performance boost.
- Added more safety checks.
- Added milliseconds to the benchmark output time.

## [1.3.1](https://github.com/iArtorias/lazy_crc/releases/tag/v1.3.1)

- A few bugfixes.

## [1.3.0](https://github.com/iArtorias/lazy_crc/releases/tag/v1.3.0)

- A version with the SFV file check support.

## [1.2.0](https://github.com/iArtorias/lazy_crc/releases/tag/v1.2.0)

- A general performance boost.

## [1.1.0](https://github.com/iArtorias/lazy_crc/releases/tag/v1.1.0)

- A version with a fixed critical bug related to the size of the physically installed RAM.

## [1.0.0](https://github.com/iArtorias/lazy_crc/releases/tag/v1.0.0)

- First release