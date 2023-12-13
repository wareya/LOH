# LOH

Lookback and Huffman compression codec as a single-header library.

Public domain, extremely small* lookback (LZSS) and Huffman compression, with optional delta coding.

LOH's compressor is fast, and its decompressor is slightly slower than `unzip` and `lz4` (the commands). Its compression ratio is mediocre, except on uncompressed audio and images, where it outperforms codecs that don't support delta coding, and files that are overwhelmingly dominated by a single byte value, where it outperforns most codecs, including `zip` and `lz4` (the commands).

LOH is meant to be embedded into other applications, not used as a general purpose compression tool. The encoder and decoder implemented here are not streaming, but streaming encoders and decoders are possible and shouldn't be too hard to write.

LOH is good for applications that have to compress lots of data quickly, especially images, and also for applications that need a single-header compression library.

This project compiles cleanly both as C and C++ code without warnings or errors. Requires C99 or C++11 or newer.

Not fuzzed. However, the compressor is probably perfectly safe, and the decompressor is probably safe on trusted/correct data.

\* Around 880 lines of actual code according to `cloc`. The file itself is over 1k lines because it's well-commented.

## Comparison

Made with the gzip, lz4, brotli, and zstd commands, and loh.c compiled as -O3 (without -march=native). Ran on a Steam Deck in desktop mode. Dashes refer to compression level. LOH has technique flags, so the technique flags that differ from the defaults are noted instead (- means lookback, h means Huffman, d means delta). The best flags are used for the given file. If the default flags are the best, alternative flags are not attempted. For LOH, both the default (4) and maximum (9) quality level are attempted.

Times are the average of five runs or however many runs it took to break 10 total seconds, whichever was fewer.

