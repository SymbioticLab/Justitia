#!/usr/bin/python2.7
import sys
with open(sys.argv[1]) as f1:
	lines1 = f1.read().splitlines()
l1 = [float(i) for i in lines1]
print "avg msg/s: "
print reduce(lambda x, y: x + y, l1) / len(l1)
print "aggr msg/s: "
print reduce(lambda x, y: x + y, l1)

#with open(sys.argv[2]) as f2:
#	lines2 = f2.read().splitlines()
#l2 = [float(i) for i in lines2]
#print "avg median latency: "
#print reduce(lambda x, y: x + y, l2) / len(l2)

