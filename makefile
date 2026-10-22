CC = gcc
CFLAGS = -g -std=gnu11 -Werror -Wall -Wextra -Wpedantic -Wmissing-declarations -Wmissing-prototypes -Wold-style-definition
 

mdu: mdu.o
	$(CC) $(CFLAGS) -pthread -o mdu mdu.o
mdu.o: mdu.c
	$(CC) $(CFLAGS) -c mdu.c

mduCompetition: mduCompetition.o
	$(CC) $(CFLAGS) -Ofast -pthread -o mduCompetition mduCompetition.o

mduCompetition.o: mduCompetition.c
	$(CC) $(CFLAGS) -Ofast -c mduCompetition.c 