Name  |  Size  |  Compress time  |  Decompress time
-|-|-|-
data/cc0_photo.tga | 3728 KB | - | -
data/cc0_photo.tga.loh | 2597 KB | 0.125s | 0.048s
data/cc0_photo.tga.-9.loh | 2575 KB | 0.697s | 0.05s
data/cc0_photo.tga.-0d3.loh | 2051 KB | **0.054s** | 0.048s
data/cc0_photo.tga.-5.gz | 2473 KB | 0.171s | 0.03s
data/cc0_photo.tga.-9.gz | 2466 KB | 0.262s | 0.033s
data/cc0_photo.tga.-5.lz4 | 2777 KB | 0.116s | 0.013s
data/cc0_photo.tga.-9.lz4 | 2753 KB | 0.178s | **0.012s**
data/cc0_photo.tga.-19.zst | 2328 KB | 1.031s | 0.013s
data/cc0_photo.tga.-11.br | **2069 KB** | 12.242s | 0.039s
-|-|-|-
data/blake recorded 11.wav | 27002 KB | - | -
data/blake recorded 11.wav.loh | 25995 KB | 0.788s | 0.3s
data/blake recorded 11.wav.-9.loh | 25980 KB | 10.259s | 0.316s
data/blake recorded 11.wav.d4.loh | 24327 KB | **0.515s** | 0.277s
data/blake recorded 11.wav.d4.loh | **24306 KB** | 10.289s | 0.286s
data/blake recorded 11.wav.-5.gz | 25920 KB | 0.859s | 0.192s
data/blake recorded 11.wav.-9.gz | 25910 KB | 1.132s | 0.189s
data/blake recorded 11.wav.-5.lz4 | 26404 KB | 0.719s | 0.073s
data/blake recorded 11.wav.-9.lz4 | 26375 KB | 0.809s | 0.065s
data/blake recorded 11.wav.-19.zst | 26130 KB | 8.756s | **0.034s**
data/blake recorded 11.wav.-11.br | 25268 KB | 91.809s | 0.302s
-|-|-|-
data/moby dick.txt | 1246 KB | - | -
data/moby dick.txt.loh | 604 KB | 0.049s | 0.02s
data/moby dick.txt.-9.loh | 575 KB | 0.146s | 0.015s
data/moby dick.txt.-5.gz | 508 KB | 0.06s | 0.009s
data/moby dick.txt.-9.gz | 499 KB | 0.13s | 0.01s
data/moby dick.txt.-5.lz4 | 591 KB | **0.044s** | 0.005s
data/moby dick.txt.-9.lz4 | 583 KB | 0.079s | 0.006s
data/moby dick.txt.-19.zst | 412 KB | 0.689s | **0.005s**
data/moby dick.txt.-11.br | **403 KB** | 2.38s | 0.008s
-|-|-|-
data/oops all zeroes.bin | 27002 KB | - | -
data/oops all zeroes.bin.loh | 228 Bytes | 0.147s | 0.106s
data/oops all zeroes.bin.-9.loh | 228 Bytes | 0.137s | 0.11s
data/oops all zeroes.bin.-5.gz | 26 KB | 0.132s | 0.13s
data/oops all zeroes.bin.-9.gz | 26 KB | 0.13s | 0.129s
data/oops all zeroes.bin.-5.lz4 | 106 KB | 0.03s | 0.029s
data/oops all zeroes.bin.-9.lz4 | 106 KB | **0.024s** | 0.025s
data/oops all zeroes.bin.-19.zst | 866 Bytes | 0.04s | **0.012s**
data/oops all zeroes.bin.-11.br | **27 Bytes** | 0.567s | 0.1s
-|-|-|-
data/white noise.bin | **27002 KB** | - | -
data/white noise.bin.loh | 27062 KB | 0.712s | 0.193s
data/white noise.bin.-9.loh | 27062 KB | 10.544s | 0.195s
data/white noise.bin.-5.gz | 27006 KB | 0.795s | 0.158s
data/white noise.bin.-9.gz | 27006 KB | 0.794s | 0.151s
data/white noise.bin.-5.lz4 | 27002 KB | **0.704s** | 0.068s
data/white noise.bin.-9.lz4 | 27002 KB | 0.722s | 0.066s
data/white noise.bin.-19.zst | 27003 KB | 8.674s | **0.033s**
data/white noise.bin.-11.br | 27002 KB | 46.472s | 0.061s
-|-|-|-
data/Godot_v4.1.3-stable_win64.exe | 117559 KB | - | -
data/Godot_v4.1.3-stable_win64.exe.loh | 58797 KB | 2.923s | 1.142s
data/Godot_v4.1.3-stable_win64.exe.-9.loh | 56813 KB | 19.022s | 1.342s
data/Godot_v4.1.3-stable_win64.exe.-5.gz | 54154 KB | 3.856s | 0.759s
data/Godot_v4.1.3-stable_win64.exe.-9.gz | 53650 KB | 10.608s | 0.75s
data/Godot_v4.1.3-stable_win64.exe.-5.lz4 | 60631 KB | **2.317s** | 0.391s
data/Godot_v4.1.3-stable_win64.exe.-9.lz4 | 60210 KB | 3.903s | 0.4s
data/Godot_v4.1.3-stable_win64.exe.-19.zst | 45147 KB | 66.121s | **0.292s**
data/Godot_v4.1.3-stable_win64.exe.-11.br | **42549 KB** | 382.946s | 0.889s
-|-|-|-
data/unifont-jp.tga | 65537 KB | - | -
data/unifont-jp.tga.loh | 2963 KB | **0.473s** | 0.245s
data/unifont-jp.tga.-9.loh | 2088 KB | 0.593s | 0.209s
data/unifont-jp.tga.-5.gz | 2223 KB | 0.548s | 0.219s
data/unifont-jp.tga.-9.gz | 1492 KB | 16.262s | 0.192s
data/unifont-jp.tga.-5.lz4 | 4679 KB | 0.651s | 0.137s
data/unifont-jp.tga.-9.lz4 | 2715 KB | 4.813s | 0.132s
data/unifont-jp.tga.-19.zst | 1258 KB | 21.78s | **0.076s**
data/unifont-jp.tga.-11.br | **1055 KB** | 156.354s | 0.133s

(LZ4 has a maximum compression ratio of 1:256-ish, because of how it stores long integers.)

## Format

LOH has three compression steps:

1) An optional delta step that differentiates the file with an arbitrary comparison distance from 1 to 255 (inclusive). Unsigned 8-bit subtraction, overflow wraps around. This does not change the size of the file, but it can make the following steps more efficient for some types of file.
2) An optional LZSS-style lookback stage, where a given run of output bytes can either be encoded as a literal, or a lookback reference defined by distance and length (and the distance is allowed to be less than the length).
3) An optional Huffman coding stage, using a length-limited canonical Huffman code.

These steps are performed in order on the entire output of the previous stage. So, while step 2 has an output length prefix, that length prefix is compressed by step 3, and then step 3 has its own output length prefix as well.

All three steps are optional, and whether they're done is stored in the header. This means you can use LOH as a preprocessor or postprocessor for other formats, e.g. applying delta coding to an image before `zip`ing it, or applying Huffman coding to an `lz4` file.

Each step is applied to arbitrarily-sized chunks, which are listed by start location (both in the compressed and decompressed file) after the LOH file's header. For simplicity's sake, the encoder splits the file into 4 chunks, or chunks with 32k source file length, whichever results in bigger chunks.

