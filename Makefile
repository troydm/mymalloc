all: mymalloc mymemsim mysmemsim sysmemsim genrandms

CC=cc
LD=ld
LD_FLAGS=-lpthread
CC_FLAGS=-std=gnu99 -O1 -Wall -I. -g

%.o: %.c
	$(CC) -c $< $(CC_FLAGS)

genrandms: genrandms.o
	$(CC) -o $@ $^ $(LD_FLAGS) 

mymalloc: main.o mymalloc.o
	$(CC) -o $@ $^ $(LD_FLAGS) 

mysmalloc: main.o mysmalloc.o
	$(CC) -o $@ $^ $(LD_FLAGS) 

mymemsim: mymemsim.o mymalloc.o libmemsim.o
	$(CC) -o $@ $^ $(LD_FLAGS) 

mysmemsim: mymemsim.o mysmalloc.o libmemsim.o
	$(CC) -o $@ $^ $(LD_FLAGS) 

sysmemsim: sysmemsim.o libmemsim.o
	$(CC) -o $@ $^ $(LD_FLAGS) 

clean:
	rm -f *.o
	rm -f mymemsim
	rm -f mysmemsim
	rm -f sysmemsim
	rm -f mymalloc
	rm -f genrandms

