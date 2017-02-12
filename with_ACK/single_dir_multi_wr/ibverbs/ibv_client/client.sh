#rdma-client 
iters=100
log_file="w"${2}".log"
echo "build malloc mr_x post end" > $log_file
for ((i=1;i<=${iters};i++)); do
echo $i
./rdma-client write cp-2 $1 $2 >> $log_file
usleep 1000
done;
	
