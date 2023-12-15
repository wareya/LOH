#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef THREADED
#include "loh_lite.h"
#else
#include "loh_impl_threaded.h"
#endif

int main(int argc, char ** argv)
{
    if (argc < 4 || (argv[1][0] != 'z' && argv[1][0] != 'x'))
    {
        puts("usage: loh (z[0-9]|x) <in> <out> [0-9] [0|1] [number]");
        puts("");
        puts("z: compresses <in> into <out>");
        puts("x: decompresses <in> into <out>");
        puts("");
        puts("The three numeric arguments at the end are for z (compress) mode.");
        puts("");
        puts("The first turns on lookback, with different numbers corresponding to\n"
            "different compression qualities. The default value is 4, which is\n"
            "pretty low quality but fast enough to be reasonable. 1 means fastest,\n"
            "9 means slowest.");
        puts("");
        puts("The second turns on Huffman coding.");
        puts("");
        puts("The third turns on delta coding, with a byte distance. 3 does good for\n"
            "3-channel RGB images, 4 does good for 4-channel RGBA images or 16-bit\n"
            "PCM audio. Only if they're not already compressed, though. Does not\n"
            "generally work well with most files, like text.");
        puts("");
        puts("If given, the numeric arguments must be given in order. If not given,\n"
            "their defaults are 5, 1, 0. In other words, RLE and Huffman are enabled\n"
            "by default, but delta coding is not.");
        puts("");
        puts("Lookback and huffman are disabled for chunks of file that don't benefit.");
        return 0;
    }
    FILE * f = fopen(argv[2], "rb");
    if (!f)
    {
        puts("error: failed to open input file");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size_t file_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t * raw_data = (uint8_t *)malloc(file_len);
    fread(raw_data, file_len, 1, f);
    loh_byte_buffer buf = {raw_data, file_len, file_len};
    
    fclose(f);
    
    if (argv[1][0] == 'z')
    {
        uint8_t do_diff = 0;
        int8_t do_lookback = 5;
        uint8_t do_huff = 1;
        
        if (argc > 4)
            do_lookback = strtol(argv[4], 0, 10);
        if (argc > 5)
            do_huff = strtol(argv[5], 0, 10);
        if (argc > 6)
            do_diff = strtol(argv[6], 0, 10);
        
#ifdef THREADED
        buf.data = loh_compress_threaded(buf.data, buf.len, do_lookback, do_huff, do_diff, &buf.len, 4);
        (void)(loh_compress);
#else
        buf.data = loh_compress(buf.data, buf.len, do_lookback, do_huff, do_diff, &buf.len);
#endif
        
        FILE * f2 = fopen(argv[3], "wb");
        
        // WHY IS THIS FASTER THAN JUST WRITING THE FILE ALL AT ONCE IF IT'S REALLY BIG
        const size_t chunk_size = 1 << 20;
        while (buf.len > chunk_size)
        {
            fwrite(buf.data, chunk_size, 1, f2);
            buf.data += chunk_size;
            buf.len -= chunk_size;
        }
        fwrite(buf.data, buf.len, 1, f2);
        
        fclose(f2);
    }
    else if (argv[1][0] == 'x')
    {
#ifdef THREADED
        buf.data = loh_decompress_threaded(buf.data, buf.len, &buf.len, 1);
        (void)(loh_decompress);
#else
        buf.data = loh_decompress(buf.data, buf.len, &buf.len, 1);
#endif
        
        if (buf.data)
        {
            FILE * f2 = fopen(argv[3], "wb");
            // WHY IS THIS FASTER THAN JUST WRITING THE FILE ALL AT ONCE IF IT'S REALLY BIG
            const size_t chunk_size = 1 << 20;
            while (buf.len > chunk_size)
            {
                fwrite(buf.data, chunk_size, 1, f2);
                buf.data += chunk_size;
                buf.len -= chunk_size;
            }
            fwrite(buf.data, buf.len, 1, f2);
            fclose(f2);
        }
        else
        {
            fprintf(stderr, "error: decompression failed");
            exit(-1);
        }
    }
    
    free(raw_data);
    
    return 0;
}