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

#ifndef LOH_REALLOC
#define LOH_REALLOC realloc
#endif

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

static const size_t min_lookback_length = 1;

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
    uint8_t hash_max;
    uint8_t hash_mask;
} loh_hashmap;

#define LOH_HASH_LENGTH 4
static inline uint32_t hashmap_hash(loh_hashmap * hashmap, const uint8_t * bytes)
{
    // hashing function (can be anything; go ahead and optimize it as long as it doesn't result in tons of collisions)
    uint32_t temp = 0xA68BF1D7;
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
    hashmap->hashtable_i[key_i] = (hashmap->hashtable_i[key_i] + 1) & hashmap->hash_mask;
}

// bytes must point to four characters and be inside of buffer
static inline uint64_t hashmap_get(loh_hashmap * hashmap, const uint8_t * bytes, const uint8_t * buffer, const size_t buffer_len, const uint8_t final, uint64_t * min_len)
{
    const uint32_t key_i = hashmap_hash(hashmap, bytes);
    const uint32_t key = key_i << hashmap->hash_shl;
    
    // look for match within key
    size_t i = (size_t)(bytes - buffer);
    uint64_t best = -1;
    uint64_t best_size = min_lookback_length - 1;
    for (uint8_t j = 0; j < hashmap->hash_max; j++)
    {
        // cycle from newest to oldest
        int n = (hashmap->hashtable_i[key_i] + hashmap->hash_max - 1 - j) & hashmap->hash_mask;
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
        const uint64_t chunk_size = 16;
        uint64_t remaining = buffer_len - i;
        if (remaining > good_enough_length)
            remaining = good_enough_length;
        
        uint64_t size = 0;
        while (size + chunk_size < remaining)
        {
            if (memcmp(&bytes[size], &buffer[value + size], chunk_size) != 0)
                break;
            size += chunk_size;
        }
        while (size < remaining && bytes[size] == buffer[value + size])
            size += 1;
        
        if (size > best_size || (size == best_size && value > best))
        {
            best_size = size;
            best = value;
            
            if (best_size >= good_enough_length)
                break;
            if (!final && best_size >= 8)
                break;
        }
    }
    
    *min_len = best_size;
    return best;
}

static inline uint64_t hashmap_get_if_efficient(loh_hashmap * hashmap, const uint64_t i, const uint8_t * input, const uint64_t input_len, const uint8_t final, uint64_t * out_size)
{
    // here we only return the hashmap hit if it would be efficient to code it
    
    uint64_t remaining = input_len - i;
    if (i >= input_len || remaining <= min_lookback_length)
        return -1;
    
    if (remaining > 0x8000)
        remaining = 0x8000;
    
    uint64_t size = 0;
    const uint64_t found_loc = hashmap_get(hashmap, &input[i], input, input_len, final, &size);
    const uint64_t dist = i - found_loc;
    if (found_loc != (uint64_t)-1 && found_loc < i && dist <= 0x3FFFFFFFF)
    {
        // find true length of match match
        // (this is significantly faster than testing byte-by-byte)
        if (final)
        {
            const uint64_t chunk_size = 16;
            while (size + chunk_size < remaining)
            {
                if (memcmp(&input[i + size], &input[found_loc + size], chunk_size) == 0)
                    size += chunk_size;
                else
                    break;
            }
            while (size < remaining && input[i + size] == input[found_loc + size])
                size += 1;
        }
        
        if (size > 0x7FFF)
            size = 0x7FFF;
        
        uint64_t overhead =
            dist <= 0x3F ? 1 :
            (dist <= 0x1FFF ? 2 :
            (dist <= 0xFFFFF ? 3 :
            (dist <= 0x7FFFFF ? 4 : 5)));
        overhead += 1; // size byte for size <= 0x7F
        // don't need to account for the size > 0x7F case because overhead will always be smaller than size then
        
        if (overhead < size)
        {
            *out_size = size;
            return found_loc;
        }
    }
    return -1;
}

