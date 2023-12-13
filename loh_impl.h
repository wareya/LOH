#ifndef LOH_IMPL_HEADER
#define LOH_IMPL_HEADER

/*
    LOH (LOokahead + Huffman)
    Single-header compression/decompression library.
    Uses a bespoke format.
    You probably want these functions:
        loh_compress
        loh_decompress
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// must return a buffer with at least 8-byte alignment
#ifndef LOH_REALLOC
#define LOH_REALLOC realloc
#endif

// must return a buffer with at least 8-byte alignment
#ifndef LOH_MALLOC
#define LOH_MALLOC malloc
#endif

#ifndef LOH_FREE
#define LOH_FREE free
#endif

/* data structures and other shared code */

typedef struct {
    uint8_t * data;
    size_t len;
    size_t cap;
} loh_byte_buffer;

static inline void bytes_reserve(loh_byte_buffer * buf, size_t extra)
{
    if (buf->cap < 8)
        buf->cap = 8;
    while (buf->len + extra > buf->cap)
        buf->cap <<= 1;
    buf->data = (uint8_t *)LOH_REALLOC(buf->data, buf->cap);
    if (!buf->data)
        buf->cap = 0;
}
static inline void bytes_push(loh_byte_buffer * buf, const uint8_t * bytes, size_t count)
{
    bytes_reserve(buf, count);
    memcpy(&buf->data[buf->len], bytes, count);
    buf->len += count;
}
static inline void byte_push(loh_byte_buffer * buf, uint8_t byte)
{
    if (buf->len == buf->cap)
    {
        buf->cap = buf->cap << 1;
        if (buf->cap < 8)
            buf->cap = 8;
        buf->data = (uint8_t *)LOH_REALLOC(buf->data, buf->cap);
    }
    buf->data[buf->len] = byte;
    buf->len += 1;
}

typedef struct {
    loh_byte_buffer buffer;
    size_t byte_index;
    size_t bit_count;
    uint8_t bit_index;
} loh_bit_buffer;

static inline void bits_push(loh_bit_buffer * buf, uint64_t data, uint8_t bits)
{
    if (bits == 0)
        return;
    
    // push byte if there's nothing to write bits into
    if (buf->buffer.len == 0)
        byte_push(&buf->buffer, 0);
    
    // if we have more bits to push than are available, push as many bits as possible without adding new bytes all at once
    if (bits >= 8 - buf->bit_index)
    {
        uint8_t avail = 8 - buf->bit_index;
        uint64_t mask = (1 << avail) - 1;
        buf->buffer.data[buf->buffer.len - 1] |= (data & mask) << buf->bit_index;
        
        byte_push(&buf->buffer, 0);
        
        buf->bit_index = 0;
        buf->byte_index += 1;
        
        bits -= avail;
        data >>= avail;
        
        // then push any remaining whole bytes worth of bits all at once
        while (bits >= 8)
        {
            buf->buffer.data[buf->buffer.len - 1] |= data & 0xFF;
            bits -= 8;
            data >>= 8;
            byte_push(&buf->buffer, 0);
            buf->byte_index += 1;
        }
    }
    
    // push any remaining bits (we'll be at less than 8 bits to write and more than 8 bits available)
    if (bits > 0)
    {
        uint64_t mask = (1 << bits) - 1;
        buf->buffer.data[buf->buffer.len - 1] |= (data & mask) << buf->bit_index;
        buf->bit_index += bits;
        buf->bit_count += bits;
        return;
    }
}
static inline void bit_push(loh_bit_buffer * buf, uint8_t data)
{
    if (buf->bit_index >= 8 || buf->buffer.len == 0)
    {
        byte_push(&buf->buffer, 0);
        if (buf->bit_index >= 8)
        {
            buf->bit_index -= 8;
            buf->byte_index += 1;
        }
    }
    buf->buffer.data[buf->buffer.len - 1] |= data << buf->bit_index;
    buf->bit_index += 1;
    buf->bit_count += 1;
}
static inline uint64_t bits_pop(loh_bit_buffer * buf, uint8_t bits)
{
    if (buf->byte_index >= buf->buffer.len)
        return 0;
    if (bits == 0)
        return 0;
    uint64_t ret = 0;
    for (uint8_t n = 0; n < bits; n += 1)
    {
        if (buf->bit_index >= 8)
        {
            buf->bit_index -= 8;
            buf->byte_index += 1;
        }
        ret |= (uint64_t)((buf->buffer.data[buf->byte_index] >> buf->bit_index) & 1) << n;
        buf->bit_index += 1;
    }
    return ret;
}
static inline uint8_t bit_pop(loh_bit_buffer * buf)
{
    if (buf->byte_index >= buf->buffer.len)
        return 0;
    if (buf->bit_index >= 8)
    {
        buf->bit_index -= 8;
        buf->byte_index += 1;
    }
    uint8_t ret = (buf->buffer.data[buf->byte_index] >> buf->bit_index) & 1;
    buf->bit_index += 1;
    
    return ret;
}

