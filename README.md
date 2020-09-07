# **LazyCRC**

### A fast, yet tiny tool to calculate the CRC32 and create the .SFV file

## Usage

```
lazy_crc <file|directory>
```

## Benchmark

- **985 MB** file (**1,032,863,429** bytes) : **Elapsed time: 0h 0m 1s** *
- **4.03 GB** file (**4,328,447,109** bytes) : **Elapsed time: 0h 0m 6s** *
- **7.95 GB** file (**8,537,629,034** bytes) : **Elapsed time: 0h 0m 13s** *
- **14.3 GB** file (**15,355,753,472** bytes) : **Elapsed time: 0h 0m 23s** *
- **35 GB** file (**37,580,963,840** bytes) : **Elapsed time: 0h 0m 56s** *

_* All the tests were performed on **Samsung 256GB PM981 NVMe PCIe M.2** (**MZVLB256HAHQ-00000**)_

## Notes
- **UTF-8** / **UTF-16** file names are supported
- You can also **drag** either the file or directory to the **LazyCRC** executable file

## Stuff used

- **date** (https://github.com/HowardHinnant/date)
- **{fmt}** (https://github.com/fmtlib/fmt)
- **Fast CRC32** (https://github.com/stbrumme/crc32)
- **Crc32** (_16*2 algorithm_) (https://github.com/Bulat-Ziganshin/Crc32)

## Compilation notes

- **Visual Studio 2019** is recommended to compile this project
