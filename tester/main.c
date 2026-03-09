#define MEMARENA_IMPLEMENTATION
#include "../arena_allocator.h"

#define SWISSTABLE_IMPLEMENTATION
#include "../swisstable.h"

#include <stdio.h>
#include <assert.h>
#include <stdarg.h>

#define PRPL "\033[1;95m"
#define GRN  "\033[1;92m"
#define CYAN "\033[1;96m"
#define YLW "\033[1;93m"
#define RED  "\033[1;91m"
#define RESET "\033[0m"

void success(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf(GRN "[OK] ");
	vprintf(fmt, args);
	printf(RESET "\n");
	va_end(args);
}

void info(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf(CYAN);
	vprintf(fmt, args);
	printf(RESET "\n");
	va_end(args);
}

void print_separator(const char *title)
{
	printf("\n" YLW);
	printf("===========================================\n");
	printf("%s\n", title);
	printf("===========================================\n" RESET);
}

void test_basic_operations()
{
	print_separator("TEST: Basic Operations (malloc)");
	
	SwissTable map = st_init_malloc(int, int);
	
	info("Inserting key=42, value=100");
	assert(st_insert_t(&map, int, int, 42, 100));
	assert(map.size == 1);
	
	info("Inserting key=17, value=200");
	assert(st_insert_t(&map, int, int, 17, 200));
	assert(map.size == 2);
	
	// Test get
	int *val = st_get_t(&map, int, int, 42);
	assert(val != NULL);
	assert(*val == 100);
	success("get(42) = %d", *val);
	
	val = st_get_t(&map, int, int, 17);
	assert(val != NULL);
	assert(*val == 200);
	success("get(17) = %d", *val);
	
	// Test non-existent key
	val = st_get_t(&map, int, int, 999);
	assert(val == NULL);
	success("get(999) = NULL (not found)");
	
	// Test contains
	assert(st_contains(&map, &(int){42}));
	assert(st_contains(&map, &(int){17}));
	assert(!st_contains(&map, &(int){999}));
	success("contains() working");
	
	// Test update (insert existing key)
	info("Updating key=42 to value=999");
	assert(st_insert_t(&map, int, int, 42, 999));
	assert(map.size == 2);  // Size shouldn't change
	val = st_get_t(&map, int, int, 42);
	assert(*val == 999);
	success("Updated value: %d", *val);
	
	// Test remove
	info("Removing key=17");
	assert(st_remove(&map, &(int){17}));
	assert(map.size == 1);
	assert(!st_contains(&map, &(int){17}));
	success("Removed successfully");
	
	// Remove non-existent
	assert(!st_remove(&map, &(int){999}));
	success("Remove non-existent returns false");
	
	st_print_stats(&map);
	st_destroy(&map);
	success("Basic operations test passed");
}

void test_growth()
{
	print_separator("TEST: Automatic Growth");
	
	SwissTable map = st_init_malloc(int, int);
	
	info("Inserting 100 entries");
	for (int i = 0; i < 100; i++)
	{
		assert(st_insert_t(&map, int, int, i, i * 10));
	}
	
	info("Map size: %zu, capacity: %zu", map.size, map.capacity);
	assert(map.size == 100);
	assert(map.capacity >= 128);
	
	info("Verifying all 100 entries");
	for (int i = 0; i < 100; i++)
	{
		int *val = st_get_t(&map, int, int, i);
		assert(val != NULL);
		assert(*val == i * 10);
	}
	success("All entries verified");
	
	st_print_stats(&map);
	st_destroy(&map);
	success("Growth test passed");
}

void test_reserve()
{
	print_separator("TEST: Reserve");
	
	SwissTable map = st_init_malloc(int, int);
	
	info("Reserving space for 1000 entries");
	assert(st_reserve(&map, 1000));
	size_t reserved_capacity = map.capacity;
	info("Reserved capacity: %zu (accounts for 87.5%% load factor)", reserved_capacity);
	assert(reserved_capacity >= 1143);
	
	info("Inserting 1000 entries");
	for (int i = 0; i < 1000; i++)
	{
		assert(st_insert_t(&map, int, int, i, i * 2));
	}
	
	info("Capacity after inserts: %zu", map.capacity);
	assert(map.capacity == reserved_capacity);
	success("No reallocation occurred");
	
	// Verify
	info("Verifying entries");
	for (int i = 0; i < 1000; i++)
	{
		int *val = st_get_t(&map, int, int, i);
		assert(val != NULL);
		assert(*val == i * 2);
	}
	
	st_print_stats(&map);
	st_destroy(&map);
	success("Reserve test passed");
}

