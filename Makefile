CC = gcc

mfs: mfs.c
	${CC}${CFLAG} -Wall -Werror --std=c99 -o mfs mfs.c

clean:
	rm mfs 