#define MEMARENA_IMPLEMENTATION
#include "../memarena.h"

#define SWISSTABLE_IMPLEMENTATION
#include "../swisstable.h"

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#define PRPL "\033[1;95m"
#define GRN  "\033[1;92m"
#define CYAN "\033[1;96m"
#define YLW "\033[1;93m"
#define RED  "\033[1;91m"
#define RESET "\033[0m"

void info(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf(CYAN "  .   ");
	vprintf(fmt, args);
	printf(RESET "\n");
	va_end(args);
}

void success(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf(GRN "  [OK] ");
	vprintf(fmt, args);
	printf(RESET "\n");
	va_end(args);
}

double get_time_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

void benchmark_inserts(size_t num_ops)
{
	printf("\n" YLW);
	printf("===========================================\n");
	printf("Benchmark: %zu INSERT operations\n", num_ops);
	printf("===========================================\n" RESET);
	
	SwissTable map = st_init_malloc(int, int);
	
	double start = get_time_ms();
	
	for (size_t i = 0; i < num_ops; i++)
		st_insert_t(&map, int, int, (int)i, (int)(i * 2));
	
	double end = get_time_ms();
	double elapsed = end - start;
	
	info("Inserted %zu entries", map.size);
	info("Time: %.2f ms", elapsed);
	info("Ops/sec: %.0f", (num_ops / elapsed) * 1000.0);
	success("%.2f ns per insert", (elapsed * 1000000.0) / num_ops);
	
	st_destroy(&map);
}

void benchmark_lookups(size_t num_entries, size_t num_lookups)
{
	printf("\n");
	printf(YLW "===========================================\n");
	printf("Benchmark: %zu LOOKUP operations\n", num_lookups);
	printf("(Table size: %zu entries)\n", num_entries);
	printf("===========================================\n" RESET);
	
	SwissTable map = st_init_malloc(int, int);
	st_reserve(&map, num_entries);
	
	for (size_t i = 0; i < num_entries; i++)
		st_insert_t(&map, int, int, (int)i, (int)(i * 2));
	
	info("Table populated with %zu entries", map.size);
	info("Capacity: %zu (load: %.1f%%)", map.capacity, 
		100.0 * map.size / map.capacity);
	
	double start = get_time_ms();
	
	volatile int sum = 0;
	for (size_t i = 0; i < num_lookups; i++)
	{
		int key = (int)(i % num_entries);
		int *val = st_get_t(&map, int, int, key);
		if (val)
			sum += *val;
	}
	
	double end = get_time_ms();
	double elapsed = end - start;
	
	info("Performed %zu lookups", num_lookups);
	info("Time: %.2f ms", elapsed);
	info("Ops/sec: %.0f", (num_lookups / elapsed) * 1000.0);
	success("%.2f ns per lookup", (elapsed * 1000000.0) / num_lookups);
	
	st_destroy(&map);
}

void benchmark_mixed(size_t num_ops)
{
	printf("\n");
	printf(YLW "===========================================\n");
	printf("Benchmark: %zu MIXED operations\n", num_ops);
	printf("(70%% lookup, 20%% insert, 10%% remove)\n");
	printf("===========================================\n" RESET);
	
	SwissTable map = st_init_malloc(int, int);
	
	for (size_t i = 0; i < num_ops / 4; i++)
		st_insert_t(&map, int, int, (int)i, (int)i);
	
	double start = get_time_ms();
	
	volatile int sum = 0;
	for (size_t i = 0; i < num_ops; i++)
	{
		int op = i % 10;
		int key = (int)(i % (num_ops / 2));
		
		if (op < 7)  // 70% lookup
		{
			int *val = st_get_t(&map, int, int, key);
			if (val)
				sum += *val;
		}
		else if (op < 9)  // 20% insert
			st_insert_t(&map, int, int, key, key * 2);
		else  // 10% remove
			st_remove(&map, &key);
	}
	
	double end = get_time_ms();
	double elapsed = end - start;
	
	info("Final table size: %zu", map.size);
	info("Time: %.2f ms", elapsed);
	info("Ops/sec: %.0f", (num_ops / elapsed) * 1000.0);
	success("%.2f ns per operation", (elapsed * 1000000.0) / num_ops);
	
	st_destroy(&map);
}

