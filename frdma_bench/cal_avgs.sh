#!/bin/bash

grep "\[message per sec\]" $1 | awk 'NF>1{print $NF}' > temp_msgpersec
#grep -A 1 "\[message per sec\]" $1  | tail -n 1 > temp_median

python get_avg.py temp_msgpersec