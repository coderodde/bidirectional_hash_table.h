demo: *.c *h
	gcc *c -o demo -g -O3 -Wall -Werror -Wpedantic

valgrind: demo
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./demo

