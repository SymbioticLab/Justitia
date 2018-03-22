#ifndef HOST_H
#define HOST_H

enum host_request_type {
    QUERY_FLOW_JOIN = 0,
    QUERY_FLOW_EXIT = 1
}

struct host_request {                       /* request sent from host pacer */
    enum host_request_type req_type;
    uint8_t dest_qp_num;
    uint8_t is_read;
    uint32_t request_id;                    /* TODO: handle overflow later */
};

struct host_info {
    struct host_request host_req;           /* the *MR* for each host to update info via specific request using an RDMA verb*/ 
    // some other info go here...
    uint16_t *flow_map;                     /* an array keeping track of flows sending to other host in the cluster */
    struct pingpong_context *ctx;           /* other rdma related ctx goes here */
};


#endif