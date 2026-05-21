# **LazyCRC**

[![GitHub All Releases](https://img.shields.io/github/downloads/iArtorias/lazy_crc/total.svg)](https://github.com/iArtorias/lazy_crc/releases)

### A fast, multi-threaded tool for hashing files and creating checksum files

LazyCRC computes CRC32, MD5, SHA-256, SHA-512 or BLAKE3 over a single file or an entire directory tree, writing a checksum file that can later be verified with `--check`. It is built around a thread pool, double-buffered overlapped Win32 I/O, and sector-aligned read buffers, and targets workloads ranging from a single large archive to directory trees containing tens of thousands of files.

## Usage

**Generate a checksum file:**
```
lazy_crc <file|directory> [--md5|--sha256|--sha512|--blake3] [--single] [--output <path>]
```

**Generate a checksum file from a list of files and/or folders:**
```
lazy_crc --files <path1> <path2> ... [--md5|--sha256|--sha512|--blake3] [--single] [--output <path>]
```

**Verify an existing checksum file:**
```
lazy_crc <path_to_checksum_file> --check
```

## Flags

| Flag | Description |
| --- | --- |
| *(none)* | Use CRC32 hashing, output a `.sfv` file |
| `--md5` | Use MD5 hashing, output a `.md5` file |
| `--sha256` | Use SHA-256 hashing, output a `.sha256` file |
| `--sha512` | Use SHA-512 hashing, output a `.sha512` file |
| `--blake3` | Use BLAKE3 hashing, output a `.b3sum` file |
| `--check` | Verify an existing `.sfv`, `.md5`, `.sha256`, `.sha512`, or `.b3sum` file |
| `--single` | Disable multi-threading (useful for HDDs and network shares) |
| `--files <paths...>` | Hash an explicit list of files and/or folders. Folders are walked recursively. The list runs until the next known flag or the end of the command line |
| `--output <path>` | Write the checksum file to the given path. If `<path>` is a directory the default filename is placed inside it, otherwise `<path>` is used verbatim |

Flags can appear in any order after the path. The algorithm is detected automatically when using `--check` based on the hash width in the file (`8` hex chars = CRC32, `32` = MD5, `64` = SHA-256 or BLAKE3, `128` = SHA-512), with the file extension used to distinguish SHA-256 from BLAKE3.

## Output file naming

Without `--output`, the default checksum file name follows `{name}-{YYYYMMDD-HHMMSS}.{ext}` where `{name}` depends on the input:

| Input | `{name}` | Location |
| --- | --- | --- |
| Single file | the file's own name | next to the input file |
| Directory | the directory name | next to the directory |
| `--files <folder> ...` | the folder name (first argument) | next to that folder |
| `--files <file1> <file2> ...` | parent directory name of the first file | next to those files |

When `--output` is given, the timestamp is omitted. Pointing at a directory drops a default named file there, and pointing at a file path uses it verbatim.

## Benchmarks

### Single file

Wall-clock time to hash one contiguous file of the given size.

| File size | CRC32 | MD5 | SHA-256 | SHA-512 | BLAKE3 |
| --- | --- | --- | --- | --- | --- |
| **5 GB** | 0h 0m 2s 295ms | 0h 0m 4s 869ms | 0h 0m 2s 253ms |  0h 0m 4s 513ms | 0h 0m 1s 73ms |
| **10 GB** | 0h 0m 4s 66ms |  0h 0m 9s 754ms | 0h 0m 4s 537ms | 0h 0m 9s 47ms | 0h 0m 1s 890ms |
| **25 GB** | 0h 0m 9s 301ms | 0h 0m 24s 373ms | 0h 0m 11s 335ms | 0h 0m 22s 629ms | 0h 0m 4s 608ms |
| **50 GB** | 0h 0m 17s 8ms | 0h 0m 48s 794ms | 0h 0m 22s 607ms | 0h 0m 44s 910ms | 0h 0m 9s 46ms |
| **100 GB** | 0h 0m 34s 552ms | 0h 1m 37s 317ms | 0h 0m 45s 64ms | 0h 1m 29s 687ms | 0h 0m 18s 16ms |

* _Samsung 980 Pro NVMe PCIe 4.0_

### Directory tree

Wall-clock time to hash a directory tree of **~155 GB** spread across **~16 000 files** of mixed sizes, processed through the thread pool (`N-1` workers where `N` is the logical core count).

| Workload | CRC32 | MD5 | SHA-256 | SHA-512 | BLAKE3 |
| --- | --- | --- | --- | --- | --- |
| **155 GB** / ~**16k** files | 0h 0m 33s 269ms | 0h 0m 51s 180ms | 0h 0m 29s 197ms | 0h 0m 49s 87ms | 0h 0m 28s 237ms |

* _Samsung 980 Pro NVMe PCIe 4.0_

## Notes

- **UTF-8 filenames are fully supported** - checksum files are written and read as UTF-8 regardless of the system locale, so filenames with CJK, Cyrillic, or other non-Latin characters are handled correctly
- **Algorithm auto-detection** - `--check` reads the hash width from the file and selects the correct algorithm automatically
- **Drag and drop** - drag a file or directory onto the executable to hash it with default settings (CRC32)
- **`--single` for HDD** - parallel random seeks on an HDD are usually slower than sequential access, `--single` processes files in order and avoids that overhead
- **Overlapped I/O for large files** - files at or above 64 MiB are read through a double-buffered async pipeline (`FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED`) so the next read is in flight while the previous chunk is being hashed

See [CHANGELOG.md](CHANGELOG.md) for the full list of changes.

## Dependencies

- **Fast CRC32** - [stbrumme/crc32](https://github.com/stbrumme/crc32) *(Crc32, 16x2 algorithm)*
- **OpenSSL** - [openssl/openssl](https://github.com/openssl/openssl) *(EVP API for MD5, SHA-256, SHA-512 hashing)*
- **BLAKE3** - [BLAKE3-team/BLAKE3](https://github.com/BLAKE3-team/BLAKE3) *(Official C implementation with runtime SIMD dispatch)*

## Compilation

**Visual Studio 2022** is recommended.