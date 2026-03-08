/*
   ----------------------------------------------------------------------------
   MEMARENA.H v1.1.0
   ----------------------------------------------------------------------------
   Growing memory arena using mmap and linked lists with ASAN support.
   
   Author:  Juuso Rinta
   Repo:    github.com/juusokasperi/memarena
   License: MIT
   ----------------------------------------------------------------------------
   
   USAGE:
     Define MEMARENA_IMPLEMENTATION in *one* .c file before including this:
     
     #define MEMARENA_IMPLEMENTATION
     #include "memarena.h"

	 In all other files, just #include "memarena.h" as per normal.
*/

#ifndef MEMARENA_H
# define MEMARENA_H

# include <stdint.h>
# include <stdio.h>
# include <stdlib.h>
# include <sys/mman.h>
# include <unistd.h>
# include <string.h>
# include <stdbool.h>
# include <stdarg.h>

/* --- Versioning --- */
#define MEMARENA_VERSION_MAJOR 1
#define MEMARENA_VERSION_MINOR 1
#define MEMARENA_VERSION_PATCH 0

#define MEMARENA_STR_HELPER(x) #x
#define MEMARENA_STR(x) MEMARENA_STR_HELPER(x)
#define MEMARENA_VERSION_STRING \
    MEMARENA_STR(MEMARENA_VERSION_MAJOR) "." \
    MEMARENA_STR(MEMARENA_VERSION_MINOR) "." \
    MEMARENA_STR(MEMARENA_VERSION_PATCH)

// Helper to bundle version info
typedef struct {
	int	major;
	int	minor;
	int	patch;
} MemArenaVersion;


/* --- Configuration --- */
// To disable dynamic resizing (effectively allow only one ArenaBlock)
// #define MEMARENA_DISABLE_RESIZE	

#ifndef MEMARENA_DEFAULT_SIZE
  #define MEMARENA_DEFAULT_SIZE (64 * 1024 * 1024)
#endif

#define DEFAULT_ALIGNMENT 8
#define is_power_of_two(x) ((x != 0) && ((x & (x - 1)) == 0))

//* --- ASAN Support --- *//
#if defined(__SANITIZE_ADDRESS__) || defined(ADDRESS_SANITIZER) \
	|| (defined(__has_feature) && __has_feature(address_sanitizer))
  #include <sanitizer/asan_interface.h>
  #ifndef ASAN_POISON_MEMORY_REGION
    #define ASAN_POISON_MEMORY_REGION(addr, size) \
	  __asan_poison_memory_region((void*)(addr), (size))
  #endif

  #ifndef ASAN_UNPOISON_MEMORY_REGION 
    #define ASAN_UNPOISON_MEMORY_REGION(addr, size) \
	  __asan_unpoison_memory_region((void*)(addr), (size))
  #endif 

#else
  #define ASAN_POISON_MEMORY_REGION(addr, size)		((void)0)
  #define ASAN_UNPOISON_MEMORY_REGION(addr, size)	((void)0)
#endif 



/* --- Structs --- */
typedef struct ArenaBlock ArenaBlock;

struct ArenaBlock
{
ArenaBlock	*prev;
	size_t		size;
	size_t		offset;
};

typedef struct {
	ArenaBlock	*curr;
	int			prot;
} Arena;

typedef struct {
	ArenaBlock	*block;
	size_t		offset;
} ArenaPos;

typedef struct {
	Arena		*arena;
	ArenaPos	pos;
} ArenaTemp;

/* --- API prototypes --- */
Arena			arena_init(int prot);
void			arena_free(Arena *a);
void			arena_reset(Arena *a);

void			*arena_alloc(Arena *a, size_t size);
void			*arena_alloc_aligned(Arena *a, size_t size, size_t align);
void			*arena_alloc_zeroed(Arena *a, size_t size);

void			*arena_realloc(Arena *a, void *ptr, size_t old_size, size_t new_size);
void			*arena_realloc_aligned(Arena *a, void *ptr, size_t old_size, size_t new_size, size_t align);

ArenaTemp		arena_temp_begin(Arena *a);
void			arena_temp_end(ArenaTemp temp);

size_t			arena_total_used(Arena *a);
bool			arena_set_prot(Arena *a, int prot);
void			arena_print_stats(Arena *a);
MemArenaVersion	arena_get_version(void);

