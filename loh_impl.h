#ifndef LOH_IMPL_HEADER
#define LOH_IMPL_HEADER

/*
    LOH (LOokback + Huffman)
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

#ifndef LOH_LOW_MEMORY
#define LOH_HASH_SIZE (20)
#define LOH_PREVLINK_SIZE (20) // 1m
#else
#ifndef LOH_ULTRA_LOW_MEMORY
#define LOH_HASH_SIZE (18)
#define LOH_PREVLINK_SIZE (18) // ~256k
#else
#define LOH_HASH_SIZE (15)
#define LOH_PREVLINK_SIZE (15) // ~32k
#endif
#endif

// for finding lookback matches, we use a chained hash table with limited, location-based chaining
typedef struct {
    uint32_t * hashtable;
    uint32_t * prevlink;
    uint32_t max_distance;
    uint16_t chain_len;
} loh_hashmap;

#define loh_prevlink_mask ((1<<LOH_PREVLINK_SIZE) - 1)

#define LOH_HASH_LENGTH 4
static inline uint32_t hashmap_hash_raw(const void * bytes)
{
    // hashing function (can be anything; go ahead and optimize it as long as it doesn't result in tons of collisions)
    uint32_t temp = 0xA68BB0D5;
    // unaligned-safe 32-bit load
    uint32_t a = 0;
    memcpy(&a, bytes, 4);
    // then just multiply it by the const and return the top N bits
    return a * temp;
}
static inline uint32_t hashmap_hash(const void * bytes)
{
    return hashmap_hash_raw(bytes) >> (32 - LOH_HASH_SIZE);
}
static inline uint32_t loh_hashlink_index(uint64_t value)
{
    return value & loh_prevlink_mask;
}

// bytes must point to four characters
static inline void hashmap_insert(loh_hashmap * hashmap, const uint8_t * bytes, uint64_t value)
{
    const uint32_t key = hashmap_hash(bytes);
    hashmap->prevlink[loh_hashlink_index(value)] = hashmap->hashtable[key];
    hashmap->hashtable[key] = value;
}
/*
static inline uint64_t calc_lookback_overhead(uint64_t dist, uint64_t size, size_t pre_context, size_t last_real_size)
{
    uint64_t overhead = 1; // minimum cost of the start of this match
    
    if (last_real_size == pre_context)
        dist >>= 2;
    
    size_t n_max = 0x80;
    size_t n_byte_count = 1;
    while (dist >= n_max)
    {
        dist -= n_max;
        if (n_byte_count < 9)
            n_byte_count += 1;
        n_max = ((uint64_t)1) << (n_byte_count * 7);
    }
    
    overhead += n_byte_count;
    
    uint64_t size_min = (last_real_size == pre_context) ? 0x1F : 0xF;
    
    // cost of extra size bytes if size is long
    if (size - loh_min_lookback_length >= size_min)
        overhead += 1;
    if (size - loh_min_lookback_length >= 0x80 + size_min)
        overhead += 1;
    if (size - loh_min_lookback_length >= 0x4080 + size_min)
        overhead += 1;
    
    return overhead;
}
*/
static inline uint64_t calc_lookback_overhead(uint64_t dist, uint64_t lb_size, uint64_t size, size_t last_real_size)
{
    uint64_t overhead = 1; // minimum cost of the start of this match
    
    if (last_real_size == size)
        dist >>= 2;
    
    size_t n_max = 0x80;
    size_t n_byte_count = 1;
    while (dist >= n_max)
    {
        dist -= n_max;
        if (n_byte_count < 9)
            n_byte_count += 1;
        n_max = ((uint64_t)1) << (n_byte_count * 7);
    }
    
    overhead += n_byte_count;
    
    uint64_t size_min = (last_real_size == size) ? 0x1F : 0xF;
    
    // cost of extra lb_size bytes if lb_size is long
    if (lb_size - loh_min_lookback_length >= size_min)
        overhead += 1;
    if (lb_size - loh_min_lookback_length >= 0x80 + size_min)
        overhead += 1;
    if (lb_size - loh_min_lookback_length >= 0x4080 + size_min)
        overhead += 1;
    
    if (size >= 0x7)
        overhead += 1;
    if (size >= 0x87)
        overhead += 1;
    if (size >= 0x4087)
        overhead += 1;
    
    return overhead;
}

