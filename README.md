# **LazyCRC**

[![GitHub All Releases](https://img.shields.io/github/downloads/iArtorias/lazy_crc/total.svg)](https://github.com/iArtorias/lazy_crc/releases)

### A fast, yet tiny tool to calculate the CRC32 and create the .SFV file

## Usage

```
lazy_crc <file|directory>
```

*or*

```
lazy_crc <path_to_sfv_file> --check
```

## Benchmark

| File size (bytes)  | Result time |
| ------------- | ------------- |
| 270,708,736  | 0h 0m 0s 147ms  |
| 1,081,618,432  | 0h 0m 0s 528ms  |
| 1,836,064,768  | 0h 0m 0s 815ms  |
| 4,388,488,186  | 0h 0m 1s 914ms  |
| 32,237,408,256  | 0h 0m 13s 879ms  |

_* All the tests were performed on **Samsung 980 Pro NVMe PCle 4.0 (2 Tb)**_

## Notes
- **UTF-8** / **UTF-16** file names are supported
- You can also **drag** either the file or directory to the **LazyCRC** executable file

## Stuff used

- **date** (https://github.com/HowardHinnant/date)
- **{fmt}** (https://github.com/fmtlib/fmt)
- **Fast CRC32** (https://github.com/stbrumme/crc32)
- **Crc32** (_16*2 algorithm_) (https://github.com/Bulat-Ziganshin/Crc32)

## Compilation notes

- **Visual Studio 2022	** is recommended to compile this project
