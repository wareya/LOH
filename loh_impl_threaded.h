#ifndef LOH_IMPL_THREADED_HEADER
#define LOH_IMPL_THREADED_HEADER

#include "loh_impl.h"

#include <pthread.h>

typedef struct {
    uint8_t * data;
    uint64_t data_len;
    uint8_t do_diff;
    uint8_t do_lookback;
    uint8_t do_huff;
    uint8_t buf_replaced;
} loh_compress_threaded_args;

static void * loh_compress_threaded_single(void * _args)
{
    loh_compress_threaded_args * args = (loh_compress_threaded_args *)_args;
    uint8_t * raw_data = args->data;
    uint64_t in_size = args->data_len;
    uint8_t do_diff = args->do_diff;
    uint8_t quality_level = args->do_lookback;
    uint8_t do_huff = args->do_huff;
    //printf("%lld %lld\n", in_start, in_end);
    
    loh_byte_buffer buf = {raw_data, in_size, in_size};
    
    // detect probably-good differentiation stride
    // step 1: figure out the typical absolute difference between bytes
    // (128 isn't guaranteed)
    
    int64_t difference = 0;
    uint64_t rand = 19529;
    const uint64_t m = 0xA68BF0C7;
    uint8_t seen_values[256] = {0};
    for (size_t n = 0; n < 4096; n += 1)
    {
        rand *= m + n * 2;
        size_t a = rand % in_size;
        rand *= m + n * 2;
        size_t b = rand % in_size;
        int16_t diff = (int16_t)raw_data[a] - (int16_t)raw_data[b];
        diff = diff < 0 ? -diff : diff;
        difference += diff;
        seen_values[raw_data[a]] = 1;
        seen_values[raw_data[b]] = 1;
    }
    // to prevent differentiating files that only have a small number of unique values (doing so thrashes the entropy coder)
    uint16_t num_seen_values = 0;
    for (size_t n = 0; n < 256; n++)
        num_seen_values += seen_values[n];
    difference /= 4096;
    
    int64_t orig_difference = difference;
    
    // now check 1 through 16 as possible differentiation values, using a similar strategy
    if (!do_diff && num_seen_values > 128)
    {
        for (uint8_t diff_opt = 1; diff_opt <= 16; diff_opt += 1)
        {
            int64_t diff_difference = 0;
            if (diff_opt * 2 > in_size)
                break;
            for (size_t n = 0; n < 4096; n += 1)
            {
                rand *= m + n * 2;
                size_t a = rand % (in_size - diff_opt);
                int16_t diff = (int16_t)raw_data[a] - (int16_t)raw_data[a + diff_opt];
                diff = diff < 0 ? -diff : diff;
                diff_difference += diff;
            }
            diff_difference /= 4096;
            // 2x to prevent noise from triggering differentiation when it's not necessary
            if (diff_difference * 2 < orig_difference && diff_difference < difference)
            {
                difference = diff_difference;
                do_diff = diff_opt;
            }
        }
    }
    
    uint8_t buf_replaced = 0;
    
    if (do_diff)
    {
        for (size_t i = buf.len - 1; i >= do_diff; i -= 1)
            buf.data[i] -= buf.data[i - do_diff];
    }
    
    loh_byte_buffer orig_buf = buf;
    
    size_t lb_comp_ratio_100 = 100;
    
    if (quality_level)
    {
        loh_byte_buffer new_buf = lookback_compress(buf.data, buf.len, quality_level);
        
        if (new_buf.len < buf.len)
        {
            lb_comp_ratio_100 = new_buf.len * 100 / buf.len;
            buf = new_buf;
            buf_replaced = 1;
        }
        else
        {
            LOH_FREE(new_buf.data);
            quality_level = 0;
        }
    }
    uint8_t did_huff = 0;
    if (do_huff)
    {
        loh_byte_buffer new_buf = huff_pack(buf.data, buf.len).buffer;
        if (new_buf.len < buf.len)
        {
            if (buf_replaced)
                LOH_FREE(buf.data);
            buf = new_buf;
            did_huff = 1;
            buf_replaced = 1;
            
            // if we did lookback but it's tenuous, try huff-compressing the original data too to see if it comes out smaller
            
            if (quality_level && (lb_comp_ratio_100 > 80 || (do_diff != 0 && lb_comp_ratio_100 > 30)))
            {
                loh_byte_buffer new_buf_2 = huff_pack(orig_buf.data, orig_buf.len).buffer;
                
                if (new_buf_2.len < buf.len)
                {
                    buf = new_buf_2;
                    quality_level = 0;
                }
                else
                    LOH_FREE(new_buf_2.data);
            }
        }
        else
        {
            LOH_FREE(new_buf.data);
            did_huff = 0;
        }
    }
    
    args->data = buf.data;
    args->data_len = buf.len;
    args->do_diff = do_diff;
    args->do_lookback = quality_level;
    args->do_huff = did_huff;
    args->buf_replaced = buf_replaced;
    return (void *) args;
}
    

