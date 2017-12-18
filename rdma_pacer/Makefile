.PHONY: clean

CFLAGS  := -Wall -O3
LD      := gcc
LDLIBS  := ${LDLIBS} -lpthread -lrt -libverbs -lm

APPS    := pacer

all: ${APPS}

pacer: pingpong_utils.o pingpong.o get_clock.o prng.o massdal.o countmin.o p2.o monitor.o pacer.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