void test_clear()
{
	print_separator("TEST: Clear");
	
	SwissTable map = st_init_malloc(int, int);
	
	// Insert some data
	for (int i = 0; i < 50; i++)
	{
		st_insert_t(&map, int, int, i, i);
	}
	
	size_t old_capacity = map.capacity;
	info("Before clear: size=%zu, capacity=%zu", map.size, old_capacity);
	
	st_clear(&map);
	
	info("After clear: size=%zu, capacity=%zu", map.size, map.capacity);
	assert(map.size == 0);
	assert(map.capacity == old_capacity);
	
	for (int i = 0; i < 50; i++)
		assert(!st_contains(&map, &(int){i}));
	
	assert(st_insert_t(&map, int, int, 100, 200));
	int *val = st_get_t(&map, int, int, 100);
	assert(*val == 200);
	
	st_destroy(&map);
	success("Clear test passed");
}

void test_with_arena()
{
	print_separator("TEST: Arena Allocator");
	
	Arena arena = arena_init(PROT_READ | PROT_WRITE);
	SwissTable map = st_init_alloc(arena_allocator(&arena), int, int);
	
	info("Reserving for 500 entries (one allocation)");
	st_reserve(&map, 500);
	
	info("Inserting 500 entries");
	for (int i = 0; i < 500; i++)
		assert(st_insert_t(&map, int, int, i, i * 3));
	
	info("Verifying entries");
	for (int i = 0; i < 500; i++)
	{
		int *val = st_get_t(&map, int, int, i);
		assert(val != NULL);
		assert(*val == i * 3);
	}
	
	st_print_stats(&map);
	arena_print_stats(&arena);
	
	info("Testing ArenaTemp with hash table");
	ArenaTemp temp = arena_temp_begin(&arena);
	{
		SwissTable temp_map = st_init_alloc(arena_allocator(&arena), int, int);
		for (int i = 0; i < 100; i++)
			st_insert_t(&temp_map, int, int, i, i);
		info("Temp map size: %zu", temp_map.size);
	}
	arena_temp_end(temp);
	info("After temp_end:");
	arena_print_stats(&arena);
	
	int *val = st_get_t(&map, int, int, 42);
	assert(val != NULL);
	assert(*val == 42 * 3);
	success("Original map still valid after temp_end");
	
	arena_free(&arena);
	success("Arena test passed");
}

void test_iteration()
{
	print_separator("TEST: Iteration");
	
	SwissTable map = st_init_malloc(int, int);
	
	for (int i = 0; i < 20; i++)
		st_insert_t(&map, int, int, i, i * 100);
	
	info("Iterating through all entries:");
	int count = 0;
	int sum = 0;
	
	swisstable_foreach(&map, int, int, key, val,
		printf(PRPL "  [%d] = %d\n" RESET, *key, *val);
		count++;
		sum += *val;
	);
	
	info("Found %d entries, sum of values = %d", count, sum);
	assert(count == 20);
	assert(sum == 19000);
	
	st_destroy(&map);
	success("Iteration test passed");
}

void test_collisions()
{
	print_separator("TEST: Hash Collisions");
	
	SwissTable map = st_init_malloc(int, int);
	st_reserve(&map, 12);
	
	info("Reserved capacity: %zu", map.capacity);
	info("Inserting 12 entries (75%% load in 16-capacity table)");
	for (int i = 0; i < 12; i++)
		assert(st_insert_t(&map, int, int, i * 1000, i));
	
	info("Verifying all entries despite collisions");
	for (int i = 0; i < 12; i++)
	{
		int *val = st_get_t(&map, int, int, i * 1000);
		assert(val != NULL);
		assert(*val == i);
	}
	
	st_print_stats(&map);
	st_destroy(&map);
	success("Collision test passed");
}