static uint32_t loh_checksum(uint8_t * data, size_t len)
{
    const uint32_t stripes = 4;
    const uint32_t big_prime = 0x1011B0D5;
    uint32_t checksum = 0x87654321;
    
    uint32_t partial_sum[stripes];
    for (size_t j = 0; j < stripes; j++)
        partial_sum[j] = checksum + j;
    
    size_t checksum_i = 0;
    while (checksum_i + (stripes - 1) < len)
    {
        for (size_t j = 0; j < stripes; j++)
            partial_sum[j] = (partial_sum[j] + data[checksum_i++]) * big_prime;
    }
    
    for (size_t j = 0; j < stripes; j++)
        checksum = (checksum + partial_sum[j]) * big_prime;
    
    while (checksum_i < len)
        checksum = (checksum + data[checksum_i++]) * big_prime;
    
    checksum += len;
    
    return checksum;
}

/* compression */

static const size_t loh_min_lookback_length = 4;

// for finding lookback matches, we use a fast quasi-lru cache based on a hash table
// collisions and identical matches share eviction; the number of values per hash is static

typedef struct {
    // table of hash (hashed four bytes) to value (address in file)
    // we have 2<<n cells per hash
    uint64_t * hashtable;
    // overwriting cursor
    uint8_t * hashtable_i;
    // size of the hash function output in bits.
    // must be at most 32, but values significantly above 16 are a Bad Idea.
    // higher values take up exponentially more memory.
    // default: 15
    uint8_t hash_size;
    // log2 of the number of values per key (0 -> 1 value per key, 1 -> 2, 2 -> 4, 3 -> 8, 4 -> 16, etc)
    // higher values are slower, but result in smaller files. 8 is the max.
    // higher values take up exponentially more memory and are exponentially slower.
    // default: 2
    uint8_t hash_shl;
    uint16_t hash_i_max;
    uint16_t hash_i_mask;
} loh_hashmap;

#define LOH_HASH_LENGTH 4
static inline uint32_t hashmap_hash(loh_hashmap * hashmap, const uint8_t * bytes)
{
    // hashing function (can be anything; go ahead and optimize it as long as it doesn't result in tons of collisions)
    uint32_t temp = 0xA68BF0C7;
    // unaligned-safe 32-bit load
    uint32_t a = bytes[0];
    a |= ((uint32_t)bytes[0]) << 0;
    a |= ((uint32_t)bytes[1]) << 8;
    a |= ((uint32_t)bytes[2]) << 16;
    a |= ((uint32_t)bytes[3]) << 24;
    // then just multiply it by the const and return the top N bits
    temp *= a;
    return temp >> (32 - hashmap->hash_size);
}

// bytes must point to four characters
static inline void hashmap_insert(loh_hashmap * hashmap, const uint8_t * bytes, uint64_t value)
{
    const uint32_t key_i = hashmap_hash(hashmap, bytes);
    const uint32_t key = key_i << hashmap->hash_shl;
    
    hashmap->hashtable[key + hashmap->hashtable_i[key_i]] = value;
    hashmap->hashtable_i[key_i] = (hashmap->hashtable_i[key_i] + 1) & hashmap->hash_i_mask;
}

// bytes must point to four characters and be inside of buffer
static inline uint64_t hashmap_get(loh_hashmap * hashmap, size_t i, const uint8_t * buffer, const size_t buffer_len, uint64_t * min_len)
{
    const uint8_t * bytes = &buffer[i];
    const uint32_t key_i = hashmap_hash(hashmap, bytes);
    const uint32_t key = key_i << hashmap->hash_shl;
    
    // look for match within key
    uint64_t best = -1;
    uint64_t best_size = loh_min_lookback_length - 1;
    for (uint16_t j = 0; j < hashmap->hash_i_max; j++)
    {
        // cycle from newest to oldest
        int n = (hashmap->hashtable_i[key_i] + hashmap->hash_i_max - 1 - j) & hashmap->hash_i_mask;
        const uint64_t value = hashmap->hashtable[key + n];
        
        if (value >= i)
            break;
        
        // early-out for things that can't possibly be an (efficient) match
        if (bytes[0] != buffer[value] || bytes[1] != buffer[value + 1] || bytes[2] != buffer[value + 2])
            continue;
        
        // find longest match
        // if we hit 128 bytes we call it good enough and take it
        const uint64_t good_enough_length = 128;
        
        // testing in chunks is significantly faster than testing byte-by-byte
        uint64_t remaining = buffer_len - i;
        if (remaining > good_enough_length)
            remaining = good_enough_length;
        
        uint64_t size = 0;
        while (size < remaining && bytes[size] == buffer[value + size])
            size += 1;
        
        if (size > best_size || (size == best_size && value > best))
        {
            best_size = size;
            best = value;
            
            if (best_size >= good_enough_length)
                break;
        }
    }
    
    *min_len = best_size;
    return best;
}

    
// bits within lookback header byte (which spends 2 bits on length extension bits)
static const size_t loh_size_bits = 3;
static const size_t loh_size_mask = (1 << loh_size_bits) - 1;
static const size_t loh_dist_bits = 6 - loh_size_bits;
static const size_t loh_dist_mask = (1 << loh_dist_bits) - 1;

