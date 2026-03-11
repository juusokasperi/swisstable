NAME = tester

CFLAGS = -Wall -Wextra -Werror -g -fsanitize=address -O3
CC = cc


all: $(NAME)

$(NAME): main.c
	$(CC) $(CFLAGS) main.c -o $(NAME) 

benchmark:
	$(CC) $(CFLAGS) benchmark.c -o bench_neon -DHAVE_NEON -march=armv8-a
	$(CC) $(CFLAGS) benchmark.c -o bench_scalar -DFORCE_SCALAR
	@./bench_neon > results_neon.txt
	@./bench_scalar > results_scalar.txt
	@echo "\033[1;93m=== SCALAR ===\033[0m" && grep "ns per" results_scalar.txt
	@echo "\033[1;93m=== NEON ===\033[0m" && grep "ns per" results_neon.txt

clean: fclean

fclean:
	rm $(NAME) bench_neon bench_scalar results_neon.txt results_scalar.txt


re: fclean all
