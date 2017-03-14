.PHONY: clean

CFLAGS  := -Wall -g -D_GNU_SOURCE -O2
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := write_bw send_bw

all: ${APPS}

write_bw: write_bw.o get_clock.o
	${LD} -o $@ $^ ${LDLIBS}

send_bw: send_bw.o get_clock.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}