static inline uint64_t hashmap_get_if_efficient(loh_hashmap * hashmap, const size_t i, const uint8_t * input, const uint64_t input_len, const uint8_t final, uint64_t * out_size)
{
    // here we only return the hashmap hit if it would be efficient to code it
    
    uint64_t remaining = input_len - i;
    if (i >= input_len || remaining <= loh_min_lookback_length)
        return -1;
    
    uint64_t size = 0;
    const uint64_t found_loc = hashmap_get(hashmap, i, input, input_len, &size);
    uint64_t dist = i - found_loc;
    if (found_loc != (uint64_t)-1 && found_loc < i)
    {
        // find true length of match match
        // (this is significantly faster than testing byte-by-byte)
        while (size < remaining && input[i + size] == input[found_loc + size])
            size += 1;
        
        size_t dist_max_next = loh_dist_mask + 1;
        size_t dist_byte_count = 1;
        size_t dist_bit_count = loh_dist_bits;
        while (dist >= dist_max_next)
        {
            dist_bit_count += 7;
            dist_max_next += 1 << dist_bit_count;
            dist_byte_count += 1;
        }
        
        uint64_t overhead = dist_byte_count;
        
        // cost of extra size byte if size is long
        // if size is more than one extra byte long, then overhead will definitely be less than size, so we don't have to account for it
        if (size - loh_min_lookback_length > loh_size_mask)
            overhead += 1;
        
        if (!final) // cost of switching out of literal mode
            overhead += 1;
        
        if (overhead < size)
        {
            *out_size = size;
            return found_loc;
        }
    }
    return -1;
}

static loh_byte_buffer lookback_compress(const uint8_t * input, uint64_t input_len, int8_t quality_level)
{
    uint8_t hash_size = 13;
    uint8_t hash_shl = 0;
    if (quality_level > 15)
        quality_level = 15;
    if (quality_level < -10)
        quality_level = -10;
    if (quality_level > 0)
    {
        hash_size += (quality_level + 1) / 2;
        hash_shl += quality_level / 2;
    }
    else
    {
        quality_level += 1;
        hash_size += quality_level;
    }
    size_t hash_capacity = ((size_t)1) << (hash_size + hash_shl);
    size_t hash_i_capacity = ((size_t)1) << hash_size;
    
    loh_byte_buffer ret = {0, 0, 0};
    
    loh_hashmap hashmap;
    
    hashmap.hashtable = (uint64_t *)LOH_MALLOC(sizeof(uint64_t) * hash_capacity);
    if (!hashmap.hashtable)
        return ret;
    
    hashmap.hashtable_i = (uint8_t *)LOH_MALLOC(sizeof(uint8_t) * hash_i_capacity);
    if (!hashmap.hashtable_i)
        return ret;
    
    hashmap.hash_size = hash_size;
    hashmap.hash_shl = hash_shl;
    hashmap.hash_i_max = (1 << hash_shl);
    hashmap.hash_i_mask = (1 << hash_shl) - 1;
    
    memset(hashmap.hashtable, 0, sizeof(uint64_t) * hash_capacity);
    memset(hashmap.hashtable_i, 0, sizeof(uint8_t) * hash_i_capacity);
    
    byte_push(&ret, input_len & 0xFF);
    byte_push(&ret, (input_len >> 8) & 0xFF);
    byte_push(&ret, (input_len >> 16) & 0xFF);
    byte_push(&ret, (input_len >> 24) & 0xFF);
    byte_push(&ret, (input_len >> 32) & 0xFF);
    byte_push(&ret, (input_len >> 40) & 0xFF);
    byte_push(&ret, (input_len >> 48) & 0xFF);
    byte_push(&ret, (input_len >> 56) & 0xFF);
    
    uint64_t i = 0;
    uint64_t l = 0;
    uint64_t found_size = 0;
    uint64_t found_loc = 0;
    while (i < input_len)
    {
        // check for lookback hit
        if (found_size != 0)
        {
            l += 1;
            
            uint64_t dist = i - found_loc;
            
            if (dist > i)
            {
                fprintf(stderr, "LOH internal error: broken lookback distance calculation\n");
                exit(-1);
            }
            
            // advance cursor and update hashmap
            uint64_t start_i = i;
            i += 1;
            uint64_t temp_loc = 0;
            uint64_t temp_size = 0;
            for (size_t j = 1; j < found_size; j++)
            {
                if (i + LOH_HASH_LENGTH < input_len)
                    hashmap_insert(&hashmap, &input[i], i);
                i += 1;
            }
            if (start_i + LOH_HASH_LENGTH < input_len)
                hashmap_insert(&hashmap, &input[start_i], start_i);
            
            
            size_t write_size = found_size - loh_min_lookback_length;
            
            uint8_t head_byte = 0;
            
            size_t size_max = 0;
            size_t size_max_next = loh_size_mask + 1;
            size_t size_byte_count = 1;
            size_t size_bit_count = loh_size_bits;
            while (write_size >= size_max_next)
            {
                size_max = size_max_next;
                size_bit_count += 7;
                size_max_next += ((uint64_t)1) << size_bit_count;
                size_byte_count += 1;
            }
            write_size -= size_max;
            
            size_t dist_max = 0;
            size_t dist_max_next = loh_dist_mask + 1;
            size_t dist_byte_count = 1;
            size_t dist_bit_count = loh_dist_bits;
            while (dist >= dist_max_next)
            {
                dist_max = dist_max_next;
                dist_bit_count += 7;
                dist_max_next += ((uint64_t)1) << dist_bit_count;
                dist_byte_count += 1;
            }
            dist -= dist_max;
            
            head_byte |= ((write_size & loh_size_mask) << 1) | (size_byte_count > 1);
            write_size >>= loh_size_bits;
            
            head_byte |= ((dist & loh_dist_mask) << (loh_size_bits + 2)) | ((dist_byte_count > 1) << (loh_size_bits + 1));
            dist >>= loh_dist_bits;
            
            byte_push(&ret, head_byte);
            
            for (size_t n = 1; n < size_byte_count; n++)
            {
                byte_push(&ret, ((write_size & 0x7F) << 1) | (n + 1 < size_byte_count));
                write_size >>= 7;
            }
            for (size_t n = 1; n < dist_byte_count; n++)
            {
                byte_push(&ret, ((dist & 0x7F) << 1) | (n + 1 < dist_byte_count));
                dist >>= 7;
            }
            
            found_size = 0;
            
            if (temp_size)
            {
                found_loc = temp_loc;
                found_size = temp_size;
                continue;
            }
        }
        
        // store a literal if we found no lookback
        uint64_t size = 0;
        while (i + size < input_len)
        {
            if (i + size + LOH_HASH_LENGTH < input_len)
                found_loc = hashmap_get_if_efficient(&hashmap, i + size, input, input_len, size == 0, &found_size);
            if (found_size != 0)
                break;
            // need to update the hashmap mid-literal
            if (i + size + LOH_HASH_LENGTH < input_len)
                hashmap_insert(&hashmap, &input[i + size], i + size);
            size += 1;
        }
        
        if (size > input_len - i)
            size = input_len - i;
        
        if (size == 0)
            continue;
        
        size_t write_size = size - 1;
        
        size_t size_max = 0;
        size_t size_max_next = loh_size_mask + 1;
        size_t size_byte_count = 1;
        size_t size_bit_count = loh_size_bits;
        while (write_size >= size_max_next)
        {
            size_max = size_max_next;
            size_bit_count += 7;
            size_max_next += ((uint64_t)1) << size_bit_count;
            size_byte_count += 1;
        }
        write_size -= size_max;
        
        uint8_t head_byte = 0;
        head_byte |= ((write_size & loh_size_mask) << 1) | (size_byte_count > 1);
        write_size >>= loh_size_bits;
        
        byte_push(&ret, head_byte);
        
        for (size_t n = 1; n < size_byte_count; n++)
        {
            byte_push(&ret, ((write_size & 0x7F) << 1) | (n + 1 < size_byte_count));
            write_size >>= 7;
        }
        
        bytes_push(&ret, &input[i], size);
        i += size;
    }
    
    LOH_FREE(hashmap.hashtable);
    LOH_FREE(hashmap.hashtable_i);
    
    return ret;
}

