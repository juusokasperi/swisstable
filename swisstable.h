#ifndef SWISSTABLE_H
#define SWISSTABLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "arena_allocator.h"

#define CTRL_EMPTY 0x80
#define CTRL_DELETED 0xFE
#define CTRL_SENTINEL 0xFF

typedef struct {
	uint8_t		*ctrl;
	void		*keys;
	void		*values;
	size_t		capacity;
	size_t		size;
	size_t		key_size;
	size_t		value_size;
	Allocator	alloc;
} SwissTable;

/* === */
/* API */
/* === */

/* ============== */
/* Core functions */
/* ============== */

// Initialize empty table with custom Allocator
// Starts with capacity 0, lazy-allocation on first insert
SwissTable	st_init(Allocator alloc, size_t key_size, size_t value_size);

// Free all memory
void		st_destroy(SwissTable *table);

// Reset to empty state, don't de-allocate
void		st_clear(SwissTable *table);

// Insert or update key-value pair
// Return true on success, false on alloc failure
// Grows table automatically when load exceeds 87.5%
bool		st_insert(SwissTable *table, void *key, void *value);

// Get value pointer for key, NULL if not found
void		*st_get(SwissTable *table, void *key);

// Remove key-value pair,
// returns true if removed, false if key did not exist
bool		st_remove(SwissTable *table, void *key);

// Check if key exists (same as checking if get returns null)
bool		st_contains(SwissTable *table, void *key);

/* =================== */
/* Capacity management */
/* =================== */

bool		st_reserve(SwissTable *table, size_t min_capacity);
// void		st_shrink_to_fit(SwissTable *table);

/* ================= */
/* Utility functions */
/* ================= */

// Print capacity, load factor, etc. (debugging)
void		st_print_stats(SwissTable *table);

/* ====== */
/* Macros */
/* ====== */

// SwissTable map = st_init_malloc(int, int);
#define st_init_malloc(K, V) \
	st_init(malloc_allocator(), sizeof(K), sizeof(V))

// if (!st_insert_t(&map, int, int, 42, 100))
//   error_handling..
#define st_insert_t(table, K, V, k, v) \
	st_insert((table), &(K){k}, &(V){v})

// Returns a typed pointer
// int *val = st_get_t(&map, int, int, 42);
#define st_get_t(table, K, V, k) \
	((V*)st_get((table), &(K){k}))

#define swisstable_foreach(table, K, V, key_var, val_var, body) \
    for (size_t _i = 0; _i < (table)->capacity; _i++) { \
        if ((table)->ctrl[_i] < 0x80) { \
            K *key_var = (K*)((char*)(table)->keys + _i * sizeof(K)); \
            V *val_var = (V*)((char*)(table)->values + _i * sizeof(V)); \
            body \
        } \
    }

uint64_t	hash_bytes(const void *data, size_t len);
size_t		H1(uint64_t hash, size_t capacity);
uint8_t		H2(uint64_t hash);
bool		allocate_table(SwissTable *table, size_t capacity);


#endif // SWISSTABLE_H

#ifdef SWISSTABLE_IMPLEMENTATION
# ifndef SWISSTABLE_IMPLEMENTATION_GUARD
# define SWISSTABLE_IMPLEMENTATION_GUARD

# include <string.h>
# include <stdio.h>

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

// TODO Scalar matching for now (replace with SIMD)
static inline uint32_t match_byte(const uint8_t *ctrl, uint8_t target)
{
	uint32_t mask = 0;
	for (int i = 0; i < 16; ++i)
	{
		if (ctrl[i] == target)
			mask |= (1U << i);
	}
	return (mask);
}

