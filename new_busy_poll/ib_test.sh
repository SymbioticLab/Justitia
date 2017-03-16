# WRITE SERVER: -O 0; READ -O 1; WRITE_IMM -O 2
./write_bw -F -e -s 1000000000 -n 1000 -O 2
./write_bw -F -e -s 1000000 -n 1000000 -O 2
./write_bw -F -e -s 1000000000 -n 1000 -O 2 -p 8888
./write_bw -F -e -s 1000000 -n 1000000 -O 2 -p 8888
# WRITE CLIENT:
./write_bw 192.168.0.28 -F -s 1000000000 -n 1000 -e -O 2 -o out_WIMM_1G_vs_WIMM_1G_A.txt > result_WIMM_1G_vs_WIMM_1G_A.txt
./write_bw 192.168.0.28 -F -s 1000000 -n 1000000 -e -O 2 -o out_WIMM_1M_vs_WIMM_1M_A.txt > result_WIMM_1M_vs_WIMM_1M_A.txt
./write_bw 192.168.0.28 -F -s 1000000000 -n 1000 -e -O 2 -p 8888 -o out_WIMM_1G_vs_WIMM_1G_A.txt > result_WIMM_1G_vs_WIMM_1G_A.txt
./write_bw 192.168.0.28 -F -s 1000000 -n 1000000 -e -O 2 -p 8888 -o out_WIMM_1M_vs_WIMM_1M_A.txt > result_WIMM_1M_vs_WIMM_1M_A.txt
# SEND SERVER
./send_bw -F -e -s 1000000000 -N -n 1000 -t 5
./send_bw -F -e -s 1000000 -N -n 1000000 -t 5
./send_bw -F -e -s 1000000000 -N -n 1000 -p 8888 -t 5
./send_bw -F -e -s 1000000 -N -n 1000000 -p 8888 -t 5
# SEND CLIENT
./send_bw 192.168.0.28 -F -N -s 1000000000 -n 1000 -e -o out_WIMM_1G_vs_SEND_1G_A.txt > result_WIMM_1G_vs_SEND_1G_A.txt
./send_bw 192.168.0.28 -F -N -s 1000000 -n 1000000 -e -o out_WIMM_1G_vs_SEND_1M_A.txt > result_WIMM_1G_vs_SEND_1M_A.txt
./send_bw 192.168.0.28 -F -N -s 1000000000 -n 1000 -e -p 8888 -o out_WIMM_1G_vs_SEND_1G_A.txt > result_WIMM_1G_vs_SEND_1G_A.txt
./send_bw 192.168.0.28 -F -N -s 1000000 -n 1000000 -e -p 8888 -o out_WIMM_1G_vs_SEND_1M_A.txt > result_WIMM_1G_vs_SEND_1M_A.txt
