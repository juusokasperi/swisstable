NAME = tester

CFLAGS = -Wall -Wextra -Werror -g -fsanitize=address -O2
CC = cc


all: $(NAME)

$(NAME): main.c
	$(CC) $(CFLAGS) main.c -o $(NAME) 

clean: fclean

fclean:
	rm $(NAME)

re: fclean all