typedef struct _huff_node {
    struct _huff_node * children[2];
    int64_t freq;
    // We length-limit our codes to 15 bits, so storing them in a u16 is fine.
    uint16_t code;
    uint8_t code_len;
    uint8_t symbol;
} huff_node_t;

static huff_node_t * alloc_huff_node()
{
    return (huff_node_t *)LOH_MALLOC(sizeof(huff_node_t));
}

static void free_huff_nodes(huff_node_t * node)
{
    if (node->children[0])
        free_huff_nodes(node->children[0]);
    if (node->children[1])
        free_huff_nodes(node->children[1]);
    LOH_FREE(node);
}

static void push_code(huff_node_t * node, uint8_t bit)
{
    //node->code |= (bit & 1) << node->code_len;
    node->code <<= 1;
    node->code |= bit & 1;
    node->code_len += 1;
    
    if (node->children[0])
        push_code(node->children[0], bit);
    if (node->children[1])
        push_code(node->children[1], bit);
}

static int count_compare(const void * a, const void * b)
{
    int64_t n = *((int64_t*)b) - *((int64_t*)a);
    return n > 0 ? 1 : n < 0 ? -1 : 0;
}

static int huff_len_compare(const void * a, const void * b)
{
    int64_t len_a = (*(huff_node_t**)a)->code_len;
    int64_t len_b = (*(huff_node_t**)b)->code_len;
    if (len_a < len_b)
        return -1;
    else if (len_a > len_b)
        return 1;
    int64_t freq_a = (*(huff_node_t**)a)->freq;
    int64_t freq_b = (*(huff_node_t**)b)->freq;
    if (freq_a > freq_b)
        return -1;
    else if (freq_a < freq_b)
        return 1;
    return 0;
}
static loh_bit_buffer huff_pack(uint8_t * data, size_t len)
{
    // set up buffers and start pushing data to them
    loh_bit_buffer ret;
    memset(&ret, 0, sizeof(loh_bit_buffer));
    bits_push(&ret, len, 8*8);
    
    // The huffman stage is split up into chunks, so that each chunk can have a more ideal huffman code.
    // The chunk size is arbitrary, but for the sake of simplicity, this encoder uses a fixed 64k chunk size.
    // Each chunk is prefixed with a byte-aligned 32-bit integer giving the number of output tokens in the chunk.
    
    uint64_t chunk_size = (1 << 16);
    uint64_t chunk_count = (len + chunk_size - 1) / chunk_size;
    
    //uint64_t header_overhead_bytes = 0;
    
    for (uint32_t chunk = 0; chunk < chunk_count; chunk += 1)
    {
        size_t chunk_start = chunk * chunk_size;
        size_t chunk_end = (chunk + 1) * chunk_size;
        if (chunk + 1 == chunk_count)
            chunk_end = len;
        
        uint8_t * _data = &data[chunk_start];
        uint8_t * data = _data;
        size_t len = chunk_end - chunk_start;
        
        // the bit buffer is forcibly aligned to the start of the next byte at the start of the chunk
        if (ret.bit_index != 0)
            ret.bit_index = 8;
        
        bits_push(&ret, len, 8*4);
        
        //header_overhead_bytes += 4;
        
        // build huff dictionary
        
        // count bytes, then sort them
        uint64_t counts[256] = {0};
        uint64_t total_count = len;
        for (size_t i = 0; i < len; i += 1)
            counts[data[i]] += 1;
        // we stuff the byte identity into the bottom 8 bits
        size_t symbol_count = 0;
        for (size_t b = 0; b < 256; b++)
        {
            if (counts[b])
                symbol_count += 1;
            counts[b] = (counts[b] << 8) | b;
        }
        
        qsort(&counts, 256, sizeof(uint64_t), count_compare);
        
        // we want to generate a length-limited code with a maximum of 15 bits...
        // ... which means that the minimum frequency must be at least 1/(1<<14) of the total count
        // (we give ourselves 1 bit of leniency because the algorithm isn't perfect)
        if (symbol_count > 0)
        {
            const uint64_t n = 1 << 14;
            // use ceiled division to make super extra sure that we don't go over 1/16k
            uint64_t min_ok_count = (total_count + n - 1) / n;
            while ((counts[symbol_count-1] >> 8) < min_ok_count)
            {
                for (int i = symbol_count-1; i >= 0; i -= 1)
                {
                    // We use an x = max(minimum, x) approach instead of just adding to every count, because
                    //  if we never add to the most frequent item's frequency, we will definitely converge.
                    // (Specifically, this is guaranteed to converge if there are 16k or less symbols in
                    //  the dictionary, which there are. There are only 256 at most.)
                    // More proof of convergence: We will eventually add less than 16k to "total_count"
                    //  two `while` iterations in a row, which will cause min_ok_count to stop changing.
                    if (counts[i] >> 8 < min_ok_count)
                    {
                        uint64_t diff = min_ok_count - (counts[i] >> 8);
                        counts[i] += diff << 8;
                        total_count += diff;
                    }
                    else
                        break;
                }
                min_ok_count = (total_count + n - 1) / n;
            }
        }
        
        // set up raw huff nodes
        huff_node_t * unordered_dict[256];
        for (size_t i = 0; i < 256; i += 1)
        {
            unordered_dict[i] = alloc_huff_node();
            unordered_dict[i]->symbol = counts[i] & 0xFF;
            unordered_dict[i]->code = 0;
            unordered_dict[i]->code_len = 0;
            unordered_dict[i]->freq = counts[i] >> 8;
            unordered_dict[i]->children[0] = 0;
            unordered_dict[i]->children[1] = 0;
        }
        
        // set up byte name -> huff node dict
        huff_node_t * dict[256];
        for (size_t i = 0; i < 256; i += 1)
            dict[unordered_dict[i]->symbol] = unordered_dict[i];
        
        // set up tree generation queues
        huff_node_t * queue[512];
        memset(queue, 0, sizeof(queue));
        
        size_t queue_count = 256;
        
        for (size_t i = 0; i < 256; i += 1)
            queue[i] = unordered_dict[i];
        
        // remove zero-frequency items from the input queue
        while (queue_count > 0 && queue[queue_count - 1]->freq == 0)
        {
            free_huff_nodes(queue[queue_count - 1]);
            queue_count -= 1;
        }
        
        uint8_t queue_needs_free = 0;
        // start pumping through the queues
        while (queue_count > 1)
        {
            queue_needs_free = 1;
            
            huff_node_t * lowest = queue[queue_count - 1];
            huff_node_t * next_lowest = queue[queue_count - 2];
            
            queue_count -= 2;
            
            if (!lowest || !next_lowest)
            {
                fprintf(stderr, "LOH internal error: failed to find lowest-frequency nodes\n");
                exit(-1);
            }
            
            // make new node
            huff_node_t * new_node = alloc_huff_node();
            new_node->symbol = 0;
            new_node->code = 0;
            new_node->code_len = 0;
            new_node->freq = lowest->freq + next_lowest->freq;
            new_node->children[0] = next_lowest;
            new_node->children[1] = lowest;
            
            push_code(new_node->children[0], 0);
            push_code(new_node->children[1], 1);
            
            // insert new element at end of array, then bubble it down to the correct place
            queue[queue_count] = new_node;
            queue_count += 1;
            if (queue_count > 512)
            {
                fprintf(stderr, "LOH internal error: huffman tree generation too deep\n");
                exit(-1);
            }
            for (size_t i = queue_count - 1; i > 0; i -= 1)
            {
                if (queue[i]->freq >= queue[i-1]->freq)
                {
                    huff_node_t * temp = queue[i];
                    queue[i] = queue[i-1];
                    queue[i-1] = temp;
                }
            }
        }
        
        // With the above done, our basic huffman tree is built. Now we need to canonicalize it.
        // Canonicalization algorithms only work on sorted lists. Because of frequency ties, our
        //  code list might not be sorted by code length. Let's fix that by sorting it first.
        
        qsort(&unordered_dict, symbol_count, sizeof(huff_node_t*), huff_len_compare);
        
        // If we only have one symbol, we need to ensure that it thinks it has a code length of exactly 1.
        if (symbol_count == 1)
            unordered_dict[0]->code_len = 1;
        
        // Now we ACTUALLY canonicalize the huffman code list.
        
        uint64_t canon_code = 0;
        uint64_t canon_len = 0;
        uint16_t codes_per_len[256] = {0};
        for (size_t i = 0; i < symbol_count; i += 1)
        {
            if (canon_code == 0)
            {
                canon_len = unordered_dict[i]->code_len;
                codes_per_len[canon_len] += 1;
                unordered_dict[i]->code = 0;
                canon_code += 1;
                continue;
            }
            if (unordered_dict[i]->code_len > canon_len)
                canon_code <<= unordered_dict[i]->code_len - canon_len;
            
            canon_len = unordered_dict[i]->code_len;
            codes_per_len[canon_len] += 1;
            uint64_t code = canon_code;
            // we store codes with the most significant huffman bit in the least significant word bit
            // (this makes string encoding faster)
            for (size_t b = 0; b < canon_len / 2; b++)
            {
                size_t b2 = canon_len - b - 1;
                uint64_t diff = (!((code >> b) & 1)) != (!((code >> b2) & 1));
                diff = (diff << b) | (diff << b2);
                code ^= diff;
            }
            unordered_dict[i]->code = code;
            
            canon_code += 1;
        }
        
        // Our canonical length-limited huffman code is finally done!
        // To print it out (with modified frequencies):
        /*
        for (size_t c = 0; c < symbol_count; c += 1)
        {
            printf("%02X: ", unordered_dict[c]->symbol);
            for (size_t i = 0; i < unordered_dict[c]->code_len; i++)
                printf("%c", ((unordered_dict[c]->code >> i) & 1) ? '1' : '0');
            printf("\t %lld", unordered_dict[c]->freq);
            puts("");
        }
        */
        
        // Now we actually compress the input data.
        
        //size_t start_byte = ret.buffer.len;
        
        // push huffman code description
        // start at code length 1
        // bit 1: add 1 to code length
        // bit 0: read next 8 bits as symbol for next code. add 1 to code
        if (len > 0)
        {
            bits_push(&ret, symbol_count - 1, 8);
            size_t code_depth = 1;
            for (size_t i = 0; i < symbol_count; i++)
            {
                while (code_depth < unordered_dict[i]->code_len)
                {
                    bit_push(&ret, 1);
                    code_depth += 1;
                }
                bit_push(&ret, 0);
                bits_push(&ret, unordered_dict[i]->symbol, 8);
            }
        }
        
        //size_t end_byte = ret.buffer.len;
        
        //header_overhead_bytes += end_byte - start_byte + 1;
        
        // the bit buffer is forcibly aligned to the start of the next byte at the end of the huff tree
        if (ret.bit_index != 0)
            ret.bit_index = 8;
        
        // push huffman-coded string
        for (size_t i = 0; i < len; i++)
            bits_push(&ret, dict[data[i]]->code, dict[data[i]]->code_len);
        
        // despite all we've done to them, our huffman tree nodes still have their child pointers intact
        // so we can recursively free all our nodes all at once
        if (queue_needs_free)
            free_huff_nodes(queue[0]);
        // if we only have 0 or 1 nodes, then the queue doesn't run, so we need to free them directly
        // (only if there are actually any nodes, though)
        else if (symbol_count == 1)
            free_huff_nodes(unordered_dict[0]);
    }
    //printf("huff table overhead: %lld\n", header_overhead_bytes);
    return ret;
}

