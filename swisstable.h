#ifndef SWISSTABLE_H
#define SWISSTABLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "arena_allocator.h"

#define CTRL_EMPTY 0x80
#define CTRL_DELETED 0xFE
#define CTRL_SENTINEL 0xFF

#define GROUP_WIDTH 16

typedef uint64_t (*HashFn)(const void *key, size_t key_size);
typedef bool (*EqFn)(const void *a, const void *b, size_t key_size);
typedef void (*KeyCopyFn)(void *dst, const void *src, Allocator alloc);
typedef void (*KeyDestroyFn)(void *key, Allocator alloc);

typedef struct {
	HashFn			hash;
	EqFn			eq;
	KeyCopyFn		copy;
	KeyDestroyFn	destroy;
} KeyOps;

typedef struct {
	uint32_t	len;
	char		*str;
} StringKey;

typedef struct {
	uint8_t		*ctrl;
	void		*keys;
	void		*values;

	size_t		capacity;
	size_t		size;

	size_t		key_size;
	size_t		value_size;

	KeyOps		key_ops;

	Allocator	alloc;
} SwissTable;

/* ============== */
/* Core functions */
/* ============== */

// Initialize empty table with custom Allocator
SwissTable	st_init(Allocator alloc, size_t key_size, size_t value_size, KeyOps key_ops);

// Free all memory
void		st_destroy(SwissTable *table);

// Reset to empty state, don't de-allocate
void		st_clear(SwissTable *table);

// Insert or update key-value pair
bool		st_insert(SwissTable *table, const void *key, void *value);

// Get value pointer for key, NULL if not found
void		*st_get(SwissTable *table, const void *key);

// Remove key-value pair, returns true if removed, false if key did not exist
bool		st_remove(SwissTable *table, const void *key);

// Check if key exists (same as checking if get returns null)
bool		st_contains(SwissTable *table, const void *key);

/* =================== */
/* Capacity management */
/* =================== */

// Pre-allocate desired capacity
bool		st_reserve(SwissTable *table, size_t min_capacity);

// void		st_shrink_to_fit(SwissTable *table);

// Print capacity, load factor, etc. (debugging)
void		st_print_stats(SwissTable *table);

/* ====== */
/* Macros */
/* ====== */

// SwissTable map = st_init_malloc(int, int);
#define st_init_malloc(K, V) \
	st_init(malloc_allocator(), sizeof(K), sizeof(V), default_key_ops())

// Arena arena = arena_init(PROT_READ | PROT_WRITE);
// SwissTable map = st_init_alloc(arena_allocator(&arena), int, int);
#define st_init_alloc(A, K, V) \
	st_init((A), sizeof(K), sizeof(V), default_key_ops())

// SwissTable map = st_init_str_malloc(int);
#define st_init_str_malloc(V) \
	st_init(malloc_allocator(), sizeof(StringKey), sizeof(V), string_key_ops())

// SwissTable map = st_init_str_alloc(arena_allocator(&arena), int);
#define st_init_str_alloc(A, V) \
	st_init((A), sizeof(StringKey), sizeof(V), string_key_ops())

// st_insert_t(&map, int, int, 42, 100)
#define st_insert_t(table, K, V, k, v) \
	st_insert((table), &(K){k}, &(V){v})

// st_get_t(&map, int, int, 42)
#define st_get_t(table, K, V, k) \
	((V*)st_get((table), &(K){k}))

// st_insert_str(&map, "hello", int, 42)
#define st_insert_str(table, str, V, val) \
	st_insert((table), &(StringKey){strlen(str), (char *)(str)}, &(V){val})

// st_get_str(&map, int, "hello")
#define st_get_str(table, V, str) \
	((V*)st_get((table), &(StringKey){strlen(str), (char *)(str)}))

// st_remove_str(&map, "hello")
#define st_remove_str(table, str) \
	st_remove((table), &(StringKey){strlen(str), (char *)(str)})