// passed-in data is modified, but not stored; it still belongs to the caller, and must be freed by the caller
// returned data must be freed by the caller; it was allocated with LOH_MALLOC
static uint8_t * loh_compress_threaded(uint8_t * data, size_t len, uint8_t do_lookback, uint8_t do_huff, uint8_t do_diff, size_t * out_len, uint16_t threads)
{
    if (!data || !out_len) return 0;
    
    uint32_t checksum = loh_checksum(data, len);
    
    // LOH files are composed of a series of arbitrary-length chunks.
    // Chunks have their compressed and decompressed start addresses stored in the header,
    //  and also the address just past the end of the last chunk (still for both).
    // Compression config is stored on a per-chunk basis at the start of each compressed chunk.
    // For the sake of simplicity, the reference compressor divides the file into at most N chunks,
    //  or chunks with 32KB length, whichever gives bigger chunks.
    // There is no maximum chunk size.
    
    uint64_t chunk_div = threads;
    uint64_t chunk_size = (len + chunk_div - 1) / chunk_div;
    if (chunk_size < (1 << 15))
        chunk_size = (1 << 15);
    uint64_t chunk_count = (len + chunk_size - 1) / chunk_size;
    
    //printf("%lld\n", chunk_count);
    
    loh_byte_buffer real_buf = {0, 0, 0};
    
    bytes_push(&real_buf, (const uint8_t *)"LOHz", 4);
    bytes_push(&real_buf, (uint8_t *)&checksum, 4);
    bytes_push(&real_buf, (uint8_t *)&chunk_count, 8);
    
    size_t chunk_table_loc = real_buf.len;
    for (size_t i = 0; i < chunk_count + 1; i += 1)
    {
        uint64_t n = 0;
        bytes_push(&real_buf, (uint8_t *)&n, 8);
        bytes_push(&real_buf, (uint8_t *)&n, 8);
    }
    size_t chunk_table_end = real_buf.len;
    
    pthread_t * thread_table = (pthread_t *)LOH_MALLOC(sizeof(pthread_t) * chunk_count);
    loh_compress_threaded_args * thread_args  = (loh_compress_threaded_args *)LOH_MALLOC(sizeof(loh_compress_threaded_args) * chunk_count);
    
    uint64_t total_uncompressed_len = 0;
    for (size_t i = 0; i < chunk_count; i += 1)
    {
        uint64_t in_start = i * chunk_size;
        uint64_t in_end = (i + 1) * chunk_size;
        if (in_end > len)
            in_end = len;
        
        uint64_t * chunk_table = (uint64_t *)&real_buf.data[chunk_table_loc];
        chunk_table[i * 2 + 1] = total_uncompressed_len;
        total_uncompressed_len += in_end - in_start;
        
        loh_compress_threaded_args * args = &thread_args[i];
        args->data = &data[in_start];
        args->data_len = in_end - in_start;
        args->do_diff = do_diff;
        args->do_lookback = do_lookback;
        args->do_huff = do_huff;
        args->buf_replaced = 0;
        
        pthread_create(&thread_table[i], NULL, loh_compress_threaded_single, args);
    }
    
    uint64_t total_compressed_len = chunk_table_end;
    for (size_t i = 0; i < chunk_count; i += 1)
    {
        uint64_t * chunk_table = (uint64_t *)&real_buf.data[chunk_table_loc];
        chunk_table[i * 2 + 0] = total_compressed_len;
        
        pthread_join(thread_table[i], 0);
        
        // join thread
        loh_compress_threaded_args * ret = &thread_args[i];
        
        byte_push(&real_buf, ret->do_diff);
        byte_push(&real_buf, ret->do_lookback);
        byte_push(&real_buf, ret->do_huff);
        byte_push(&real_buf, 0);
        bytes_push(&real_buf, ret->data, ret->data_len);
        
        if (ret->buf_replaced)
            LOH_FREE(ret->data);
        
        total_compressed_len += ret->data_len + 4;
    }
    
    uint64_t * chunk_table = (uint64_t *)&real_buf.data[chunk_table_loc];
    chunk_table[chunk_count * 2 + 0] = total_compressed_len;
    chunk_table[chunk_count * 2 + 1] = total_uncompressed_len;
    
    *out_len = real_buf.len;
    return real_buf.data;
}

typedef struct {
    uint8_t * in_data;
    uint64_t in_data_len;
    uint8_t * out_data;
    uint64_t out_data_len;
    uint8_t error;
} loh_decompress_threaded_args;


