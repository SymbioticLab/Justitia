.PHONY: clean

CFLAGS  := -Wall -O3
#CFLAGS  := -Wall -O0 -g
LD      := gcc
LDLIBS  := ${LDLIBS} -lpthread -lrt -libverbs -lm

APPS    := arbiter pacer

all: ${APPS}

arbiter: pingpong_utils.o pingpong.o get_clock.o queue.o massdal.o prng.o countmin.o arbiter.o vector.o priority_queue.o
	${LD} -o $@ $^ ${LDLIBS}

pacer: pingpong_utils.o pingpong.o get_clock.o queue.o massdal.o prng.o countmin.o monitor.o ringbuf.o pacer.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