// st_contains_str(&map, "hello")
#define st_contains_str(table, str) \
	st_contains((table), &(StringKey){strlen(str), (char *)(str)})

#define swisstable_foreach(table, K, V, key_var, val_var, body) \
    for (size_t _i = 0; _i < (table)->capacity; _i++) { \
        if ((table)->ctrl[_i] < CTRL_EMPTY) { \
            K *key_var = (K*)((char*)(table)->keys + _i * sizeof(K)); \
            V *val_var = (V*)((char*)(table)->values + _i * sizeof(V)); \
            body \
        } \
    }

#endif // SWISSTABLE_H

// #define SWISSTABLE_IMPLEMENTATION
#ifdef SWISSTABLE_IMPLEMENTATION
# ifndef SWISSTABLE_IMPLEMENTATION_GUARD
# define SWISSTABLE_IMPLEMENTATION_GUARD

# include <string.h>
# include <stdio.h>

/* ============== */
/* SIMD detection */
/* ============== */

#ifndef FORCE_SCALAR
# if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  include <emmintrin.h>
#  define HAVE_SSE2 1
# elif defined(__aarch64__) || defined(_M_ARM64)
#  include <arm_neon.h>
#  define HAVE_NEON 1
# endif
#endif

/* =============== */
/* Match functions */
/* =============== */

static inline uint32_t match_byte_scalar(const uint8_t *ctrl, uint8_t target)
{
	uint32_t mask = 0;
	for (int i = 0; i < 16; ++i)
	{
		if (ctrl[i] == target)
			mask |= (1U << i);
	}
	return (mask);
}

#ifdef HAVE_SSE2
static inline uint32_t match_byte_sse2(const uint8_t *ctrl, uint8_t target)
{
	__m128i	group = __mm_loadu_si128((const __m1281 *)ctrl);
	__m128i	target_vec = __mm_set1_epi8(target);
	__m128i cmp = __mm_cmpeq_epi8(group, target_vec);
	return ((uint32_t)__mm_movemask_epi8(cmp));
}
#endif

#ifdef HAVE_NEON
static inline uint32_t match_byte_neon(const uint8_t *ctrl, uint8_t target)
{
	uint8x16_t	group = vld1q_u8(ctrl);
	uint8x16_t	target_vec = vdupq_n_u8(target);
	uint8x16_t	cmp = vceqq_u8(group, target_vec);

	uint8_t		result[16];
	vst1q_u8(result, cmp);

	uint32_t	mask = 0;
	#pragma GCC unroll 16
	for (int i = 0; i < 16; ++i)
		mask |= (result[i] ? 1U : 0U) << i;

	return (mask);
}
#endif

static inline uint32_t match_byte(const uint8_t *ctrl, uint8_t target)
{
#ifdef HAVE_SSE2
	return (match_byte_sse2(ctrl, target));
#elif HAVE_NEON
	return (match_byte_neon(ctrl, target));
#else
	return (match_byte_scalar(ctrl, target));
#endif
}

static inline uint32_t match_empty(const uint8_t *ctrl) { return (match_byte(ctrl, CTRL_EMPTY)); }
/* ================ */
/* Internal helpers */
/* ================ */

static inline size_t next_power_of_2(size_t n)
{
	if (n == 0)
		return (1);
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;
	return (n + 1);
}

static inline int trailing_zeros(uint32_t x)
{
#if defined(__GNUC__) || defined(__clang__)
	return (__builtin_ctz(x));
#else
	int	count = 0;
	while ((x & 1) == 0 && count < 32)
	{
		x >>= 1;
		count++;
	}
	return (count);
#endif
}


/* ============== */
/* Hash functions */
/* ============== */

uint64_t hash_bytes(const void *data, size_t len)
{
	uint64_t		hash = 14695981039346656037ULL;
	uint64_t		prime = 1099511628211ULL;
	const uint8_t	*bytes = (const uint8_t *)data;

	for (size_t i = 0; i < len; ++i)
	{
		hash ^= bytes[i];
		hash *= prime;
	}
	return (hash);
}

