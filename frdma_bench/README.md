About the benchmark
-----------

This benchmark is modified from the Mellanox pertest. The major change is in the way the sender posts work requests -- by default the next work request is posted only after the work completion of the previous work request is polled from the completion queue. CPU cycles since the program started is dumped after each work request is posted and completed, and computed after the data transfer ends, along with the difference of the two for latency logging. The way the RDMA connection is built between the client and the server is left unchanged.

Running the benchmark
-----------

Here is a short list of the command line arguments that are most important. You'll find most of them similar to the original perftest if you are familiar with the tool. All 4 RDMA operations are combined into one model. 

-F: used to ignore an error when measuring latency using cpu cycle. Always turn it on.
-O: RDMA op type. Write = 0, Read = 1, WRITE_IMM = 2, Send = 3.
-e: Turn on event-triggered polling. Poll CQ only when a work completion is generated. Default = busy polling.
-s: size of each message
-n: number of message to transfer
-p: port number. Default=18515
-o: specify the output file that all time info is dumped to
-t: size of message receiving pipeline. i.e., the receive requests pre-posted on the receiver side in SEND and WIMM

Now let's move onto how to run experiments.
For example, to compare 2 elephant flows with both using event-triggered polling:

On the receiver node:
```bash
./write_bw -F -e -s 1000000000 -n 1000 -O 0
./write_bw -F -e -s 1000000 -n 1000000 -O 0 -p 8888
```
On the sender node:
```bash
./write_bw [IP of receiver] -F -s 1000000000 -n 1000 -e -O 0 -o out_WRITE_1G_vs_WRITE_1M_A.txt > result_WRITE_1G_vs_WRITE_1M_A.txt
./write_bw [IP of receiver] -F -s 1000000 -n 1000000 -e -O 0 -p 8888 -o out_WRITE_1G_vs_WRITE_1M_B.txt > result_WRITE_1G_vs_WRITE_1M_B.txt
```

Since we need to start two RDMA instances at the same time, we need a way to synchronize them. There are multiple ways to do it. I use "at" command with scripts. 
For the receiver, once you type in the command, it will always wait for the sender(client). So start the server ahead of time, and then use "at" on the client side to run the prepared command inside a script at a specified future time.
```bash
sudo apt-get install at;
at XX:XX -f run1.sh
at XX:XX -f run2.sh
```
XX:XX is the time you want the script to run. Always use "date" to pick a time, since the time in the machine is not always the time shown on your local clock.
Note when using "at", you have to dump useful output to a file otherwise you won't see anything, which we've already done in the -o flag.

After the output is dumped, look into out_WRITE_1G_vs_WRITE_1M_A.txt and out_WRITE_1G_vs_WRITE_1M_B.txt. Find the one that ends earlier. Use the ending time to find how many pieces of messages the other one has finished sending at that time. Now we can calculate the bandwidth of both flows. You can write your own script to do this or do it manually -- I don't find it hard anyway.

For example, in out_WRITE_1G_vs_WRITE_1M_A.txt, I find it finished sending all data at 283344584 micro seconds. Then I go to out_WRITE_1G_vs_WRITE_1M_B.txt, search for the nearest timestamp and get 283344457, and figure out that the corresponding task # is 727326 (out of 1000000). 
Since the amount of the data transfer is large, the time difference in register the memory region for RDMA won't matter. To calculate bandwidth (in unit of Gbps(Gigabit per second))for each flow:
Flow A: 1e12 / 1e9 * 8  / (283344584 / 1e6) = 28.234 Gbps.
Flow B: 1e12 / 1e9 * (727326 / 1000000) * 8 / ( 283344457 / 1e6) = 20.535 Gbps.