// passed-in data is modified, but not stored; it still belongs to the caller, and must be freed by the caller
// returned data must be freed by the caller; it was allocated with LOH_MALLOC
static uint8_t * loh_compress(uint8_t * data, size_t len, uint8_t do_lookback, uint8_t do_huff, uint8_t do_diff, size_t * out_len)
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
    
    uint64_t chunk_div = 4;
    uint64_t chunk_size = (len + chunk_div - 1) / chunk_div;
    if (chunk_size < (1 << 15))
        chunk_size = (1 << 15);
    uint64_t chunk_count = (len + chunk_size - 1) / chunk_size;
    
    printf("%lld\n", chunk_count);
    
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
    
    uint64_t total_compressed_len = chunk_table_end;
    uint64_t total_uncompressed_len = 0;
    for (size_t i = 0; i < chunk_count; i += 1)
    {
        uint64_t * chunk_table = (uint64_t *)&real_buf.data[chunk_table_loc];
        chunk_table[i * 2 + 0] = total_compressed_len;
        chunk_table[i * 2 + 1] = total_uncompressed_len;
        
        uint64_t in_start = i * chunk_size;
        uint64_t in_end = (i + 1) * chunk_size;
        if (in_end > len)
            in_end = len;
        
        //printf("%lld %lld\n", in_start, in_end);
        
        uint64_t in_size = in_end - in_start;
        
        uint8_t * raw_data = &data[in_start];
        
        loh_byte_buffer buf = {raw_data, in_size, in_size};
        
        if (do_diff)
        {
            for (size_t i = buf.len - 1; i >= do_diff; i -= 1)
                buf.data[i] -= buf.data[i - do_diff];
        }
        if (do_lookback)
        {
            loh_byte_buffer new_buf = lookback_compress(buf.data, buf.len, do_lookback);
            if (buf.data != raw_data)
                LOH_FREE(buf.data);
            buf = new_buf;
        }
        if (do_huff)
        {
            loh_byte_buffer new_buf = huff_pack(buf.data, buf.len).buffer;
            if (buf.data != raw_data)
                LOH_FREE(buf.data);
            buf = new_buf;
        }
        
        byte_push(&real_buf, do_diff);
        byte_push(&real_buf, do_lookback);
        byte_push(&real_buf, do_huff);
        byte_push(&real_buf, 0);
        bytes_push(&real_buf, buf.data, buf.len);
        
        if (buf.data != raw_data)
            LOH_FREE(buf.data);
        
        total_compressed_len += buf.len + 4;
        total_uncompressed_len += in_size;
    }
    uint64_t * chunk_table = (uint64_t *)&real_buf.data[chunk_table_loc];
    chunk_table[chunk_count * 2 + 0] = total_compressed_len;
    chunk_table[chunk_count * 2 + 1] = total_uncompressed_len;
    
    *out_len = real_buf.len;
    return real_buf.data;
}