### Lookback

The LZSS-style layer works strictly with bytes, not with a bitstream.

Each byte instruction sequence defines a distance term and a length term. If the distance term is zero, the length term is interpreted as a number of literal bytes to decode (plus one), which follow the byte instruction sequence. If the distance term is not zero, then the length and distance are interpreted as a lookback command (with size plus four).

The byte instruction sequences are made up of an initial byte with four bits for each value and a lower bit, three of which are part of the value itself and one of which indicates whether any extension bytes for that value follow. Any extension bytes for the size term follow first, followed by any extension bytes for the distance term. Extension bytes contain seven bits of value information in the top bits, with the zeroth bit (lowest, the '1' bit) indicating whether or not the extension byte continues. Bits from later extension bytes are added shifted left relative to the bits from previous bytes.

```
Initial byte:
dddX sssX

Extension byte (if not final):
vvvv vvv1

Extension byte (if final):
vvvv vvv0
```

These values are stored with respect to their minimum value being one more than the maximum value that could be stored by the previous length. So, a two-byte size starts at 8 (2^3), a three-byte size starts at 1032 (2^3 + 2^(7+3)), etc. This refers to the encoded value, not the actual value; the actual size value is stored minus 1 for literals and minus 4 for lookback matches.

This minimum-value-respecting encoding can be encoded like:

```c
size_t size_max = 0;
size_t size_max_next = loh_size_mask + 1;
size_t size_byte_count = 1;
size_t size_bit_count = loh_size_bits; // 3
while (write_size >= size_max_next)
{
    size_max = size_max_next;
    size_bit_count += 7;
    size_max_next += ((uint64_t)1) << size_bit_count;
    size_byte_count += 1;
}
write_size -= size_max;

uint8_t head_byte = 0;
head_byte |= ((write_size & loh_size_mask) << 1) | ((size_byte_count > 1) ? 1 : 0);
write_size >>= loh_size_bits;

byte_push(&ret, head_byte);

for (size_t n = 1; n < size_byte_count; n++)
{
    byte_push(&ret, ((write_size & 0x7F) << 1) | ((n + 1 < size_byte_count) ? 1 : 0));
    write_size >>= 7;
}
```

And decoded like:

```c
uint8_t size_continues = dat & 1;
size_t size = (dat >> 1) & loh_size_mask;

// ...

uint64_t n = loh_size_bits; // 3
while (size_continues)
{
    _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
    uint64_t cont_dat = input[i++];
    size_continues = cont_dat & 1;
    size += ((cont_dat >> 1) << n);
    size += ((uint64_t)1) << n;
    n += 7;
}
```

This slightly complicates encoding/decoding but gives a "free" entropy savings that allows the entropy coder to be more efficient.

Lookback commands cannot reference data from previous chunks. In theory, this allows for parallel encoding and decoding.

### Huffman coding

LOH uses canonical Huffman codes to allow for faster decoding.

The huffman stream is split up into chunks. Each chunk has its own code table, and is prefixed by a 32-bit length. For simplicity's sake, the encoder strictly works with 32k-sized chunks. These chunks are in addition to the LOH-file-global chunks.

Also, this stage uses individual bit access, unlike the lookback stage. The encoder writes bits starting with the first bit in the least-significant bit of the first byte (the 1 bit), going up to the most-significant bit (the 128 bit), then going on to the 1 bit of the second byte, and so on.

The Huffman code table is stored at the start of the output of this stage, in order from shortest to longest code, in order of code value, starting with a code length of 1. The encoder outputs a `1` bit to indicate that it's moved on to the next code length, or a `0` to indicate that there's another symbol associated with this code length, followed by whatever that symbol is. Symbols are encoded as a difference from the previous symbol, with that difference encoded like:

```
0 : 1
10 : 2
110 : 3
1110 : 4
1111xxxxxxxx : all other diffs (the x bits are least-significant-first)
```

Before the code table, the number of symbols coded in it minus one is written as an 8-bit integer. So if there are 8 symbols `0x07` is written. This is how the decoder knows when to stop decoding the code table and start decoding the compressed data.

After the code table, the encoder rounds its bit cursor up to the start of the next byte, allowing the decoder to work on a byte-by-byte basis when decompressing the compressed data rather instead of slow bit-banging.

Huffman codes in the compressed data are stored starting from the most significant bit of each code word (i.e. the root of the Huffman tree) and working towards the least significant bit. These bits are written into the output buffer starting at the least-significant bit of a given byte and working towards the most-significant bit of that byte, before moving on to the next byte, where bits also start out being stored in the least-significant bit.


