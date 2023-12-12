# LOH

Lookback and Huffman compression codec as a single-header library.

Public domain, extremely small* lookback (LZSS) and Huffman compression, with optional delta coding.

LOH's compressor is fast, and its decompressor is slightly slower than `unzip` and `lz4` (the commands). Its compression ratio is mediocre, except on uncompressed audio and images, where it outperforms codecs that don't support delta coding, and files that are overwhelmingly dominated by a single byte value, where it outperforns most codecs, including `zip` and `lz4` (the commands).

LOH is meant to be embedded into other applications, not used as a general purpose compression tool. The encoder and decoder implemented here are not streaming, but streaming encoders and decoders are possible and shouldn't be too hard to write.

LOH is good for applications that have to compress lots of data quickly, especially images, and also for applications that need a single-header compression library.

This project compiles cleanly both as C and C++ code without warnings or errors. Requires C99 or C++11 or newer.

Not fuzzed. However, the compressor is probably perfectly safe, and the decompressor is probably safe on trusted/correct data.

\* Around 850 lines of actual code according to `cloc`. The file itself is over 1k lines because it's well-commented.

## Comparison

Made with the gzip, lz4, brotli, and zstd commands, and loh.c compiled as -O3 (without -march=native). Ran on a Steam Deck in desktop mode. Dashes refer to compression level. LOH has technique flags, so the technique flags that differ from the defaults are noted instead (l means lookback, h means Huffman, d means delta). The best flags are used for the given file. If the default flags are the best, alternative flags are not attempted. For LOH, both the default (4) and maximum (9) quality level are attempted; it's indicated as `l9` (that's an L, not a 1) for maximum or unmarked for default.

Times are the average of five runs or however many runs it took to break 10 total seconds, whichever was fewer.