/* decompression */

// On error, the value poitned to by the error parameter will be set to 1.
// Any partially-decompressed data is returned rather than being freed and nulled.
static loh_byte_buffer lookback_decompress(const uint8_t * input, size_t input_len, int * error)
{
    size_t i = 0;
    
    loh_byte_buffer ret = {0, 0, 0};
    
    if (input_len < 8)
    {
        ret.len = 1;
        ret.cap = 0;
        return ret;
    }
    
    uint64_t size = 0;
    size |= input[i++];
    size |= ((uint64_t)input[i++]) << 8;
    size |= ((uint64_t)input[i++]) << 16;
    size |= ((uint64_t)input[i++]) << 24;
    size |= ((uint64_t)input[i++]) << 32;
    size |= ((uint64_t)input[i++]) << 40;
    size |= ((uint64_t)input[i++]) << 48;
    size |= ((uint64_t)input[i++]) << 56;
    
    bytes_reserve(&ret, size);
    
    if (!ret.data)
    {
        *error = 1;
        return ret;
    }
    
#define _LOH_CHECK_I_VS_LEN_OR_RETURN(N) \
    if (i + N > input_len) return *error = 1, ret;

    while (i < input_len)
    {
        uint8_t dat = input[i++];
        
        uint8_t size_continues = dat & 1;
        size_t size = (dat >> 1) & loh_size_mask;
        
        uint8_t dist_continues = dat & (1 << (loh_size_bits + 1));
        size_t dist = (dat >> (loh_size_bits + 2)) & loh_dist_mask;
        
        uint64_t n = loh_size_bits;
        while (size_continues)
        {
            _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
            uint64_t cont_dat = input[i++];
            size_continues = cont_dat & 1;
            size += ((cont_dat >> 1) << n);
            size += ((uint64_t)1) << n;
            n += 7;
        }
        
        n = loh_dist_bits;
        while (dist_continues)
        {
            _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
            uint64_t cont_dat = input[i++];
            dist_continues = cont_dat & 1;
            dist += ((cont_dat >> 1) << n);
            dist += ((uint64_t)1) << n;
            n += 7;
        }
        
        // lookback mode
        if (dist > 0)
        {
            size += loh_min_lookback_length;
            // bounds limit
            if (dist > ret.len)
            {
                *error = 1;
                return ret;
            }
            
            // bytes_push uses memcpy, which doesn't work if there's any overlap.
            // overlap is allowed here, so we have to copy bytes manually.
            for(size_t j = 0; j < size; j++)
                byte_push(&ret, ret.data[ret.len - dist]);
        }
        // literal mode
        else
        {
            size += 1;
            
            _LOH_CHECK_I_VS_LEN_OR_RETURN(size)
            bytes_push(&ret, &input[i], size);
            i += size;
        }
    }
    
#undef _LOH_CHECK_I_VS_LEN_OR_RETURN
    
    return ret;
}