static loh_byte_buffer lookback_compress(const uint8_t * input, uint64_t input_len, uint8_t quality_level)
{
    if (quality_level > 9)
        quality_level = 9;
    uint8_t hash_size = 13 + (quality_level + 1) / 2;
    uint8_t hash_shl = quality_level / 2;
    size_t hash_capacity = 1 << (hash_size + hash_shl);
    size_t hash_i_capacity = 1 << hash_size;
    
    loh_byte_buffer ret = {0, 0, 0};
    
    loh_hashmap hashmap;
    
    hashmap.hashtable = malloc(sizeof(uint64_t) * hash_capacity);
    if (!hashmap.hashtable)
        return ret;
    
    hashmap.hashtable_i = malloc(sizeof(uint8_t) * hash_i_capacity);
    if (!hashmap.hashtable_i)
        return ret;
    
    hashmap.hash_size = hash_size;
    hashmap.hash_shl = hash_shl;
    hashmap.hash_max = (1 << hash_shl);
    hashmap.hash_mask = (1 << hash_shl) - 1;
    
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
    while (i < input_len)
    {
        uint64_t remaining = input_len - i;
        
        // check for lookback hit
        if (remaining > min_lookback_length)
        {
            uint64_t size = 0;
            uint64_t found_loc = hashmap_get_if_efficient(&hashmap, i, input, input_len, 1, &size);
            if (found_loc != (uint64_t)-1)
            {
                l += 1;
                
                uint64_t dist = i - found_loc;
                
                if (dist > i)
                {
                    fprintf(stderr, "LOH internal error: broken lookback distance calculation\n");
                    exit(-1);
                }
                
                // push lookback distance
                if (dist <= 0x3F)
                    byte_push(&ret, 1 | (dist << 2));
                else if (dist <= 0x1FFF)
                {
                    byte_push(&ret, 3 | (dist << 3));
                    byte_push(&ret, dist >> 5);
                }
                else if (dist <= 0xFFFFF)
                {
                    byte_push(&ret, 7 | (dist << 4));
                    byte_push(&ret, dist >> 4);
                    byte_push(&ret, dist >> 12);
                }
                else if (dist <= 0x7FFFFF)
                {
                    byte_push(&ret, 0xF | (dist << 5));
                    byte_push(&ret, dist >> 3);
                    byte_push(&ret, dist >> 11);
                    byte_push(&ret, dist >> 19);
                }
                else if (dist <= 0x3FFFFFFFF)
                {
                    byte_push(&ret, 0x1F | (dist << 6));
                    byte_push(&ret, dist >> 2);
                    byte_push(&ret, dist >> 10);
                    byte_push(&ret, dist >> 18);
                    byte_push(&ret, dist >> 26);
                }
                else
                {
                    fprintf(stderr, "LOH internal error: bad lookback match (goes too far)");
                    exit(-1);
                }
                
                // push size
                if (size > 0x7F)
                {
                    byte_push(&ret, 1 | (size << 1));
                    byte_push(&ret, size >> 7);
                }
                else
                    byte_push(&ret, size << 1);
                
                // advance cursor and update hashmap
                for (size_t j = 0; j < size; ++j)
                {
                    if (i + LOH_HASH_LENGTH < input_len)
                        hashmap_insert(&hashmap, &input[i], i);
                    i += 1;
                }
                continue;
            }
        }
        
        // store a literal if we found no lookback
        uint16_t size = 0;
        while (size + 1 < (1 << 14) && i + size < input_len)
        {
            uint64_t _size_unused;
            uint64_t found_loc = hashmap_get_if_efficient(&hashmap, i + size, input, input_len, 0, &_size_unused);
            if (found_loc != (uint64_t)-1)
                break;
            // need to update the hashmap mid-literal
            if (i + size + LOH_HASH_LENGTH < input_len)
                hashmap_insert(&hashmap, &input[i + size], i + size);
            size += 1;
        }
        
        if (size > input_len - i)
            size = input_len - i;
        
        // we store short and long literals slightly differently, with a bit flag to differentiate
        if (size > 0x3F)
        {
            byte_push(&ret, (size << 2) | 2);
            byte_push(&ret, size >> 6);
        }
        else
            byte_push(&ret, (size << 2));
        
        bytes_push(&ret, &input[i], size);
        i += size;
    }
    
    return ret;
}

