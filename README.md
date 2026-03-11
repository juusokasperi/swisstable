# SwissTable - Hash table implementation in C

A fast, memory-efficient hash table implementation using the Swiss table algorithm with SIMD acceleration.

## Features

- **Generic**: Works with any key/value type using `void*` and macros
- **Fast**: ~22ns lookups with SIMD acceleration (NEON/SSE2)
- **Memory-agnostic**: Works with malloc, custom arenas, or any allocator
- **High load factor**: 87.5% capacity utilization before resize
- **String support**: Built-in optimized string key handling
- **Type-safe macros**: Compile-time type checking via macros

## Quick Start

### Basic Usage (Integer Keys)
```c
#define SWISSTABLE_IMPLEMENTATION
#include "swisstable.h"

int main(void) {
    // Create a hash table: int -> int
    SwissTable map = st_init_malloc(int, int);
    
    // Insert
    st_insert_t(&map, int, int, 42, 100);
    st_insert_t(&map, int, int, 17, 200);
    
    // Get
    int *val = st_get_t(&map, int, int, 42);
    printf("Value: %d\n", *val);  // 100
    
    // Remove
    st_remove(&map, &(int){17});
    
    // Cleanup
    st_destroy(&map);
}
```

### String Keys
```c
SwissTable map = st_init_str_malloc(int);

// Insert string keys
st_insert_str(&map, "hello", int, 42);
st_insert_str(&map, "world", int, 100);

// Get by string
int *val = st_get_str(&map, int, "hello");
printf("%d\n", *val);  // 42

// Remove
st_remove_str(&map, "world");

st_destroy(&map);
```

### Custom Memory Arena
```c
#include "arena_allocator.h"

Arena arena = arena_init(PROT_READ | PROT_WRITE);
SwissTable map = st_init_alloc(arena_allocator(&arena), int, int);

// Reserve to avoid reallocations
st_reserve(&map, 10000);

// Use the map...
st_insert_t(&map, int, int, 1, 2);

// Free everything at once
arena_free(&arena);  // Frees the entire arena including the map
```

### Iteration
```c
SwissTable map = st_init_malloc(int, int);
// ... insert data ...

// Iterate over all entries
swisstable_foreach(&map, int, int, key, val,
    printf("[%d] = %d\n", *key, *val);
);
```

## API Reference

### Core Functions
```c
// Initialize empty table
SwissTable st_init(Allocator alloc, size_t key_size, size_t value_size, KeyOps ops);

// Destroy and free all memory
void st_destroy(SwissTable *table);

// Clear all entries (keep allocated memory)
void st_clear(SwissTable *table);

// Insert or update key-value pair (returns false on allocation failure)
bool st_insert(SwissTable *table, void *key, void *value);

// Get value for key (returns NULL if not found)
void *st_get(SwissTable *table, void *key);

// Remove key-value pair (returns true if removed)
bool st_remove(SwissTable *table, void *key);

// Check if key exists
bool st_contains(SwissTable *table, void *key);

// Reserve capacity for min_capacity entries (avoids reallocations)
bool st_reserve(SwissTable *table, size_t min_capacity);

// Print debug statistics
void st_print_stats(SwissTable *table);
```

### Convenience Macros
```c
// Initialize with malloc allocator
st_init_malloc(K, V)
st_init_str_malloc(V)

// Initialize with custom allocator
st_init_alloc(A, K, V)
st_init_str(A, V)

// Type-safe insert (returns bool)
st_insert_t(&map, K, V, key, value)
st_insert_str(&map, string, V, value)

// Type-safe get (returns V*)
st_get_t(&map, K, V, key)
st_get_str(&map, V, string)

// String operations
st_remove_str(&map, string)
st_contains_str(&map, string)

// Iteration
swisstable_foreach(&map, K, V, key_var, val_var, body)
```

### Direct Access
```c
map.size       // Number of entries
map.capacity   // Total capacity
```

## Performance

Benchmarked on Apple M3 with NEON acceleration:
```
Operation              Time (ns)
--------------------------------
Insert (int key)       75
Lookup (int key)       22
String insert          69
String lookup          42
```

**SIMD Acceleration:**
- NEON (ARM): ~20-30% faster than scalar
- SSE2 (x86-64): ~20-30% faster than scalar
- Automatic fallback to scalar on other platforms

## Memory Layout

SwissTable uses separate arrays for metadata, keys, and values:
```
ctrl:   [metadata for each slot - 1 byte per slot]
keys:   [key data - key_size bytes per slot]
values: [value data - value_size bytes per slot]
```

**Load Factor:** Automatically grows at 87.5% capacity (7/8 full)

**Growth Strategy:** Doubles capacity

## Custom Key Types

You can use custom key types with custom hash/equality functions:
```c
typedef struct {
    int x, y;
} Point;

uint64_t point_hash(const void *key, size_t key_size) {
    const Point *p = (const Point *)key;
    return hash_bytes(p, sizeof(Point));
}

bool point_eq(const void *a, const void *b, size_t key_size) {
    return memcmp(a, b, sizeof(Point)) == 0;
}

KeyOps point_ops = {
    .hash = point_hash,
    .eq = point_eq,
    .copy = NULL,
    .destroy = NULL
};

SwissTable map = st_init(malloc_allocator(), sizeof(Point), sizeof(int), point_ops);
```

## Allocator Interface

The library uses a simple allocator interface:
```c
typedef void *(*alloc_fn)(void *ctx, size_t size, size_t align);
typedef void (*free_fn)(void *ctx, void *ptr);
typedef void *(*realloc_fn)(void *ctx, void *ptr, size_t size);

typedef struct {
    alloc_fn   alloc;
    realloc_fn realloc;
    free_fn    free;
    void       *ctx;
} Allocator;
```

This allows integration with any memory management system.

## How It Works

SwissTable is based on Google's [Swiss Tables](https://abseil.io/about/design/swisstables) design:

1. **H1 hash** determines the starting group (16 slots)
2. **H2 hash** (top 7 bits) is stored in control bytes
3. **SIMD** checks 16 control bytes in parallel for H2 matches
4. **Full key comparison** only on SIMD matches (typically 0-2 per lookup)
5. **Linear probing** to next group if not found

## Usage

### Single File
```c
#define SWISSTABLE_IMPLEMENTATION
#include "swisstable.h"
```

### With Custom Allocator
```c
#include "arena_allocator.h"  // Your allocator header

#define SWISSTABLE_IMPLEMENTATION
#include "swisstable.h"
```

## Thread Safety

**Not thread-safe.** Use external synchronization if accessing from multiple threads.

## Limitations

- Maximum capacity: 2^63 entries (practical limit is much lower)
- Keys and values are copied by value (use pointers for large objects)
- No built-in iterator invalidation protection
- Deletion marks slots as deleted (doesn't shrink capacity)