// bytes must point to four characters and be inside of buffer
static inline uint64_t hashmap_get(loh_hashmap * hashmap, size_t i, const uint8_t * input, const size_t buffer_len, const size_t pre_context, const size_t last_real_size, uint64_t * min_len, size_t * back_distance)
{
    const uint32_t key = hashmap_hash(&input[i]);
    uint64_t value = hashmap->hashtable[key];
    // file might be more than 4gb, so map in the upper bits of the current address
    if (sizeof(size_t) > sizeof(uint32_t))
        value |= i & 0xFFFFFFFF00000000;
    if (!value)
        return -1;
    
    // if we hit 128 bytes we call it good enough and take it
    const uint64_t good_enough_length = 48;
    // if we hit 128 bytes we call it good enough and take it
    const uint64_t really_good_enough_length = 128;
    uint64_t remaining = buffer_len - i;
    
    // look for best match under key
    uint64_t best = -1;
    uint64_t best_size = loh_min_lookback_length - 1;
    uint64_t best_d = 0;
    uint64_t first_value = value;
    uint16_t chain_len = hashmap->chain_len;
    double best_rate = 1.0;
    while (chain_len-- > 0)
    {
        if (i - value > hashmap->max_distance)
            break;
        if (memcmp(&input[i], &input[value], 4) == 0 && input[i + best_size] == input[value + best_size])
        {
            uint64_t size = 0;
            while (size < remaining && input[i + size] == input[value + size])
                size += 1;
            
            uint64_t total_covered = pre_context + size;
            
            size_t d = 1;
            while (value > 0 && input[i - d] == input[value - 1] && d <= pre_context)
            {
                value -= 1;
                size += 1;
                d += 1;
            }
            d -= 1;
            
            uint64_t literals = pre_context - d;
            uint64_t encoded_cost = calc_lookback_overhead(i - value, size, literals, last_real_size);
            
            double rate = (double)(encoded_cost + literals) / (double)(total_covered);
            //double rate = (double)(encoded_cost + literals) - (double)(total_covered);
            
            //double comp_ratio = (double)(calc_lookback_overhead(i - value, size, pre_context - d, last_real_size) + 500) / (double)(size + 500);
            
            if (rate < best_rate)
            {
                best_rate = rate;
                best_size = size;
                best = value;
                best_d = d;
                
                if (size >= remaining || size >= really_good_enough_length)
                    break;
            }
            // get out faster if we're being expensive
            if (size >= good_enough_length || size >= remaining)
                chain_len >>= 1;
        }
        value = hashmap->prevlink[loh_hashlink_index(value)];
        if (sizeof(size_t) > sizeof(uint32_t))
            value |= i & 0xFFFFFFFF00000000;
        
        if (value == 0 || value > i || value == first_value)
            break;
        const uint32_t key_2 = hashmap_hash(&input[value]);
        if (key_2 != key)
            break;
    }
    
    if (best_size != loh_min_lookback_length - 1)
        *min_len = best_size;
    *back_distance = best_d;
    return best;
}