// Sprintf that allocates to the arena
char			*arena_sprintf(Arena *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif // MEMARENA_H

/* ========================================================================= */
/* IMPLEMENTATION                                                            */
/* ========================================================================= */

#ifdef MEMARENA_IMPLEMENTATION
#ifndef MEMARENA_IMPLEMENTATION_GUARD
#define MEMARENA_IMPLEMENTATION_GUARD

/* --- Internal static helpers --- */
// Forces the string "MEMARENA_VERSION_x.x.x" into the binary data
#ifndef NDEBUG
 static const char *volatile memarena_version_tag __attribute__((used)) = "MEMARENA_VERSION_" MEMARENA_VERSION_STRING;
#endif

static size_t get_page_size(void) {
    static size_t page_size = 0;
    if (page_size == 0)
	{
        long res = sysconf(_SC_PAGESIZE);
        page_size = (res > 0) ? (size_t)res : 4096;
    }
    return (page_size);
}

static size_t align_to_page(size_t size)
{
    size_t page_size = get_page_size();
return ((size + page_size - 1) & ~(page_size - 1));
}

static uintptr_t align_forward(uintptr_t ptr, size_t align)
{
    uintptr_t a = (uintptr_t)align;
    uintptr_t modulo = ptr & (a - 1);
    if (modulo != 0)
        ptr += a - modulo;
    return (ptr);
}

static ArenaBlock *arena_create_block(size_t capacity, int prot, ArenaBlock *prev_block) 
{
    void *hint_addr = NULL;
    if (prev_block)
        hint_addr = (void *)((char *)prev_block + prev_block->size);

    size_t total_needed = capacity + sizeof(ArenaBlock);
    size_t total_size = align_to_page(total_needed);

    void *base = mmap(hint_addr, total_size, prot, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (base == MAP_FAILED)
        return (NULL);
    
    if (prev_block && base == hint_addr)
	{
        prev_block->size += total_size;
        ASAN_POISON_MEMORY_REGION(base, total_size);
        return (prev_block);
    }

    ArenaBlock *block = (ArenaBlock *)base;
    block->prev = prev_block;
    block->size = total_size;
    block->offset = sizeof(ArenaBlock);

    ASAN_POISON_MEMORY_REGION((char *)base + sizeof(ArenaBlock), total_size - sizeof(ArenaBlock));
    return (block);
}

/* --- API Implementation --- */
Arena arena_init(int prot)
{
    Arena a = {0};
    a.prot = prot;
#ifdef MEMARENA_DISABLE_RESIZE
    a.curr = arena_create_block(MEMARENA_DEFAULT_SIZE, prot, NULL);
#endif
    return (a);
}

void arena_free(Arena *a)
{
    ArenaBlock *curr = a->curr;
    while (curr)
	{
        ArenaBlock *prev = curr->prev;
        munmap(curr, curr->size);
        curr = prev;
    }
    a->curr = NULL;
}

void arena_reset(Arena *a)
{
    if (!a->curr)
		return;
    ArenaBlock *curr = a->curr;
    while (curr->prev != NULL)
	{
        ArenaBlock *prev = curr->prev;
        munmap(curr, curr->size);
        curr = prev;
    }
    a->curr = curr;
    a->curr->offset = sizeof(ArenaBlock);
    ASAN_POISON_MEMORY_REGION((char*)a->curr + sizeof(ArenaBlock), a->curr->size - sizeof(ArenaBlock));
}

void *arena_alloc(Arena *a, size_t size)
{
    return (arena_alloc_aligned(a, size, DEFAULT_ALIGNMENT));
}

void *arena_alloc_aligned(Arena *a, size_t size, size_t align)
{
    if (size == 0)
		return (NULL);
    if (!is_power_of_two(align))
		return (NULL);

    if (a->curr == NULL)
	{
#ifdef MEMARENA_DISABLE_RESIZE
        return (NULL);
#else
        size_t block_size = (size > MEMARENA_DEFAULT_SIZE) ? size : MEMARENA_DEFAULT_SIZE;
        a->curr = arena_create_block(block_size, a->prot, NULL);
        if (!a->curr)
			return (NULL);
#endif
    }

    uintptr_t base_addr = (uintptr_t)a->curr;
    uintptr_t current_addr = base_addr + a->curr->offset;
    uintptr_t aligned_addr = align_forward(current_addr, align);
    size_t padding = aligned_addr - current_addr;

    if (a->curr->offset + padding + size > a->curr->size)
	{
#ifdef MEMARENA_DISABLE_RESIZE
        return (NULL);
#else
        size_t needed = size + align;
        size_t next_size = (needed > MEMARENA_DEFAULT_SIZE) ? needed : MEMARENA_DEFAULT_SIZE;
        ArenaBlock *new_block = arena_create_block(next_size, a->prot, a->curr);
        if (!new_block)
			return (NULL);
        
        if (new_block != a->curr)
		{
            a->curr = new_block;
            base_addr = (uintptr_t)a->curr;
            current_addr = base_addr + a->curr->offset;
            aligned_addr = align_forward(current_addr, align);
            padding = aligned_addr - current_addr;
        }
#endif
    }

    a->curr->offset += padding;
    void *ptr = (void *)(base_addr + a->curr->offset);
    ASAN_UNPOISON_MEMORY_REGION(ptr, size);
    a->curr->offset += size;

	return (ptr);
}

void *arena_alloc_zeroed(Arena *a, size_t size)
{
    void *ptr = arena_alloc(a, size);
    if (ptr)
		memset(ptr, 0, size);
    return (ptr);
}

void *arena_realloc(Arena *a, void *ptr, size_t old_size, size_t new_size)
{
	return (arena_realloc_aligned(a, ptr, old_size, new_size, DEFAULT_ALIGNMENT));
}

void *arena_realloc_aligned(Arena *a, void *ptr, size_t old_size, size_t new_size, size_t align)
{
	if (!is_power_of_two(align))
		return (NULL);
	if (ptr == NULL)
		return (arena_alloc_aligned(a, new_size, align));

	bool is_aligned = ((uintptr_t)ptr & (align - 1)) == 0;

	/* Check for shrink / free / no-op */
	if (new_size <= old_size && is_aligned)
	{
		if (new_size < old_size && a->curr)
		{
			uintptr_t base_addr = (uintptr_t)a->curr;
			uintptr_t arena_top = base_addr + a->curr->offset;
			uintptr_t ptr_end = (uintptr_t)ptr + old_size;
			
			if (ptr_end == arena_top)
			{
				size_t diff = old_size - new_size;
				a->curr->offset -= diff;
    			ASAN_POISON_MEMORY_REGION((char *)ptr + new_size, diff);
			}
		}
		return ((new_size == 0) ? NULL : ptr);
	}

	/* Check for in-place growth */
	if (a->curr && is_aligned)
	{
		uintptr_t base_addr = (uintptr_t)a->curr;
		uintptr_t arena_top = base_addr + a->curr->offset;
		uintptr_t ptr_end = (uintptr_t)ptr + old_size;

		if (ptr_end == arena_top)
		{
			size_t diff = new_size - old_size;
			if (a->curr->offset + diff <= a->curr->size)
			{
				a->curr->offset += diff;
				ASAN_UNPOISON_MEMORY_REGION((void *)ptr_end, diff);
				return (ptr);
			}
		}
	}

	/* Fallback; allocate a new block and copy the old contents */
	void *new_ptr = arena_alloc_aligned(a, new_size, align);
	if (new_ptr)
	{
		size_t copy_size = (old_size < new_size) ? old_size : new_size;
		memcpy(new_ptr, ptr, copy_size);
	}
	return (new_ptr);
}

ArenaTemp arena_temp_begin(Arena *a)
{
    ArenaTemp temp = {0};
    temp.arena = a;
    temp.pos.block = a->curr;
    temp.pos.offset = a->curr ? a->curr->offset : 0;
    return (temp);
}

void arena_temp_end(ArenaTemp temp)
{
    if (!temp.arena->curr)
		return;
    ArenaBlock *curr = temp.arena->curr;
    while (curr != temp.pos.block)
	{
        if (curr->prev == NULL)
			return;
        ArenaBlock *prev = curr->prev;
        munmap(curr, curr->size);
        curr = prev;
    }
    temp.arena->curr = temp.pos.block;
    if (temp.arena->curr)
	{
        size_t old_offset = temp.arena->curr->offset;
        temp.arena->curr->offset = temp.pos.offset;
        ASAN_POISON_MEMORY_REGION(
				(char *)temp.arena->curr + temp.pos.offset,
				old_offset - temp.pos.offset);
    	(void)old_offset;
	}
}

size_t arena_total_used(Arena *a)
{
    size_t total = 0;
    ArenaBlock *curr = a->curr;
    while (curr)
	{
        total += curr->offset;
        curr = curr->prev;
    }
    return (total);
}

bool arena_set_prot(Arena *a, int prot)
{
    ArenaBlock *curr = a->curr;
    while (curr)
	{
        if (mprotect(curr, curr->size, prot) == -1)
			return (false);
        curr = curr->prev;
    }
    a->prot = prot;
    return (true);
}

void arena_print_stats(Arena *a)
{
    size_t total_capacity = 0;
    size_t total_used = 0;
    size_t block_count = 0;

    ArenaBlock *curr = a->curr;
    while (curr)
	{
        total_capacity += curr->size;
        total_used += curr->offset;
        block_count++;
        curr = curr->prev;
    }

    printf("Arena Stats:\n");
    printf("  OS Page size: %zuKiB\n", get_page_size() / 1024);
    printf("  Blocks:   %zu\n", block_count);
    printf("  Capacity: %zu MB (%zu KiB) [%zu bytes]\n",
			total_capacity / (1024 * 1024),
			total_capacity / 1024,
			total_capacity);
    printf("  Used:     %zu MB (%zu KiB) [%zu bytes]\n",
			total_used / (1024 * 1024),
			total_used / 1024,
			total_used);
}

MemArenaVersion arena_get_version(void)
{
    MemArenaVersion v = { 
        MEMARENA_VERSION_MAJOR, 
        MEMARENA_VERSION_MINOR, 
        MEMARENA_VERSION_PATCH 
    };
    return (v);
}

char *arena_sprintf(Arena *a, const char *fmt, ...)
{
    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
	int size = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (size < 0)
		return (NULL);
    char *buffer = arena_alloc(a, size + 1);
    vsnprintf(buffer, size + 1, fmt, args_copy);
    va_end(args_copy);
    return (buffer);
}

#endif // MEMARENA_IMPLEMENTATION_GUARD
#endif // MEMARENA_IMPLEMENTATION