static loh_byte_buffer huff_unpack(loh_bit_buffer * buf, int * error)
{
    buf->bit_index = 0;
    buf->byte_index = 0;
    size_t output_len = bits_pop(buf, 8*8);
    
    loh_byte_buffer ret = {0, 0, 0};
    
    // if output length is 0, stop decoding
    if (output_len == 0)
    {
        bytes_reserve(&ret, output_len);
        return ret;
    }
    // reserve 8 extra bytes because we might write extra garbage bytes at the end
    // (we do this to avoid having to spend a branch jumping out of the inner decoding loop)
    bytes_reserve(&ret, output_len + 8);
    
    if (!ret.data)
    {
        puts("alloc failed");
        *error = 1;
        return ret;
    }
    
    size_t start_len = 0;
    while (start_len < output_len)
    {
        // the bit buffer is forcibly aligned at the start of each chunk
        if (buf->bit_index != 0)
        {
            buf->bit_index = 0;
            buf->byte_index += 1;
        }
        
        uint32_t chunk_len = bits_pop(buf, 8*4);
        
        // load huffman code description
        // starts at code length 1
        // bit 1: add 1 to code length
        // bit 0: read next 8 bits as symbol for next code. add 1 to code
        uint16_t symbol_count = bits_pop(buf, 8) + 1;
        uint16_t max_codes[16] = {0};
        uint8_t symbols[32768] = {0};
        uint16_t code_value = 0;
        size_t code_depth = 1;
        for (size_t i = 0; i < symbol_count; i++)
        {
            uint8_t bit = bit_pop(buf);
            while (bit)
            {
                code_value <<= 1;
                code_depth += 1;
                bit = bit_pop(buf);
                if (code_depth > 15)
                {
                    *error = 1;
                    return ret;
                }
            }
            uint8_t symbol = bits_pop(buf, 8);
            
            symbols[code_value] = symbol;
            max_codes[code_depth] = code_value + 1;
            code_value += 1;
        }
        max_codes[code_depth] = 0xFFFF;
        
        // the bit buffer is forcibly aligned to the start of the next byte at the end of the huffman tree data
        if (buf->bit_index != 0)
        {
            buf->bit_index = 0;
            buf->byte_index += 1;
        }
        
        size_t n = 0;
        size_t i = start_len;
        uint16_t code_word = 0;
        uint16_t * max_code = max_codes + 1;
        // consume all remaining input bytes
        size_t j = 0;
        for (j = buf->byte_index; j < buf->buffer.len; j++)
        {
            // operating on bit buffer input bytes/words is faster than operating on individual input bits
            uint8_t word = buf->buffer.data[j];
            for (uint8_t b = 0; b < 8; b += 1)
            {
                code_word = code_word | (word & 1);
                word >>= 1;
                if (code_word < *max_code++)
                {
                    ret.data[i++] = symbols[code_word];
                    n += 1;
                    code_word = 0;
                    max_code = max_codes + 1;
                }
                else
                    code_word <<= 1;
            }
            if (n >= chunk_len)
                break;
        }
        buf->byte_index = j + 1;
        buf->bit_index = 0;
        
        start_len += chunk_len;
    }
     
    ret.len = output_len;
    
#undef _LOH_PROCESS_WORD
    
    return ret;
}