static loh_byte_buffer lookback_compress(const uint8_t * input, uint64_t input_len, int8_t quality_level);

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
    uint8_t part_a = (*(huff_node_t**)a)->symbol;
    uint8_t part_b = (*(huff_node_t**)b)->symbol;
    if (part_a < part_b)
        return -1;
    else if (part_a > part_b)
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
    // The chunk size is arbitrary, but for the sake of simplicity, this encoder uses a fixed 32k chunk size.
    // Each chunk is prefixed with a byte-aligned 32-bit integer giving the number of output tokens in the chunk.
    
    uint64_t chunk_size = (1 << 15);
    uint64_t chunk_count = (len + chunk_size - 1) / chunk_size;
    
    //uint64_t header_overhead_bytes = 0;
    
    //size_t diff_counts[256] = {0};
    
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
        uint8_t incompressible = canon_len == 8 && symbol_count == 256;
        
        /*
        // Our canonical length-limited huffman code is finally done!
        // To print it out (with modified frequencies):
        uint8_t _prev_symbol = 0;
        for (size_t c = 0; c < symbol_count; c += 1)
        {
            uint8_t d = unordered_dict[c]->symbol - _prev_symbol;
            diff_counts[d] += 1;
            _prev_symbol = unordered_dict[c]->symbol;
            
            printf("%02X: ", unordered_dict[c]->symbol);
            for (size_t i = 0; i < unordered_dict[c]->code_len; i++)
                printf("%c", ((unordered_dict[c]->code >> i) & 1) ? '1' : '0');
            printf("\t %lld", unordered_dict[c]->freq);
            puts("");
        }
        */
        
        // Now we actually compress the input data.
        
        //size_t start_byte = ret.buffer.len;
        
        bit_push(&ret, incompressible);
        
        if (!incompressible)
        {
            // push huffman code description
            // start at code length 1
            // bit 1: add 1 to code length
            // bit 0: read next 8 bits as symbol for next code. add 1 to code
            uint8_t prev_symbol = 0;
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
                    uint8_t diff = unordered_dict[i]->symbol - prev_symbol;
                    
                    // stored as diffs
                    // 0 : 1
                    // 10 : 2
                    // 110 : 3
                    // 1110 : 4
                    // 1111xxxxxxxx : other
                    if (diff >= 1 && diff <= 4)
                    {
                        bits_push(&ret, 0xFF, diff - 1);
                        bit_push(&ret, 0);
                    }
                    else
                    {
                        bits_push(&ret, 0xFF, 4);
                        bits_push(&ret, diff, 8);
                    }
                    prev_symbol = unordered_dict[i]->symbol;
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
        else
        {
            // the bit buffer is forcibly aligned to the start of the next byte before incompressible data
            if (ret.bit_index != 0)
                ret.bit_index = 8;
            
            for (size_t i = 0; i < len; i++)
                bits_push(&ret, data[i], 8);
        }
    }
    
    /*
    for (size_t s = 0; s < 256; s += 1)
    {
        printf("%02X : %d\n", s, diff_counts[s]);
    }
    */
    
    //printf("huff table overhead: %lld\n", header_overhead_bytes);
    return ret;
}