static void * loh_decompress_threaded_single(void * _args)
{
    loh_decompress_threaded_args * args = (loh_decompress_threaded_args *)_args;
    
    uint8_t * chunk_start = args->in_data;
    uint64_t chunk_len = args->in_data_len;
    
    uint8_t * out_data = args->out_data;
    uint64_t out_data_len = args->out_data_len;
    
    uint8_t * out_error = &args->error;
    
    loh_byte_buffer buf = {chunk_start, chunk_len, chunk_len};
    
    uint8_t do_diff = buf.data[0];
    uint8_t do_lookback = buf.data[1];
    uint8_t do_huff = buf.data[2];
    
    buf.data += 4;
    buf.len -= 4;
    
    uint8_t * buf_orig = buf.data;
    
    if (do_huff)
    {
        loh_bit_buffer compressed;
        memset(&compressed, 0, sizeof(loh_bit_buffer));
        compressed.buffer = buf;
        int error = 0;
        loh_byte_buffer new_buf = huff_unpack(&compressed, &error);
        if (buf.data != buf_orig)
            LOH_FREE(buf.data);
        buf = new_buf;
        if (!error && !buf.data)
        {
            *out_error = 2;
            return 0;
        }
        if (error)
        {
            if (buf.data && buf.data != buf_orig)
                LOH_FREE(buf.data);
            *out_error = 1;
            return 0;
        }
    }
    if (do_lookback)
    {
        int error = 0;
        loh_byte_buffer new_buf = lookback_decompress(buf.data, buf.len, &error);
        if (buf.data != buf_orig)
            LOH_FREE(buf.data);
        buf = new_buf;
        if (!error && !buf.data)
        {
            *out_error = 2;
            return 0;
        }
        if (error)
        {
            if (buf.data && buf.data != buf_orig)
                LOH_FREE(buf.data);
            *out_error = 1;
            return 0;
        }
    }
    if (do_diff)
    {
        for (size_t i = do_diff; i < buf.len; i += 1)
            buf.data[i] += buf.data[i - do_diff];
    }
    
    for (size_t i = 0; i < out_data_len; i += 1)
        out_data[i] = buf.data[i];
    
    return 0;
}
    

// input data is modified, but not stored; it still belongs to the caller, and must be freed by the caller
// if huffman and lookback coding are both disabled, then the returned pointer is a pointer into the input data
// otherwise, returned data must be freed by the caller; it was allocated with LOH_MALLOC
static uint8_t * loh_decompress_threaded(uint8_t * data, size_t len, size_t * out_len, uint8_t check_checksum)
{
    if (!data || !out_len) return 0;
    
    if (len < 8 || memcmp(data, "LOHz", 4) != 0)
        return 0;
    
    uint32_t stored_checksum = data[4]
        | (((uint32_t)data[5]) << 8)
        | (((uint32_t)data[6]) << 16)
        | (((uint32_t)data[7]) << 24);
    
    uint64_t chunk_count = data[8]
        | (((uint64_t)data[9]) << 8)
        | (((uint64_t)data[10]) << 16)
        | (((uint64_t)data[11]) << 24)
        | (((uint64_t)data[12]) << 32)
        | (((uint64_t)data[13]) << 40)
        | (((uint64_t)data[14]) << 48)
        | (((uint64_t)data[15]) << 56);
    
    const uint64_t * chunk_table = (uint64_t *)&data[16];
    
    uint64_t output_len = chunk_table[chunk_count * 2 + 1];
    
    loh_byte_buffer out_buf = {0, 0, 0};
    bytes_reserve(&out_buf, output_len);
    out_buf.len = output_len;
    
    pthread_t * thread_table = (pthread_t *)LOH_MALLOC(sizeof(pthread_t) * chunk_count);
    loh_decompress_threaded_args * thread_args  = (loh_decompress_threaded_args *)LOH_MALLOC(sizeof(loh_decompress_threaded_args) * chunk_count);
    
    for (size_t i = 0; i < chunk_count; i += 1)
    {
        loh_decompress_threaded_args * args = &thread_args[i];
        args->in_data = &data[chunk_table[i * 2]];
        args->in_data_len = chunk_table[i * 2 + 2] - chunk_table[i * 2];
        args->out_data = &out_buf.data[chunk_table[i * 2 + 1]];
        args->out_data_len = chunk_table[i * 2 + 3] - chunk_table[i * 2 + 1];
        args->error = 0;
        
        pthread_create(&thread_table[i], NULL, loh_decompress_threaded_single, args);
    }
    for (size_t i = 0; i < chunk_count; i += 1)
        pthread_join(thread_table[i], 0);
    
    uint32_t checksum;
    if (stored_checksum != 0 && check_checksum)
        checksum = loh_checksum(out_buf.data, out_buf.len);
    else
        checksum = stored_checksum;
    
    if (checksum == stored_checksum || !check_checksum)
    {
        *out_len = out_buf.len;
        return out_buf.data;
    }
    else
    {
        if (out_buf.data)
            LOH_FREE(out_buf.data);
        return 0;
    }
}

#endif // LOH_IMPL_THREADED_HEADER
