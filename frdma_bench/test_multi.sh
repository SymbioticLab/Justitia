#!/bin/bash
CNT=0
SIZE=$1
ITER=$2
NUM_FLOW=$3
PORT=8888
while [ $CNT -lt $NUM_FLOW ]; do
	./ib_write_bw -e -F -s $SIZE -n $ITER -t 1 -l 16 -p $(( $PORT + $CNT )) $4 &
	#if [ -z "$4" ]; then
	#	./ib_write_bw -e -F -s $SIZE -n $ITER -t 1 -l 1 -p $(($PORT + $CNT)) &
	#else
	#	./ib_write_bw -e -F -s $SIZE -n $ITER -t 1 -l 1 -p $(( $PORT + $CNT )) $4 &
	#fi
	CNT=$(( $CNT + 1 ))
done
#./test_multi.sh 100000 100000 3 192.168.0.28
#./ib_write_bw -e -F -s 1000000 -n 100000 -t 1 -l 1 192.168.0.28 &
#./ib_write_bw -e -F -s 1000000 -n 100000 -t 1 -l 1 192.168.0.28 -p 8888 &
#./ib_write_bw -e -F -s 1000000 -n 100000 -t 1 -l 1 192.168.0.28 -p 9999 &
#./ib_write_bw -e -F -s 1000000 -n 100000 -t 1 -l 1 192.168.0.28 -p 11111 &
#./ib_write_bw -e -F -s 1000000 -n 100000 -t 1 -l 1 192.168.0.28 -p 22222 &
#./ib_write_bw -e -F -s 1000000 -n 100000 -t 1 -l 1 192.168.0.28 -p 33333