// passed-in data is modified, but not stored; it still belongs to the caller, and must be freed by the caller
// returned data must be freed by the caller; it was allocated with LOH_MALLOC
static uint8_t * loh_compress(uint8_t * data, size_t len, int8_t do_lookback, uint8_t do_huff, uint8_t do_diff, size_t * out_len)
{
    if (!data || !out_len) return 0;
    
    if (do_lookback > 12)
        do_lookback = 12;
    if (do_lookback < -12)
        do_lookback = -12;
    
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
            int16_t diff = (int16_t)data[in_start + a] - (int16_t)data[in_start + b];
            diff = diff < 0 ? -diff : diff;
            difference += diff;
            seen_values[data[in_start + a]] = 1;
            seen_values[data[in_start + b]] = 1;
        }
        // to prevent differentiating files that only have a small number of unique values (doing so thrashes the entropy coder)
        uint16_t num_seen_values = 0;
        for (size_t n = 0; n < 256; n++)
            num_seen_values += seen_values[n];
        difference /= 4096;
        
        int64_t orig_difference = difference;
        
        uint8_t did_diff = do_diff;
        
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
                    int16_t diff = (int16_t)data[in_start + a] - (int16_t)data[in_start + a + diff_opt];
                    diff = diff < 0 ? -diff : diff;
                    diff_difference += diff;
                }
                diff_difference /= 4096;
                // 2x to prevent noise from triggering differentiation when it's not necessary
                if (diff_difference * 2 < orig_difference && diff_difference < difference)
                {
                    difference = diff_difference;
                    did_diff = diff_opt;
                }
            }
        }
        
        if (did_diff)
        {
            for (size_t i = buf.len - 1; i >= did_diff; i -= 1)
                buf.data[i] -= buf.data[i - did_diff];
        }
        
        loh_byte_buffer orig_buf = buf;
        
        size_t lb_comp_ratio_100 = 100;
        
        uint8_t did_lookback = do_lookback;
        
        if (do_lookback)
        {
            loh_byte_buffer new_buf = lookback_compress(buf.data, buf.len, do_lookback);
            if (new_buf.len < buf.len)
            {
                lb_comp_ratio_100 = new_buf.len * 100 / buf.len;
                if (buf.data != raw_data)
                    LOH_FREE(buf.data);
                buf = new_buf;
            }
            else
            {
                LOH_FREE(new_buf.data);
                did_lookback = 0;
            }
        }
        uint8_t did_huff = 0;
        if (do_huff)
        {
            loh_byte_buffer new_buf = huff_pack(buf.data, buf.len).buffer;
            if (new_buf.len < buf.len)
            {
                if (buf.data != raw_data)
                    LOH_FREE(buf.data);
                buf = new_buf;
                did_huff = 1;
                
                // if we did lookback but it's tenuous, try huff-compressing the original data too to see if it comes out smaller
                
                if (did_lookback && (lb_comp_ratio_100 > 80 || (did_diff != 0 && lb_comp_ratio_100 > 30)))
                {
                    loh_byte_buffer new_buf_2 = huff_pack(orig_buf.data, orig_buf.len).buffer;
                    
                    if (new_buf_2.len < buf.len)
                    {
                        buf = new_buf_2;
                        did_lookback = 0;
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
        
        byte_push(&real_buf, did_diff);
        byte_push(&real_buf, did_lookback);
        byte_push(&real_buf, did_huff);
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

static void var_len_push(loh_byte_buffer * buf, uint64_t n)
{
    if (n == 0)
        byte_push(buf, 0);
    else
    {
        size_t n_max = 0x80;
        size_t n_byte_count = 1;
        while (n >= n_max)
        {
            n -= n_max;
            if (n_byte_count < 9)
                n_byte_count += 1;
            n_max = ((uint64_t)1) << (n_byte_count * 7);
        }
        
        for (size_t i = 0; i < n_byte_count; i++)
        {
            byte_push(buf, ((n & 0x7F) << 1) | (i + 1 < n_byte_count));
            n >>= 7;
        }
    }
}
static loh_byte_buffer lookback_compress(const uint8_t * input, uint64_t input_len, int8_t quality_level)
{
    loh_hashmap hashmap;
    hashmap.hashtable = (uint32_t *)LOH_MALLOC(sizeof(uint32_t *) * (1 << LOH_HASH_SIZE));
    hashmap.prevlink = (uint32_t *)LOH_MALLOC(sizeof(uint32_t *) * (1 << LOH_PREVLINK_SIZE));
    memset(hashmap.hashtable, 0, sizeof(uint32_t *) * (1 << LOH_HASH_SIZE));
    memset(hashmap.prevlink, 0, sizeof(uint32_t *) * (1 << LOH_PREVLINK_SIZE));
    
    int8_t q = quality_level > 0 ? quality_level : 0;
    hashmap.chain_len = (1 << q);
    hashmap.max_distance = (1 << (quality_level + 11 + (quality_level < 0))) - 1;
    
    if (hashmap.chain_len == 0)
        hashmap.chain_len = 1;
    if (hashmap.max_distance == 0)
        hashmap.max_distance = 1;

    loh_byte_buffer ret = {0, 0, 0};
    
    byte_push(&ret, input_len & 0xFF);
    byte_push(&ret, (input_len >> 8) & 0xFF);
    byte_push(&ret, (input_len >> 16) & 0xFF);
    byte_push(&ret, (input_len >> 24) & 0xFF);
    byte_push(&ret, (input_len >> 32) & 0xFF);
    byte_push(&ret, (input_len >> 40) & 0xFF);
    byte_push(&ret, (input_len >> 48) & 0xFF);
    byte_push(&ret, (input_len >> 56) & 0xFF);
    
    byte_push(&ret, hashmap.max_distance & 0xFF);
    byte_push(&ret, (hashmap.max_distance >> 8) & 0xFF);
    byte_push(&ret, (hashmap.max_distance >> 16) & 0xFF);
    byte_push(&ret, (hashmap.max_distance >> 24) & 0xFF);
    
    uint64_t i = 0;
    uint64_t last_real_size = -1;
    while (i < input_len)
    {
        uint64_t size = 0; // size of literal
        uint64_t lb_size = 0; // size of lookback
        uint64_t lb_loc = -1; // location of lookback
        while (i + size < input_len)
        {
            size_t back_distance = 0;
            if (i + size + LOH_HASH_LENGTH < input_len)
                lb_loc = hashmap_get(&hashmap, i + size, input, input_len, size, last_real_size, &lb_size, &back_distance);
            if (lb_size != 0)
            {
                // zlib-style "lazy" search: only confirm the match if the next byte isn't a good match too
                if (lb_size < 64 && i + size + 1 + LOH_HASH_LENGTH < input_len)
                {
                    uint64_t lb_size_2 = 0;
                    size_t back_distance_2 = 0;
                    uint64_t lb_loc_2 = hashmap_get(&hashmap, i + size + 1, input, input_len, size + 1, last_real_size, &lb_size_2, &back_distance_2);
                    if (lb_size_2 >= lb_size + 1)
                    {
                        size += 1;
                        lb_loc = lb_loc_2;
                        lb_size = lb_size_2;
                        back_distance = back_distance_2;
                    }
                }
                if (lb_size != 0)
                {
                    size -= back_distance;
                    break;
                }
            }
            // need to update the hashmap mid-literal
            if (i + size + LOH_HASH_LENGTH < input_len)
                hashmap_insert(&hashmap, &input[i + size], i + size);
            size += 1;
        }
        
        if (size > input_len - i)
            size = input_len - i;
        
        
        size_t real_size = size;
        size_t real_lb_size = lb_size;
        uint64_t dist = i + real_size - lb_loc;
        
        if (lb_loc == (uint64_t)-1)
            dist = 0;
        else
            lb_size -= loh_min_lookback_length;
        
        //printf("%lld %lld %lld\n", real_size, real_lb_size, dist);
        
        if (real_size != last_real_size)
        {
            byte_push(&ret, ((size >= 0x7 ? 0x7 : size) << 5) | ((lb_size >= 0xF ? 0xF : lb_size) << 1) | 0);
            if (size >= 0x7)
            {
                size -= 0x7;
                var_len_push(&ret, size);
            }
            if (lb_size >= 0xF)
            {
                lb_size -= 0xF;
                var_len_push(&ret, lb_size);
            }
        }
        else
        {
            byte_push(&ret, ((dist & 3) << 6) | ((lb_size >= 0x1F ? 0x1F : lb_size) << 1) | 1);
            dist >>= 2;
            if (lb_size >= 0x1F)
            {
                lb_size -= 0x1F;
                var_len_push(&ret, lb_size);
            }
        }
        
        var_len_push(&ret, dist);
        
        last_real_size = real_size;
        
        // write literal and advance cursor
        if (real_size != 0)
        {
            bytes_push(&ret, &input[i], real_size);
            i += real_size;
        }
        
        // advance cursor and update hashmap for lookback
        if (real_lb_size != 0)
        {
            uint64_t start_i = i;
            i += 1;
            for (size_t j = 1; j < real_lb_size; j++)
            {
                if (i + LOH_HASH_LENGTH < input_len)
                    hashmap_insert(&hashmap, &input[i], i);
                i += 1;
            }
            if (start_i + LOH_HASH_LENGTH < input_len)
                hashmap_insert(&hashmap, &input[start_i], start_i);
        }
    }
    LOH_FREE(hashmap.hashtable);
    LOH_FREE(hashmap.prevlink);
    
    return ret;
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
    
    // not used by the reference decoder, but can be used by streaming decoders
    uint64_t max_lookback = 0;
    max_lookback |= input[i++];
    max_lookback |= ((uint64_t)input[i++]) << 8;
    max_lookback |= ((uint64_t)input[i++]) << 16;
    max_lookback |= ((uint64_t)input[i++]) << 24;
    
    bytes_reserve(&ret, size);
    
    if (!ret.data)
    {
        *error = 1;
        return ret;
    }
    
#define _LOH_CHECK_I_VS_LEN_OR_RETURN(N) \
    if (i + N > input_len) return *error = 1, ret;

    size_t last_size = 0;
    while (i < input_len)
    {
        //size_t start_i = i;
        //printf("%lld\n", i);
        size_t size = last_size;
        size_t lb_size = 0;
        size_t dist = 0;
        uint64_t n = 0;
        
        uint8_t dat = input[i++];
        
        // same literal size as previous
        if (dat & 1)
        {
            lb_size = (dat >> 1) & 0x1F;
            uint8_t lb_size_continues = lb_size == 0x1F;
            n = 0;
            while (lb_size_continues)
            {
                //puts("lb size continues...");
                _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
                uint64_t cont_dat = input[i++];
                lb_size_continues = cont_dat & 1;
                lb_size += ((cont_dat >> 1) << n);
                if (n > 0) lb_size += ((uint64_t)1) << n;
                n += 7;
            }
        }
        // new literal size
        else
        {
            size = dat >> 5;
            uint8_t size_continues = size == 0x7;
            n = 0;
            while (size_continues)
            {
                //puts("size continues...");
                _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
                uint64_t cont_dat = input[i++];
                size_continues = cont_dat & 1;
                size += ((cont_dat >> 1) << n);
                if (n > 0) size += ((uint64_t)1) << n;
                n += 7;
            }
            
            lb_size = (dat >> 1) & 0xF;
            uint8_t lb_size_continues = lb_size == 0xF;
            n = 0;
            while (lb_size_continues)
            {
                //puts("lb size continues...");
                _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
                uint64_t cont_dat = input[i++];
                lb_size_continues = cont_dat & 1;
                lb_size += ((cont_dat >> 1) << n);
                if (n > 0) lb_size += ((uint64_t)1) << n;
                n += 7;
            }
        }
        last_size = size;
        
        _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
        uint64_t dist_dat = input[i++];
        dist = dist_dat >> 1;
        uint8_t dist_continues = dist_dat & 1;
        n = 7;
        while (dist_continues)
        {
            //puts("dist continues...");
            _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
            uint64_t cont_dat = input[i++];
            dist_continues = cont_dat & 1;
            dist += ((cont_dat >> 1) << n);
            dist += ((uint64_t)1) << n;
            n += 7;
        }
        
        //printf("%lld %lld %lld\n", size, lb_size, dist);
        
        // fix dist in same-literal-size mode
        if (dat & 1)
        {
            dist <<= 2;
            dist |= dat >> 6;
        }
        
        //printf("%lld\t%lld\t%lld\t%lld\n", size, lb_size + loh_min_lookback_length, dist, i);
        
        // literal mode
        if (size > 0)
        {
            _LOH_CHECK_I_VS_LEN_OR_RETURN(size)
            bytes_push(&ret, &input[i], size);
            i += size;
        }
        // lookback mode
        if (dist > 0)
        {
            lb_size += loh_min_lookback_length;
            // bounds limit
            if (dist > ret.len)
            {
                //printf("bounds limit... %lld %lld %llX %llX\n", dist, ret.len, start_i + (size_t)(input - aaaaaa), i + (size_t)(input - aaaaaa));
                *error = 1;
                return ret;
            }
            
            // bytes_push uses memcpy, which doesn't work if there's any overlap.
            // overlap is allowed here, so we have to copy bytes manually.
            for(size_t j = 0; j < lb_size; j++)
                byte_push(&ret, ret.data[ret.len - dist]);
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
        
        uint8_t incompressible = bit_pop(buf);
        
        if (!incompressible)
        {
            // load huffman code description
            // starts at code length 1
            // bit 1: add 1 to code length
            // bit 0: read next 8 bits as symbol for next code. add 1 to code
            uint16_t symbol_count = bits_pop(buf, 8) + 1;
            uint16_t max_codes[16] = {0};
            uint8_t symbols[32768] = {0};
            uint16_t code_value = 0;
            size_t code_depth = 1;
            uint8_t prev_symbol = 0;
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
                
                // stored as diffs
                // 0 : 1
                // 10 : 2
                // 110 : 3
                // 1110 : 4
                // 1111xxxxxxxx : other
                uint8_t diff = 1 + bit_pop(buf);
                diff += diff == 2 && bit_pop(buf);
                diff += diff == 3 && bit_pop(buf);
                diff += diff == 4 && bit_pop(buf);
                if (diff == 5)
                    diff = bits_pop(buf, 8);
                
                uint8_t symbol = prev_symbol + diff;
                prev_symbol += diff;
                
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
        }
        else
        {
            // the bit buffer is forcibly aligned to the start of the next byte for incompressible data
            if (buf->bit_index != 0)
            {
                buf->bit_index = 0;
                buf->byte_index += 1;
            }
            
            for (size_t i = start_len; i < start_len + chunk_len; i += 1)
                ret.data[i] = buf->buffer.data[buf->byte_index + (i - start_len)];
            
            buf->byte_index += chunk_len;
            buf->bit_index = 0;
        }
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