static inline uint32_t match_empty(const uint8_t *ctrl)
{
	return (match_byte(ctrl, CTRL_EMPTY));
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

/* ================ */
/* Table allocation */
/* ================ */

bool allocate_table(SwissTable *table, size_t capacity)
{
	// Capacity must be a power of two
	if (capacity & (capacity - 1))
		return (false);

	size_t	ctrl_size = capacity + 16 + 1; // capacity + 16 for SIMD over flow + 1 sentinel
	uint8_t	*new_ctrl = (uint8_t *)table->alloc.alloc(table->alloc.ctx, ctrl_size, 0);
	if (!new_ctrl)
		return (false);

	memset(new_ctrl, CTRL_EMPTY, ctrl_size);
	new_ctrl[capacity] = CTRL_SENTINEL;

	void	*new_keys = table->alloc.alloc(table->alloc.ctx, capacity * table->key_size, 0);
	void	*new_values = table->alloc.alloc(table->alloc.ctx, capacity * table->value_size, 0);

	if (!new_keys || !new_values)
	{
		if (table->alloc.free)
		{
			if (new_keys)
				table->alloc.free(table->alloc.ctx, new_keys);
			if (new_values)
				table->alloc.free(table->alloc.ctx, new_values);
			table->alloc.free(table->alloc.ctx, new_ctrl);
			return (false);
		}
	}

	table->ctrl = new_ctrl;
	table->keys = new_keys;
	table->values = new_values;
	table->capacity = capacity;
	return (true);
}

static void st_rehash(SwissTable *table, size_t new_capacity)
{
	SwissTable old = *table;
	
	table->size = 0;
	if (!allocate_table(table, new_capacity))
	{
		*table = old;
		return;
	}

	if (old.capacity == 0)
		return;

	for (size_t i = 0; i < old.capacity; ++i)
	{
		if (old.ctrl[i] < 0x80)
		{
			void	*key = (char *)old.keys + i * old.key_size;
			void	*val = (char *)old.values + i * old.value_size;
			st_insert(table, key, val);
		}
	}

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

SwissTable	st_init(Allocator alloc, size_t key_size, size_t value_size)
{
	SwissTable st;
	
	st.ctrl = NULL;
	st.keys = NULL;
	st.values = NULL;
	st.capacity = 0;
	st.size = 0;
	st.key_size = key_size;
	st.value_size = value_size;
	st.alloc = alloc;
	return (st);
}

void st_destroy(SwissTable *table)
{
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
	if (table->ctrl)
	{
		size_t ctrl_size = table->capacity + 16 + 1;
		memset(table->ctrl, CTRL_EMPTY, ctrl_size);
		table->ctrl[table->capacity] = CTRL_SENTINEL;
	}
	table->size = 0;
}

bool st_insert(SwissTable *table, void *key, void *value)
{
	if (table->size * 8 >= table->capacity * 7 || table->capacity == 0)
	{
		size_t new_capacity = table->capacity == 0 ? 16 : table->capacity * 2;
		st_rehash(table, new_capacity);
		if (table->capacity == 0) // Alloc failed
			return (false);
	}

	uint64_t	hash = hash_bytes(key, table->key_size);
	uint8_t		h2 = H2(hash);
	size_t		idx = H1(hash, table->capacity);

	for (size_t probe = 0; probe < table->capacity; ++probe)
	{
		size_t		pos = (idx + probe) & (table->capacity - 1);
		size_t		grp_start = (pos / 16) * 16; // align to 16

		uint32_t	matches = match_byte(&table->ctrl[grp_start], h2);
		uint32_t	empties = match_empty(&table->ctrl[grp_start]);

		while (matches)
		{
			int		offset = trailing_zeros(matches);
			size_t	candidate = grp_start + offset;

			void	*candidate_key = (char *)table->keys + candidate * table->key_size;
			if (memcmp(candidate_key, key, table->key_size) == 0)
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
			memcpy((char *)table->keys + slot * table->key_size, key, table->key_size);
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

void *st_get(SwissTable *table, void *key)
{
	if (table->capacity == 0)
		return (NULL);

	uint64_t	hash = hash_bytes(key, table->key_size);
	uint8_t		h2 = H2(hash);
	size_t		idx = H1(hash, table->capacity);

	for (size_t probe = 0; probe < table->capacity; ++probe)
	{
		size_t	pos = (idx + probe) & (table->capacity - 1);
		size_t	grp_start = (pos / 16) * 16;
		
		uint32_t	matches = match_byte(&table->ctrl[grp_start], h2);
		uint32_t	empties = match_empty(&table->ctrl[grp_start]);

		while (matches)
		{
			int		offset = trailing_zeros(matches);
			size_t	candidate = grp_start + offset;

			void	*candidate_key = (char *)table->keys + candidate * table->key_size;
			if (memcmp(candidate_key, key, table->key_size) == 0)
				return ((char *)table->values + candidate * table->value_size);

			matches &= matches - 1;
		}

		if (empties)
			return (NULL);
	}
	return (NULL);
}

bool st_remove(SwissTable *table, void *key)
{
	if (table->capacity == 0)
		return (false);

	uint64_t	hash = hash_bytes(key, table->key_size);
	uint8_t		h2 = H2(hash);
	size_t		idx = H1(hash, table->capacity);

	for (size_t probe = 0; probe < table->capacity; ++probe)
	{
		size_t		pos = (idx + probe) & (table->capacity - 1);
		size_t		grp_start = (pos / 16) * 16;

		uint32_t	matches = match_byte(&table->ctrl[grp_start], h2);
		uint32_t	empties = match_empty(&table->ctrl[grp_start]);

		while (matches)
		{
			int		offset = trailing_zeros(matches);
			size_t	candidate = grp_start + offset;

			void	*candidate_key = (char *)table->keys + candidate * table->key_size;
			if (memcmp(candidate_key, key, table->key_size) == 0)
			{
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

bool st_contains(SwissTable *table, void *key) { return (st_get(table, key) != NULL); }

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