size_t H1(uint64_t hash, size_t capacity) { return (hash & (capacity - 1)); }
uint8_t H2(uint64_t hash) { return ((hash >> 57) & 0x7F); }
uint64_t default_hash(const void *data, size_t len) { return (hash_bytes(data, len)); }
bool default_eq(const void *a, const void *b, size_t len) { return (memcmp(a, b, len) == 0); }

uint64_t stringkey_hash(const void *key, size_t _)
{
	const StringKey *k = (const StringKey *)key;
	return (hash_bytes(k->str, k->len));
}

bool stringkey_eq(const void *a, const void *b, size_t _)
{
	const StringKey *ka = (const StringKey *)a;
	const StringKey *kb = (const StringKey *)b;
	if (ka->len != kb->len)
		return (false);
	return (memcmp(ka->str, kb->str, ka->len) == 0);
}

/* ================ */
/* KeyOps functions */
/* ================ */

KeyOps default_key_ops(void)
{
	KeyOps ops;

	ops.hash = default_hash;
	ops.eq = default_eq;
	ops.copy = NULL;
	ops.destroy = NULL;

	return (ops);
}

void stringkey_copy(void *dst, const void *src, Allocator alloc)
{
	const StringKey *src_k = (const StringKey *)src;
	StringKey		*dst_k = (StringKey *)dst;

	dst_k->len = src_k->len;
	dst_k->str = (char *)alloc.alloc(alloc.ctx, src_k->len + 1, 0);
	memcpy(dst_k->str, src_k->str, src_k->len + 1);
}

void stringkey_destroy(void *key, Allocator alloc)
{
	StringKey *k = (StringKey *)key;
	if (alloc.free && k->str)
		alloc.free(alloc.ctx, k->str);
}

KeyOps string_key_ops(void)
{
	KeyOps ops;

	ops.hash = stringkey_hash;
	ops.eq = stringkey_eq;
	ops.copy = stringkey_copy;
	ops.destroy = stringkey_destroy;

	return (ops);
}

/* ================ */
/* Table allocation */
/* ================ */

bool allocate_table(SwissTable *table, size_t capacity)
{
	// Capacity must be a power of two
	if (capacity & (capacity - 1))
		return (false);

	size_t	ctrl_size = capacity + GROUP_WIDTH + 1; // capacity + 16 for SIMD overflow + 1 sentinel
	uint8_t	*new_ctrl = (uint8_t *)table->alloc.alloc(table->alloc.ctx, ctrl_size, 0);
	void	*new_keys = table->alloc.alloc(table->alloc.ctx, capacity * table->key_size, 0);
	void	*new_values = table->alloc.alloc(table->alloc.ctx, capacity * table->value_size, 0);

	if (!new_ctrl || !new_keys || !new_values)
	{
		if (table->alloc.free)
		{
			if (new_ctrl)	table->alloc.free(table->alloc.ctx, new_ctrl);
			if (new_keys)	table->alloc.free(table->alloc.ctx, new_keys);
			if (new_values)	table->alloc.free(table->alloc.ctx, new_values);
		}
		return (false);
	}

	memset(new_ctrl, CTRL_EMPTY, ctrl_size);
	new_ctrl[capacity] = CTRL_SENTINEL;

	table->ctrl = new_ctrl;
	table->keys = new_keys;
	table->values = new_values;
	table->capacity = capacity;

	return (true);
}