// input data is modified, but not stored; it still belongs to the caller, and must be freed by the caller
// if huffman and lookback coding are both disabled, then the returned pointer is a pointer into the input data
// otherwise, returned data must be freed by the caller; it was allocated with LOH_MALLOC
static uint8_t * loh_decompress(uint8_t * data, size_t len, size_t * out_len, uint8_t check_checksum)
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
    
    for (size_t i = 0; i < chunk_count; i += 1)
    {
        uint8_t * chunk_start = &data[chunk_table[i * 2]];
        //uint8_t * chunk_end   = &data[chunk_table[i * 2 + 2]];
        size_t chunk_len = chunk_table[i * 2 + 2] - chunk_table[i * 2];
        //size_t output_len = chunk_table[i * 2 + 3] - chunk_table[i * 2 + 1];
        
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
                fprintf(stderr, "oops in rle");
                exit(-1);
            }
            if (error)
            {
                if (buf.data && buf.data != buf_orig)
                    LOH_FREE(buf.data);
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
                fprintf(stderr, "oops in lzss");
                exit(-1);
            }
            if (error)
            {
                if (buf.data && buf.data != buf_orig)
                    LOH_FREE(buf.data);
                return 0;
            }
        }
        if (do_diff)
        {
            for (size_t i = do_diff; i < buf.len; i += 1)
                buf.data[i] += buf.data[i - do_diff];
        }
        
        bytes_push(&out_buf, buf.data, buf.len);
    }
    
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

#endif // LOH_IMPL_HEADER
