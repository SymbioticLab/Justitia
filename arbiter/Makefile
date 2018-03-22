.PHONY: clean

CFLAGS  := -Wall -O3
LD      := gcc
LDLIBS  := ${LDLIBS} -lpthread -lrt -libverbs -lm

APPS    := arbiter

all: ${APPS}

arbiter: pingpong_utils.o pingpong.o get_clock.o queue.o massdal.o prng.o countmin.o arbiter.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