static void st_rehash(SwissTable *table, size_t new_capacity)
{
	if (new_capacity == 0)
		return;

	SwissTable old = *table;
	table->size = 0;
	if (!allocate_table(table, new_capacity))
	{
		*table = old;
		return;
	}

	if (old.capacity == 0)
		return;

	KeyCopyFn copy = table->key_ops.copy;
	table->key_ops.copy = NULL;

	for (size_t i = 0; i < old.capacity; ++i)
	{
		if (old.ctrl[i] < CTRL_EMPTY)
		{
			void	*key = (char *)old.keys + i * old.key_size;
			void	*val = (char *)old.values + i * old.value_size;
			st_insert(table, key, val);
		}
	}

	table->key_ops.copy = copy;

	if (old.alloc.free)
	{
		old.alloc.free(old.alloc.ctx, old.ctrl);
		old.alloc.free(old.alloc.ctx, old.keys);
		old.alloc.free(old.alloc.ctx, old.values);
	}
}

/* ======== */
/* Core API */
/* ======== */

SwissTable	st_init(Allocator alloc, size_t key_size, size_t value_size, KeyOps key_ops)
{
	SwissTable st;
	
	st.ctrl = NULL;
	st.keys = NULL;
	st.values = NULL;

	st.capacity = 0;
	st.size = 0;
	st.key_size = key_size;
	st.value_size = value_size;

	st.key_ops = key_ops;
	st.alloc = alloc;

	return (st);
}

static inline void clear_keys(SwissTable *table)
{
	for (size_t i = 0; i < table->capacity; ++i)
	{
		if (table->ctrl[i] < CTRL_EMPTY)
		{
			void *key = (char*)table->keys + i * table->key_size;
			table->key_ops.destroy(key, table->alloc);
		}
	}
}

void st_destroy(SwissTable *table)
{
	if (table->key_ops.destroy)
		clear_keys(table);
	if (table->alloc.free)
	{
		if (table->ctrl)
			table->alloc.free(table->alloc.ctx, table->ctrl);
		if (table->keys)
			table->alloc.free(table->alloc.ctx, table->keys);
		if (table->values)
			table->alloc.free(table->alloc.ctx, table->values);
	}
	memset(table, 0, sizeof(SwissTable));
}

void st_clear(SwissTable *table)
{
	if (table->key_ops.destroy)
		clear_keys(table);
	if (table->ctrl)
	{
		size_t ctrl_size = table->capacity + GROUP_WIDTH + 1;
		memset(table->ctrl, CTRL_EMPTY, ctrl_size);
		table->ctrl[table->capacity] = CTRL_SENTINEL;
	}
	table->size = 0;
}

bool st_insert(SwissTable *table, const void *key, void *value)
{
	if (table->size * 8 >= table->capacity * 7 || table->capacity == 0)
	{
		size_t new_capacity = table->capacity == 0 ? 16 : table->capacity * 2;
		st_rehash(table, new_capacity);
		if (table->capacity == 0) // Alloc failed
			return (false);
	}

	uint64_t	hash = table->key_ops.hash(key, table->key_size);
	uint8_t		h2 = H2(hash);
	size_t		idx = H1(hash, table->capacity);

	for (size_t probe = 0; probe < table->capacity; ++probe)
	{
		size_t		pos = (idx + probe) & (table->capacity - 1);
		size_t		grp_start = pos & ~15ULL; // align to 16

		uint32_t	matches = match_byte(&table->ctrl[grp_start], h2);
		uint32_t	empties = match_empty(&table->ctrl[grp_start]);

		while (matches)
		{
			int		offset = trailing_zeros(matches);
			size_t	candidate = grp_start + offset;

			void	*candidate_key = (char *)table->keys + candidate * table->key_size;
			if (table->key_ops.eq(candidate_key, key, table->key_size))
			{
				void *candidate_value = (char *)table->values + candidate * table->value_size;
				memcpy(candidate_value, value, table->value_size);
				return (true);
			}

			matches &= matches - 1; // clear lowest bit
		}

		if (empties)
		{
			int		offset = trailing_zeros(empties);
			size_t	slot = grp_start + offset;

			table->ctrl[slot] = h2;
			void	*key_slot = (char *)table->keys + slot * table->key_size;
			if (table->key_ops.copy)
				table->key_ops.copy(key_slot, key, table->alloc);
			else
				memcpy(key_slot, key, table->key_size);
			memcpy((char *)table->values + slot * table->value_size, value, table->value_size);
			table->size++;

			// Mirror to overflow area for SIMD wraparound
			if (slot < 15)
				table->ctrl[table->capacity + slot] = h2;

			return (true);
		}
	}
	return (false); // Table full (should not happen)
}

