#./write_bw 192.168.0.28 -F -s 1000000 -n 1000000 -w 10 -p 8888 -O 0 -e -o 1M_vs_1M_10_B.txt
#./write_bw 192.168.0.28 -F -s 1000000 -n 1000000 -w 1000 -p 8888 -O 0 -e -o 1M_vs_1M_1000_B.txt
#./write_bw 192.168.0.28 -F -s 1000000000 -n 1000 -w 10000 -p 8888 -O 0 -e -o 1M_vs_1G_10000_B.txt
#./write_bw 192.168.0.28 -F -s 1000000000 -n 1000 -w 1000 -p 8888 -O 0 -e -o 1M_vs_1G_1000_B.txt
#./write_bw 192.168.0.28 -F -s 1000000 -n 1000000 -p 8888 -O 0 -e -o 1G_1000_vs_1M_B.txt
#./write_bw 192.168.0.28 -F -s 1000000 -n 1000000 -p 8888 -t 1000 -O 0 -e -o new_1G_vs_1M_t1000_B.txt
#./write_bw 192.168.0.28 -F -s 1000000 -n 1000000 -p 8888 -t 1000 -O 0 -e -o new_1M_vs_1M_t1000_B.txt
./write_bw 192.168.0.28 -F -s 1000000 -n 1000000 -p 8888 -O 0 -e -o new_1G_split_vs_1M_t1_B.txt
