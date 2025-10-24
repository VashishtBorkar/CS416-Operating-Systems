CC = gcc
CFLAGS = -g -c
AR = ar -rc
RANLIB = ranlib

all: clean thread-worker.a

thread-worker.a: thread-worker.o datastructures.o
	$(AR) libthread-worker.a datastructures.o thread-worker.o
	$(RANLIB) libthread-worker.a

datastructures.o: datastructures.c datastructures.h
	$(CC) $(CFLAGS) datastructures.c

thread-worker.o: thread-worker.h datastructures.h

ifeq ($(SCHED), PSJF)
	$(CC) -pthread $(CFLAGS) -DPSJF thread-worker.c
else ifeq ($(SCHED), MLFQ)
	$(CC) -pthread $(CFLAGS) -DMLFQ thread-worker.c
else ifeq ($(SCHED), CFS)
	$(CC) -pthread $(CFLAGS) -DCFS thread-worker.c
else ifeq ($(SCHED), RR)
	$(CC) -pthread $(CFLAGS) -DRR thread-worker.c
else
	echo "no such scheduling algorithm."
endif

clean:
	rm -rf testfile *.o *.a