void benchmark_string_ops(size_t num_ops)
{
	printf("\n");
	printf(YLW "===========================================\n");
	printf("Benchmark: %zu STRING operations\n", num_ops);
	printf("===========================================\n" RESET);
	
	Arena arena = arena_init(PROT_READ | PROT_WRITE);
	SwissTable map = st_init_str_alloc(arena_allocator(&arena), int);
	st_reserve(&map, num_ops);
	
	char **keys = malloc(num_ops * sizeof(char*));
	for (size_t i = 0; i < num_ops; i++)
	{
		keys[i] = malloc(32);
		snprintf(keys[i], 32, "key_%zu_%zu", i, i * 12345);
	}
	
	double start = get_time_ms();
	
	for (size_t i = 0; i < num_ops; i++)
		st_insert_str(&map, keys[i], int, (int)i);
	
	double insert_time = get_time_ms() - start;
	
	start = get_time_ms();
	
	volatile int sum = 0;
	for (size_t i = 0; i < num_ops; i++)
	{
		int *val = st_get_str(&map, int, keys[i]);
		if (val)
			sum += *val;
	}
	
	double lookup_time = get_time_ms() - start;
	
	info("String inserts: %.2f ms (%.0f ops/sec)", 
		insert_time, (num_ops / insert_time) * 1000.0);
	info("String lookups: %.2f ms (%.0f ops/sec)", 
		lookup_time, (num_ops / lookup_time) * 1000.0);
	success("%.2f ns per string insert", (insert_time * 1000000.0) / num_ops);
	success("%.2f ns per string lookup", (lookup_time * 1000000.0) / num_ops);
	
	for (size_t i = 0; i < num_ops; i++)
		free(keys[i]);
	free(keys);
	
	arena_free(&arena);
}

void benchmark_collision_heavy(size_t num_ops)
{
	printf("\n");
	printf(YLW "===========================================\n");
	printf("Benchmark: COLLISION-HEAVY workload\n");
	printf("(Many keys with same H1 hash)\n");
	printf("===========================================\n" RESET);
	
	SwissTable map = st_init_malloc(int, int);
	
	double start = get_time_ms();
	
	// Insert keys that likely collide
	for (size_t i = 0; i < num_ops; i++)
	{
		// Multiply by a prime to create clustering
		int key = (int)(i * 31);
		st_insert_t(&map, int, int, key, (int)i);
	}
	
	double insert_time = get_time_ms() - start;
	
	// Lookup
	start = get_time_ms();
	
	volatile int sum = 0;
	for (size_t i = 0; i < num_ops; i++)
	{
		int key = (int)(i * 31);
		int *val = st_get_t(&map, int, int, key);
		if (val)
			sum += *val;
	}
	
	double lookup_time = get_time_ms() - start;
	
	info("Collision inserts: %.2f ms", insert_time);
	info("Collision lookups: %.2f ms", lookup_time);
	success("%.2f ns per collision lookup", (lookup_time * 1000000.0) / num_ops);
	
	st_destroy(&map);
}

int main(void)
{
	printf("\n" PRPL);
	printf("╔═══════════════════════════════════════╗\n");
	printf("║   Swiss Table Benchmark Suite         ║\n");
	printf("╚═══════════════════════════════════════╝\n" RESET);
	
#ifdef HAVE_SSE2
	printf(GRN "\n  Using SSE2 SIMD acceleration\n" RESET);
#elif HAVE_NEON
	printf(GRN "\n  Using NEON SIMD acceleration\n" RESET);
#else
	printf(YLW "\n  Using scalar fallback (no SIMD)\n" RESET);
#endif
	
	benchmark_inserts(100000);
	benchmark_lookups(10000, 1000000);
	benchmark_lookups(100000, 1000000);
	benchmark_mixed(100000);
	benchmark_string_ops(10000);
	benchmark_collision_heavy(10000);
	
	printf("\n" GRN);
	printf("╔═══════════════════════════════════════╗\n");
	printf("║   Benchmarks Complete!                ║\n");
	printf("╚═══════════════════════════════════════╝\n" RESET);
	
	return 0;
}
