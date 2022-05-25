# Justitia

This repository contains Justitia code and instructions for building and running it.
For more details of Justitia design, please refer to our NSDI '22 paper "[Justitia: Software Multi-Tenancy in Hardware Kernel-Bypass Networks](https://yiwenzhang92.github.io/assets/docs/justitia-nsdi22.pdf)".


# Overview

* [Prerequisite](#prerequisite)
* [Build Justitia](#build-justitia)
* [Run Justitia](#run-justitia)
* [Acknowledgements](#acknowledgements)
* [Contact](#contact)

# Prerequisite

Justitia is built based on MLNX driver prior to version 5.0. So make sure a MLNX driver v4.x is installed.

# Build Justitia

Before building Justitia, confirm and adjust the following parameters based on you network settings:

* ```LINE_RATE_MB``` in ```rdma_pacer/pacer.h``` to match your NIC speed
* ```ib_dev_idx``` in ```rdma_pacer/pingpong.c``` to match your NIC device index

Depending on the actual RDMA NIC you are using, choose the corresponding installation script (for libmlx4 or libmlx5). For ConnectX-3 NICs:

```
git clone https://github.com/SymbioticLab/Justitia
cd Justitia
bash setup_X3.sh 
cd rdma_pacer
make
```

Use ```setup_X4.sh``` for ConnectX-4 and ConnectX-5 devices.
If the build is successful, the new drivers should be installed under /usr/lib64.

# Run Justitia

To launch Justitia, assuming a sender node (IP: 192.168.0.11) and a receiver node (IP: 192.168.0.12):

Launch the receiver-side Justiita first on the receiver node:

```
cd Justitia/rdma_pacer
./pacer 0 192.168.0.12 1
```

On the sender node:

```
cd Justitia/rdma_pacer
./pacer 1 192.168.0.12 1
```

Note Justitia pacer needs to be run with unmodified driver so it does not get identified as a user application itself.

Justitia supports multiple senders (for an incast scenario). Launch the server with the last parameter set to the number of senders, and then start the sender Justitia instances.
In case of RoCE, add the GID index as an additional input parameter at the end to the pacer binary.

## Run An Example
Here we use perftest as an example. Remember to select the new drivers we built (as shown below) such that the applcations are under Justitia's control.

On the receiver node, possibly in another set of terminal windows:

```
cd Justitia/perftest-4.2
export LD_LIBRARY_PATH=/usr/lib64:$LD_LIBRARY_PATH
./ib_write_bw -F -s 1000000 -n 500000 -l 1 -t 1 -p 8888 &
./ib_write_lat -F -s 16 -n 10000000 -l 1 -t 1 -p 8889 &
```

Then on the sender node:
```
cd Justitia/perftest-4.2
export LD_LIBRARY_PATH=/usr/lib64:$LD_LIBRARY_PATH
./ib_write_bw -F -s 1000000 -n 500000 -l 1 -t 1 -p 8888 192.168.0.12 &
./ib_write_lat -F -s 16 -n 10000000 -l 1 -t 1 --log_off -p 8889 192.168.0.12 &
```

Adjust the number of iterations accordingly based on the link speed in use.

# Reference
Please consider citing our paper if you find Justitia related to your research project.
```bibtex
@inproceedings{justitia-nsdi22,
  title={Justitia: Software Multi-Tenancy in Hardware Kernel-Bypass Networks},
  author={Yiwen Zhang and Yue Tan and Brent Stephens and Mosharaf Chowdhury},
  booktitle={NSDI},
  year={2022}
}
```

# Contact
Yiwen Zhang (yiwenzhg@umich.edu)


