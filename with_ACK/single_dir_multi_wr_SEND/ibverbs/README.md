This program tests RDMA(based on IB_verbs) performance.
Souce code is modified from "The Geek in the corner" 02-read-write sample.
Modification:
1.Server post no operations.
2.Procedures in client is divided into : 
	build connection/context, (include memory malloc time)
	memory resigster exchange.
	post operation, (including finishing)
	clean up, exchanging MSG_DON(receive can be before post operation)