typedef struct _huff_node {
    struct _huff_node * children[2];
    int64_t freq;
    // A huffman code for a symbol from a string of length N will never exceed log2(N) in length.
    // (e.g. for a 65kbyte file, it's impossible for there to both be enough unique symbols AND
    //  an unbalanced-enough symbol distribution that the huffman tree is more than 16 levels deep)
    // For a token to require 16 bits to code, it needs to occur have a freq around 1/65k,
    //  which can only happen if the surrounding data is at least 65k long! and log2(65k) == 16.
    // (I haven't proven this to myself, but if it's wrong, it's only wrong by 1 bit.)
    // So, storing codes from a 64-bit-address-space file in a 64-bit number is fine.
    // (Or, if I'm wrong, from a 63-bit-address-space file.)
    uint64_t code;
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
    
    // we want to generate a code with a maximum of 15 bits...
    // ... which means that the minimum frequency must be at least 1/32768 of the total count
    if (symbol_count > 0)
    {
        // use ceiled division to make super extra sure that we don't go over 1/32768
        uint64_t min_ok_count = (total_count + 32767) / 32768;
        while ((counts[symbol_count-1] >> 8) < min_ok_count)
        {
            for (int i = symbol_count-1; i >= 0; i -= 1)
            {
                // We use an x = max(minimum, x) approach instead of just adding to every count, because
                //  if we never add to the most frequent item's frequency, we will definitely converge.
                // (Specifically, this is guaranteed to converge if there are 32768 or less symbols in
                //  the dictionary, which there are. There are only 256 at most.)
                // More proof of convergence: We will eventually add less than 32768 to "total_count"
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
            min_ok_count = (total_count + 32767) / 32768;
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
    
    // set up buffers and start pushing data to them
    loh_bit_buffer ret;
    memset(&ret, 0, sizeof(loh_bit_buffer));
    
    bits_push(&ret, len, 8*8);
    
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
    
    return ret;
}

// passed-in data is modified, but not stored; it still belongs to the caller, and must be freed by the caller
// returned data must be freed by the caller; it was allocated with LOH_MALLOC
static uint8_t * loh_compress(uint8_t * data, size_t len, uint8_t do_lookback, uint8_t do_huff, uint8_t do_diff, size_t * out_len)
{
    if (!data || !out_len) return 0;
    
    uint32_t checksum = loh_checksum(data, len);
    
    loh_byte_buffer buf = {data, len, len};
    
    if (do_diff)
    {
        for (size_t i = buf.len - 1; i >= do_diff; i -= 1)
            buf.data[i] -= buf.data[i - do_diff];
    }
    if (do_lookback)
    {
        loh_byte_buffer new_buf = lookback_compress(buf.data, buf.len, do_lookback);
        if (buf.data != data)
            LOH_FREE(buf.data);
        buf = new_buf;
    }
    if (do_huff)
    {
        loh_byte_buffer new_buf = huff_pack(buf.data, buf.len).buffer;
        if (buf.data != data)
            LOH_FREE(buf.data);
        buf = new_buf;
    }
    
    loh_byte_buffer real_buf = {0, 0, 0};
    bytes_reserve(&real_buf, buf.len + 8 + 4);
    
    bytes_push(&real_buf, (const uint8_t *)"LOHz", 5);
    byte_push(&real_buf, do_diff);
    byte_push(&real_buf, do_lookback);
    byte_push(&real_buf, do_huff);
    bytes_push(&real_buf, (uint8_t *)&checksum, 4);
    bytes_push(&real_buf, buf.data, buf.len);
    
    if (buf.data != data)
        LOH_FREE(buf.data);
    
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
        
        // lookback mode
        if (dat & 1)
        {
            uint64_t loc = 0;
            if (!(dat & 2))
                loc = dat >> 2;
            else if (!(dat & 4))
            {
                loc = dat >> 3;
                _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
                loc |= ((uint64_t)input[i++]) << 5;
            }
            else if (!(dat & 8))
            {
                loc = dat >> 4;
                _LOH_CHECK_I_VS_LEN_OR_RETURN(2)
                loc |= ((uint64_t)input[i++]) << 4;
                loc |= ((uint64_t)input[i++]) << 12;
            }
            else if (!(dat & 0x10))
            {
                loc = dat >> 5;
                _LOH_CHECK_I_VS_LEN_OR_RETURN(3)
                loc |= ((uint64_t)input[i++]) << 3;
                loc |= ((uint64_t)input[i++]) << 11;
                loc |= ((uint64_t)input[i++]) << 19;
            }
            else if (!(dat & 0x20))
            {
                loc = dat >> 6;
                _LOH_CHECK_I_VS_LEN_OR_RETURN(4)
                loc |= ((uint64_t)input[i++]) << 2;
                loc |= ((uint64_t)input[i++]) << 10;
                loc |= ((uint64_t)input[i++]) << 18;
                loc |= ((uint64_t)input[i++]) << 26;
            }
            
            // bounds limit
            if (loc > ret.len)
            {
                *error = 1;
                return ret;
            }
            
            _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
            uint8_t size_dat = input[i++];
            
            uint16_t size = size_dat >> 1;
            if (size_dat & 1)
            {
                _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
                size |= ((uint16_t)input[i++]) << 7;
            }
            
            // bytes_push uses memcpy, which doesn't work if there's any overlap.
            // overlap is allowed here, so we have to copy bytes manually.
            for(size_t j = 0; j < size; j++)
                byte_push(&ret, ret.data[ret.len - loc]);
        }
        // literal mode
        else
        {
            uint16_t size = dat >> 2;
            // this bit is true if the literal is long and has an extra length byte
            if (dat & 2)
            {
                _LOH_CHECK_I_VS_LEN_OR_RETURN(1)
                size |= ((uint16_t)input[i++]) << 6;
            }
            
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
    
    // reserve 8 extra bytes because we might write extra garbage bytes at the end
    // (we do this to avoid having to spend a branch jumping out of the inner decoding loop)
    bytes_reserve(&ret, output_len + 8);
    
    if (!ret.data)
    {
        *error = 1;
        return ret;
    }
    
    size_t i = 0;
    uint16_t code_word = 0;
    uint16_t * max_code = max_codes + 1;
    // consume all remaining input bytes
    for (size_t j = buf->byte_index; j < buf->buffer.len; j++)
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
                code_word = 0;
                max_code = max_codes + 1;
            }
            else
                code_word <<= 1;
        }
        if (i >= output_len)
            break;
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
    
    loh_byte_buffer buf = {data, len, len};
    
    if (buf.len < 8 || memcmp(buf.data, "LOHz", 5) != 0)
        return 0;
    
    uint8_t do_diff = buf.data[5];
    uint8_t do_lookback = buf.data[6];
    uint8_t do_huff = buf.data[7];
    
    uint32_t stored_checksum = buf.data[8]
        | (((uint32_t)buf.data[9]) << 8)
        | (((uint32_t)buf.data[10]) << 16)
        | (((uint32_t)buf.data[11]) << 24);
    
    const uint16_t header_size = 12;
    
    buf.data += header_size;
    buf.len -= header_size;
    
    if (do_huff)
    {
        loh_bit_buffer compressed;
        memset(&compressed, 0, sizeof(loh_bit_buffer));
        compressed.buffer = buf;
        int error = 0;
        loh_byte_buffer new_buf = huff_unpack(&compressed, &error);
        if (buf.data != data + header_size)
            LOH_FREE(buf.data);
        buf = new_buf;
        if (error)
        {
            if (buf.data && buf.data != data + header_size)
                LOH_FREE(buf.data);
            return 0;
        }
    }
    if (do_lookback)
    {
        int error = 0;
        loh_byte_buffer new_buf = lookback_decompress(buf.data, buf.len, &error);
        if (buf.data != data + header_size)
            LOH_FREE(buf.data);
        buf = new_buf;
        if (error)
        {
            if (buf.data && buf.data != data + header_size)
                LOH_FREE(buf.data);
            return 0;
        }
    }
    if (do_diff)
    {
        for (size_t i = do_diff; i < buf.len; i += 1)
            buf.data[i] += buf.data[i - do_diff];
    }
    
    uint32_t checksum;
    if (stored_checksum != 0 && check_checksum)
        checksum = loh_checksum(buf.data, buf.len);
    else
        checksum = stored_checksum;
    
    if (checksum == stored_checksum || !check_checksum)
    {
        *out_len = buf.len;
        return buf.data;
    }
    else
    {
        if (buf.data && buf.data != data + header_size)
            LOH_FREE(buf.data);
        return 0;
    }
}

#endif // LOH_IMPL_HEADER