Name | Size | Compress time | Decompress time
-|-|-|-
data/cc0_photo.tga | 3728 KB | - | -
data/cc0_photo.tga.loh | 2774 KB | 0.139s | 0.051s
data/cc0_photo.tga.l9.loh | 2743 KB | 0.782s | 0.049s
data/cc0_photo.tga.l0d3.loh | 2223 KB | **0.043s** | 0.044s
data/cc0_photo.tga.-5.gz | 2473 KB | 0.17s | 0.034s
data/cc0_photo.tga.-9.gz | 2466 KB | 0.257s | 0.036s
data/cc0_photo.tga.-5.lz4 | 2777 KB | 0.121s | **0.011s**
data/cc0_photo.tga.-9.lz4 | 2753 KB | 0.176s | 0.012s
data/cc0_photo.tga.-19.zst | 2328 KB | 1.026s | 0.016s
data/cc0_photo.tga.-11.br | **2069 KB** | 12.211s | 0.043s
-|-|-|-
data/blake recorded 11.wav | 27002 KB | - | -
data/blake recorded 11.wav.loh | 26134 KB | 0.775s | 0.287s
data/blake recorded 11.wav.l9.loh | 26096 KB | 11.683s | 0.327s
data/blake recorded 11.wav.d4.loh | 24495 KB | **0.467s** | 0.288s
data/blake recorded 11.wav.l9d4.loh | **24443 KB** | 11.756s | 0.285s
data/blake recorded 11.wav.-5.gz | 25920 KB | 0.865s | 0.188s
data/blake recorded 11.wav.-9.gz | 25910 KB | 1.129s | 0.189s
data/blake recorded 11.wav.-5.lz4 | 26404 KB | 0.725s | 0.07s
data/blake recorded 11.wav.-9.lz4 | 26375 KB | 0.814s | 0.072s
data/blake recorded 11.wav.-19.zst | 26130 KB | 8.776s | **0.036s**
data/blake recorded 11.wav.-11.br | 25268 KB | 92.085s | 0.303s
-|-|-|-
data/moby dick.txt | 1246 KB | - | -
data/moby dick.txt.loh | 633 KB | **0.049s** | 0.017s
data/moby dick.txt.l9.loh | 586 KB | 0.108s | 0.017s
data/moby dick.txt.-5.gz | 508 KB | 0.063s | 0.013s
data/moby dick.txt.-9.gz | 499 KB | 0.128s | 0.011s
data/moby dick.txt.-5.lz4 | 591 KB | 0.051s | **0.005s**
data/moby dick.txt.-9.lz4 | 583 KB | 0.083s | 0.007s
data/moby dick.txt.-19.zst | 412 KB | 0.695s | 0.007s
data/moby dick.txt.-11.br | **403 KB** | 2.398s | 0.009s
-|-|-|-
data/oops all zeroes.bin | 27002 KB | - | -
data/oops all zeroes.bin.loh | 462 Bytes | 0.111s | 0.105s
data/oops all zeroes.bin.l9.loh | 462 Bytes | 0.118s | 0.105s
data/oops all zeroes.bin.-5.gz | 26 KB | 0.13s | 0.129s
data/oops all zeroes.bin.-9.gz | 26 KB | 0.133s | 0.137s
data/oops all zeroes.bin.-5.lz4 | 106 KB | 0.027s | 0.031s
data/oops all zeroes.bin.-9.lz4 | 106 KB | 0.029s | **0.029s**
data/oops all zeroes.bin.-19.zst | 866 Bytes | **0.041s** | 0.01s
data/oops all zeroes.bin.-11.br | **27 Bytes** | 0.563s | 0.094s
-|-|-|-
data/white noise.bin | **27002 KB** | - | -
data/white noise.bin.loh | 27006 KB | 0.717s | 0.189s
data/white noise.bin.l9.loh | 27006 KB | 12.021s | 0.202s
data/white noise.bin.-5.gz | 27006 KB | 0.802s | 0.149s
data/white noise.bin.-9.gz | 27006 KB | 0.798s | 0.152s
data/white noise.bin.-5.lz4 | 27002 KB | **0.706s** | 0.063s
data/white noise.bin.-9.lz4 | 27002 KB | 0.714s | 0.069s
data/white noise.bin.-19.zst | 27003 KB | 8.682s | **0.033s**
data/white noise.bin.-11.br | 27002 KB | 46.547s | 0.064s
-|-|-|-
data/Godot_v4.1.3-stable_win64.exe | 117559 KB | - | -
data/Godot_v4.1.3-stable_win64.exe.loh | 63348 KB | 3.374s | 1.387s
data/Godot_v4.1.3-stable_win64.exe.l9.loh | 60964 KB | 20.82s | 1.328s
data/Godot_v4.1.3-stable_win64.exe.-5.gz | 54154 KB | 3.781s | 0.761s
data/Godot_v4.1.3-stable_win64.exe.-9.gz | 53650 KB | 10.63s | 0.748s
data/Godot_v4.1.3-stable_win64.exe.-5.lz4 | 60631 KB | **2.347s** | 0.412s
data/Godot_v4.1.3-stable_win64.exe.-9.lz4 | 60210 KB | 4.147s | 0.395s
data/Godot_v4.1.3-stable_win64.exe.-19.zst | 45147 KB | 66.246s | **0.295s**
data/Godot_v4.1.3-stable_win64.exe.-11.br | **42549 KB** | 383.963s | 0.883s
-|-|-|-
data/unifont-jp.tga | 65537 KB | - | -
data/unifont-jp.tga.loh | 2972 KB | **0.402s** | 0.235s
data/unifont-jp.tga.l9.loh | 2168 KB | 0.524s | 0.217s
data/unifont-jp.tga.-5.gz | 2223 KB | 0.548s | 0.23s
data/unifont-jp.tga.-9.gz | 1492 KB | 16.253s | 0.197s
data/unifont-jp.tga.-5.lz4 | 4679 KB | 0.649s | 0.14s
data/unifont-jp.tga.-9.lz4 | 2715 KB | 4.858s | 0.156s
data/unifont-jp.tga.-19.zst | 1258 KB | 21.742s | **0.073s**
data/unifont-jp.tga.-11.br | **1055 KB** | 156.151s | 0.135s

(LZ4 has a maximum compression ratio of 1:256-ish, because of how it stores long integers.)

## Format

LOH has three compression steps:

1) An optional delta step that differentiates the file with an arbitrary comparison distance from 1 to 255 (inclusive). Unsigned 8-bit subtraction, overflow wraps around. This does not change the size of the file, but it can make the following steps more efficient for some types of file.
2) An optional LZSS-style lookback stage, where a given run of output bytes can either be encoded as a literal, or a lookback reference defined by distance and length (and the distance is allowed to be less than the length).
3) An optional Huffman coding stage, using a length-limited canonical Huffman code.

These steps are performed in order on the entire output of the previous stage. So, while step 2 has an output length prefix, that length prefix is compressed by step 3, and then step 3 has its own output length prefix as well.

All three steps are optional, and whether they're done is stored in the header. This means you can use LOH as a preprocessor or postprocessor for other formats, e.g. applying delta coding to an image before `zip`ing it, or applying Huffman coding to an `lz4` file.

### Lookback

The LZSS-style layer works strictly with bytes, not with a bitstream. When the decoder's control loop reads a byte, it looks at the least-significant bit (i.e. it calculates `byte & 1`). If the bit is set, then the current byte is the start of a lookback command. If it's unset, then the current byte is the start of a literal sequence, i.e. uncompressed data. Literal control byte sequences store just the number of bytes that follow. Lookback sequences encode first the lookback distance and then the length (number of bytes to copy). A description of every possible control byte sequence follows:

```
Literals:

xxxx xx00 ...
literals, ... contains <xxxx xx> literals

xxxx xx10 u8 ...
long literals, ... contains <u8 xxxx xx> literals

Lookback command distance parts:

xxxx xx01
Lookback has a distance <xxxx xx>.

xxxx x011 +u8
13-bit lookback
Lookback has a distance <u8 xxxx x>.

xxxx 0111 +u16
20-bit lookback
Lookback has a distance <u16 xxxx>. The u16 is least-significant-chunk-first.

xxx0 1111 +u24
27-bit lookback
Etc.

xx01 1111 +u32
34-bit lookback

The distance part is followed by a lookback length sequence.

Lookback lengths:

xxxx xxx0
Lookback has a length of <xxxx xxx>.

xxxx xxx1 u8
Lookback has a length of <u8 xxxx xxx>.
```

Numbers stored in multiple bytes are stored with the less-significant parts of that number in earlier bytes. For example, if you decode 4 bits from byte 0, 7 bits from byte 1, and 2 bytes from byte 2, and combine them into a single value, the layout of bits from most significant to least significant should have the following byte origins: `2 2111 1111 0000`

### Huffman coding

LOH uses canonical Huffman codes to allow for faster decoding.

Also, this stage uses individual bit access, unlike the lookback stage. The encoder writes bits starting with the first bit in the least-significant bit of the first byte (the 1 bit), going up to the most-significant bit (the 128 bit), then going on to the 1 bit of the second byte, and so on.

The Huffman code table is stored at the start of the output of this stage, in order from shortest to longest code, in order of code value, starting with a code length of 1. The encoder outputs a `1` bit to indicate that it's moved on to the next code length, or a `0` to indicate that there's another symbol associated with this code length, followed by whatever that symbol is. Symbols are encoded least-significant-bit-first. So, if the shortest code is 3 bits long, and maps to the character 'a' (byte 0x61), then the encoder ouputs `1`, `1` (advance from length 1 to 2, then from 2 to 3), `0`, (symbol at current code length follows), `1`, `0`, `0`, `0`, `0`, `1`, `1`, `0` (bits of 0x61 in order). In a hex editor, this would look like `0B 03`. This is space-inefficient, but allows for near-ideally-fast construction of all the relevant tables while decoding the huffman code table..

Before the code table, the number of symbols coded in it minus one is written as an 8-bit integer. So if there are 8 symbols `0x07` is written. This is how the decoder knows when to stop decoding the code table and start decoding the compressed data.

After the code table, the encoder rounds its bit cursor up to the start of the next byte, allowing the decoder to work on a byte-by-byte basis when decompressing the compressed data rather instead of slow bit-banging.

Huffman codes in the compressed data are stored starting from the most significant bit of each code word (i.e. the root of the Huffman tree) and working towards the least significant bit. These bits are written into the output buffer starting at the least-significant bit of a given byte and working towards the most-significant bit of that byte, before moving on to the next byte, where bits also start out being stored in the least-significant bit.


