# LOH

Lookback and Huffman compression codec as a single-header library.

Public domain, extremely small* lookback (LZSS) and Huffman compression, with optional delta coding.

LOH's compressor is fast, and its decompressor is slightly slower than `unzip` and `lz4` (the commands). Its compression ratio is mediocre, except on uncompressed audio and images, where it outperforms codecs that don't support delta coding, and files that are overwhelmingly dominated by a single byte value, where it outperforns most codecs, including `zip` and `lz4` (the commands).

LOH is meant to be embedded into other applications, not used as a general purpose compression tool. The encoder and decoder implemented here are not streaming, but streaming encoders and decoders are possible and shouldn't be too hard to write.

LOH is good for applications that have to compress lots of data quickly, especially images, and also for applications that need a single-header compression library.

LOH can be modified to compress more aggressively at the cost of performance, but even then, it usually doesn't perform as well on non-image/audio files as `zip` does. To make it more aggressive, change LOH_HASH_SIZE to 16 and LOH_HASHTABLE_KEY_SHL to a number greater than 2 (usually 3, 4, or 5; increasing it has an exponential cost, both in time and memory usage).

This project compiles cleanly both as C and C++ code without warnings or errors. Requires C99 or C++11 or newer.

Not fuzzed. However, the compressor is probably perfectly safe, and the decompressor is probably safe on trusted/correct data.

\* Around 850 lines of actual code according to `cloc`. The file itself is over 1k lines because it's well-commented.

## Comparison

Made with the gzip, lz4, brotli, and zstd commands, and loh.c compiled as -O3 (without -march=native). Dashes refer to compression level. loh doesn't have a 'standard' compression level setting, only technique flags, so the technique flags that differ from the defaults are noted instead (l means lookback, h means Huffman, d means delta). The best loh flags are used for the given file. If the default flags are the best, alternative flags are not attempted.

Times are the average of five runs or however many runs it took to break 10 total seconds, whichever was fewer.

Name | Size | Compress time | Decompress time
-|-|-|-
data/cc0_photo.tga | 3728 KB | - | -
data/cc0_photo.tga.loh | 2774 KB | 0.114s | 0.047s
data/cc0_photo.tga.l0d3.loh | 2223 KB | *0.044s* | 0.052s
data/cc0_photo.tga.-5.gz | 2473 KB | 0.166s | 0.033s
data/cc0_photo.tga.-9.gz | 2466 KB | 0.264s | 0.029s
data/cc0_photo.tga.-5.lz4 | 2777 KB | 0.113s | 0.013s
data/cc0_photo.tga.-9.lz4 | 2753 KB | 0.167s | *0.012s*
data/cc0_photo.tga.-19.zst | 2328 KB | 0.985s | 0.018s
data/cc0_photo.tga.-11.br | *2069 KB* | 12.02s | 0.039s
-|-|-|-
data/blake recorded 11.wav | 27002 KB | - | -
data/blake recorded 11.wav.loh | 26134 KB | *0.608s* | 0.286s
data/blake recorded 11.wav.d4.loh | *24464 KB* | 0.626s | 0.274s
data/blake recorded 11.wav.-5.gz | 25920 KB | 0.856s | 0.187s
data/blake recorded 11.wav.-9.gz | 25910 KB | 1.124s | 0.186s
data/blake recorded 11.wav.-5.lz4 | 26404 KB | 0.683s | 0.075s
data/blake recorded 11.wav.-9.lz4 | 26375 KB | 0.783s | 0.069s
data/blake recorded 11.wav.-19.zst | 26130 KB | 8.528s | 0.036s
data/blake recorded 11.wav.-11.br | 25268 KB | 90.319s | 0.308s
-|-|-|-
data/moby dick.txt | 1246 KB | - | -
data/moby dick.txt.loh | 633 KB | *0.055s* | 0.015s
data/moby dick.txt.-5.gz | 508 KB | 0.066s | 0.01s
data/moby dick.txt.-9.gz | 499 KB | 0.126s | 0.016s
data/moby dick.txt.-5.lz4 | 591 KB | 0.057s | 0.006s
data/moby dick.txt.-9.lz4 | 583 KB | 0.08s | *0.005s*
data/moby dick.txt.-19.zst | 412 KB | 0.658s | 0.006s
data/moby dick.txt.-11.br | *403 KB* | 2.286s | 0.008s
-|-|-|-
data/oops all zeroes.bin | 27002 KB | - | -
data/oops all zeroes.bin.loh | 462 Bytes | 0.128s | 0.095s
data/oops all zeroes.bin.-5.gz | 26 KB | 0.133s | 0.13s
data/oops all zeroes.bin.-9.gz | 26 KB | 0.134s | 0.135s
data/oops all zeroes.bin.-5.lz4 | 106 KB | 0.034s | 0.042s
data/oops all zeroes.bin.-9.lz4 | 106 KB | *0.027s* | 0.033s
data/oops all zeroes.bin.-19.zst | 866 Bytes | 0.044s | *0.01s*
data/oops all zeroes.bin.-11.br | *27 Bytes* | 0.564s | 0.096s
-|-|-|-
data/white noise.bin | *27002 KB* | - | -
data/white noise.bin.loh | 27006 KB | *0.526s* | 0.177s
data/white noise.bin.-5.gz | 27006 KB | 0.785s | 0.168s
data/white noise.bin.-9.gz | 27006 KB | 0.789s | 0.156s
data/white noise.bin.-5.lz4 | 27002 KB | 0.678s | 0.071s
data/white noise.bin.-9.lz4 | 27002 KB | 0.679s | 0.066s
data/white noise.bin.-19.zst | 27003 KB | 8.508s | *0.033s*
data/white noise.bin.-11.br | 27002 KB | 45.611s | 0.066s
-|-|-|-
data/Godot_v4.1.3-stable_win64.exe | 117559 KB | - | -
data/Godot_v4.1.3-stable_win64.exe.loh | 63348 KB | *2.809s* | 1.23s
data/Godot_v4.1.3-stable_win64.exe.-5.gz | 54154 KB | 3.786s | 0.757s
data/Godot_v4.1.3-stable_win64.exe.-9.gz | 53650 KB | 10.596s | 0.743s
data/Godot_v4.1.3-stable_win64.exe.-5.lz4 | 60631 KB | 2.201s | 0.355s
data/Godot_v4.1.3-stable_win64.exe.-9.lz4 | 60210 KB | 3.792s | *0.346s*
data/Godot_v4.1.3-stable_win64.exe.-19.zst | 45147 KB | 63.875s | 0.293s
data/Godot_v4.1.3-stable_win64.exe.-11.br | *42549 KB* | 375.979s | 0.736s
-|-|-|-
data/unifont-jp.tga | 65537 KB | - | -
data/unifont-jp.tga.loh | 2972 KB | *0.361s* | 0.211s
data/unifont-jp.tga.-5.gz | 2223 KB | 0.549s | 0.226s
data/unifont-jp.tga.-9.gz | 1492 KB | 16.121s | 0.194s
data/unifont-jp.tga.-5.lz4 | 4679 KB | 0.651s | 0.131s
data/unifont-jp.tga.-9.lz4 | 2715 KB | 4.765s | 0.134s
data/unifont-jp.tga.-19.zst | 1258 KB | 21.154s | *0.079s*
data/unifont-jp.tga.-11.br | *1055 KB* | 153.957s | 0.131s

\* LZ4 has a maximum compression ratio of 1:256-ish (because of how it stores long integers)

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