void stress_test()
{
	print_separator("TEST: Stress Test (10,000 entries)");
	
	SwissTable map = st_init_malloc(int, int);
	
	info("Inserting 10,000 entries");
	for (int i = 0; i < 10000; i++)
		assert(st_insert_t(&map, int, int, i, i * 7));
	
	info("Verifying all 10,000 entries");
	for (int i = 0; i < 10000; i++)
	{
		int *val = st_get_t(&map, int, int, i);
		assert(val != NULL);
		assert(*val == i * 7);
	}
	
	info("Removing every other entry (5,000 removals)");
	for (int i = 0; i < 10000; i += 2)
		assert(st_remove(&map, &(int){i}));
	assert(map.size == 5000);
	
	info("Verifying remaining 5,000 entries");
	for (int i = 0; i < 10000; i++)
	{
		int *val = st_get_t(&map, int, int, i);
		if (i % 2 == 0)
			assert(val == NULL);
		else
		{
			assert(val != NULL);
			assert(*val == i * 7);
		}
	}
	
	st_print_stats(&map);
	st_destroy(&map);
	success("Stress test passed");
}

void test_string_keys()
{
    print_separator("TEST: String Keys");

    SwissTable map = st_init_str_malloc(int);

    info("Inserting string keys");
    assert(st_insert_str(&map, "hello", int, 42));
    assert(st_insert_str(&map, "world", int, 99));
    assert(st_insert_str(&map, "foo", int, 123));
    assert(st_insert_str(&map, "bar", int, 456));

    info("Checking contains()");
    assert(st_contains_str(&map, "hello"));
    assert(st_contains_str(&map, "world"));
    assert(!st_contains_str(&map, "baz"));
    success("Contains checks passed");

    info("Retrieving values");
    int *val = st_get_str(&map, int, "hello");
    assert(val && *val == 42);
    success("hello = %d", *val);

    val = st_get_str(&map, int, "world");
    assert(val && *val == 99);
    success("world = %d", *val);

    val = st_get_str(&map, int, "foo");
    assert(val && *val == 123);
    success("foo = %d", *val);

    val = st_get_str(&map, int, "bar");
    assert(val && *val == 456);
    success("bar = %d", *val);

    info("Updating value of 'hello' to 777");
    assert(st_insert_str(&map, "hello", int, 777));
    val = st_get_str(&map, int, "hello");
    assert(val && *val == 777);
    success("Updated hello = %d", *val);

    info("Removing 'world'");
    assert(st_remove_str(&map, "world"));
    assert(!st_contains_str(&map, "world"));
    success("'world' removed successfully");

    st_print_stats(&map);
    st_destroy(&map);
    success("String key test passed");
}

void stress_test_strings()
{
    print_separator("TEST: String Key Stress Test (10,000 entries)");

    SwissTable map = st_init_str_malloc(int);

    info("Generating and inserting 10,000 string keys");
    char buffer[64];
    for (int i = 0; i < 10000; ++i)
    {
        snprintf(buffer, sizeof(buffer), "key_%05d", i);
        assert(st_insert_str(&map, buffer, int, i * 3));
    }

    info("Verifying all 10,000 entries");
    for (int i = 0; i < 10000; i++)
    {
        snprintf(buffer, sizeof(buffer), "key_%05d", i);
        int *val = st_get_str(&map, int, buffer);
        assert(val != NULL);
        assert(*val == i * 3);
    }

    success("All 10,000 string entries verified");

    info("Removing every 4th entry (2,500 removals)");
    for (int i = 0; i < 10000; i += 4)
    {
        snprintf(buffer, sizeof(buffer), "key_%05d", i);
        assert(st_remove_str(&map, buffer));
    }

    info("Verifying remaining entries");
    for (int i = 0; i < 10000; i++)
    {
        snprintf(buffer, sizeof(buffer), "key_%05d", i);
        int *val = st_get_str(&map, int, buffer);
        if (i % 4 == 0)
            assert(val == NULL);
        else
        {
            assert(val != NULL);
            assert(*val == i * 3);
        }
    }
    success("Remaining entries verified after removals");

    st_print_stats(&map);
    st_destroy(&map);
    success("String key stress test passed");
}

int main(void)
{
	printf("\n PRPL");
	printf("╔═══════════════════════════════════════╗\n");
	printf("║   Swiss Table Test Suite              ║\n");
	printf("╚═══════════════════════════════════════╝\n" RESET);
	
	test_basic_operations();
	test_growth();
	test_reserve();
	test_clear();
	test_with_arena();
	test_iteration();
	test_collisions();
	stress_test();
	test_string_keys();
	stress_test_strings();
	
	print_separator("ALL TESTS PASSED! ✓");
	
	return (0);
}