void *st_get(SwissTable *table, const void *key)
{
	if (table->capacity == 0)
		return (NULL);

	uint64_t	hash = table->key_ops.hash(key, table->key_size);
	uint8_t		h2 = H2(hash);
	size_t		idx = H1(hash, table->capacity);

	for (size_t probe = 0; probe < table->capacity; ++probe)
	{
		size_t	pos = (idx + probe) & (table->capacity - 1);
		size_t	grp_start = pos & ~15ULL;
		
		uint32_t	matches = match_byte(&table->ctrl[grp_start], h2);
		uint32_t	empties = match_empty(&table->ctrl[grp_start]);

		while (matches)
		{
			int		offset = trailing_zeros(matches);
			size_t	candidate = grp_start + offset;

			void	*candidate_key = (char *)table->keys + candidate * table->key_size;
			if (table->key_ops.eq(candidate_key, key, table->key_size))
				return ((char *)table->values + candidate * table->value_size);

			matches &= matches - 1;
		}

		if (empties)
			return (NULL);
	}
	return (NULL);
}

bool st_remove(SwissTable *table, const void *key)
{
	if (table->capacity == 0)
		return (false);

	uint64_t	hash = table->key_ops.hash(key, table->key_size);
	uint8_t		h2 = H2(hash);
	size_t		idx = H1(hash, table->capacity);

	for (size_t probe = 0; probe < table->capacity; ++probe)
	{
		size_t		pos = (idx + probe) & (table->capacity - 1);
		size_t		grp_start = pos & ~15ULL;

		uint32_t	matches = match_byte(&table->ctrl[grp_start], h2);
		uint32_t	empties = match_empty(&table->ctrl[grp_start]);

		while (matches)
		{
			int		offset = trailing_zeros(matches);
			size_t	candidate = grp_start + offset;

			void	*candidate_key = (char *)table->keys + candidate * table->key_size;
			if (table->key_ops.eq(candidate_key, key, table->key_size))
			{
				if (table->key_ops.destroy)
					table->key_ops.destroy(candidate_key, table->alloc);
				table->ctrl[candidate] = CTRL_DELETED;
				table->size--;

				if (candidate < 15)
					table->ctrl[table->capacity + candidate] = CTRL_DELETED;
				return (true);
			}
			
			matches &= matches - 1;
		}

		if (empties)
			return (false);
	}
	return (false);
}

bool st_contains(SwissTable *table, const void *key) { return (st_get(table, key) != NULL); }

/* =================== */
/* Capacity management */
/* =================== */

bool st_reserve(SwissTable *table, size_t min_capacity)
{
	if (min_capacity == 0)
		return (true);

	// 87.5% load factor
	size_t needed_capacity = (min_capacity * 8 + 6) / 7;
	needed_capacity = next_power_of_2(needed_capacity);

	if (needed_capacity <= table->capacity)
		return (true);

	st_rehash(table, needed_capacity);
	return (table->capacity >= needed_capacity);
}

void st_print_stats(SwissTable *table)
{
	printf("Swisstable stats:\n");
	printf("  Capacity: %zu\n", table->capacity);
	printf("  Size: %zu\n", table->size);
	printf("  Load: %.1f%%\n",
		table->capacity ? (100.0 * table->size / table->capacity) : 0.0);
	printf("  Key size: %zu bytes\n", table->key_size);
	printf("  Value size: %zu bytes\n", table->value_size);
}


#endif // SWISSTABLE_IMPLEMENTATION_GUARD
#endif // SWISSTABLE_IMPLEMENTATION
