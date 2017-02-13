// NOTE: This file should really be called rdma-clientside.c since this is used exclusively for the client in the
// single direction communication. I don't bother change the name so that I don't have to modify the Makefile.
// NOTE: This version is used to test the performance of multi wr
// NOTE: no ACK version for RDMA WRITE/READ
#include "rdma-common.h"
#include <inttypes.h>

extern long RDMA_BUFFER_SIZE;
extern int NUM_WR;
struct timeval tv[10];
int NUM_TASK = 1000;

struct message {
  enum {
    MSG_MR,
    MSG_DONE,
    TASK_DONE
  } type;

  union {
    struct ibv_mr mr;
  } data;
};

struct size_info {
  long data_size;
};

struct context {
  struct ibv_context *ctx;
  struct ibv_pd *pd;
  struct ibv_cq *cq;
  struct ibv_comp_channel *comp_channel;

  pthread_t cq_poller_thread;
};

struct connection {
  struct rdma_cm_id *id;
  struct ibv_qp *qp;

  int connected;
  
  int mr_ready;

  struct ibv_mr *recv_mr;
  struct ibv_mr *send_mr;  
  struct ibv_mr *rdma_local_mr;
//  struct ibv_mr *rdma_remote_mr;  // again not used

  struct ibv_mr peer_mr;

  struct message *recv_msg;
  struct message *send_msg;

  char *rdma_local_region;
//  char *rdma_remote_region;  //// NOTE: I don't think the remote region is of any use in this case; may delete later
  
  enum {
    SS_INIT,
    SS_SZ_SENT,
    SS_RDMA_SENT,
    SS_DONE_SENT
  } send_state;
  
  enum {
    RS_INIT,
    RS_MR_RECV
  } recv_state;

  int task_done;

};

static void build_context(struct ibv_context *verbs);
static void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
//static char * get_peer_message_region(struct connection *conn);
static void on_completion(struct ibv_wc *);
static void * poll_cq(void *);
static void post_receives(struct connection *conn);
static void register_memory(struct connection *conn);
static void register_memory_local(struct connection *conn);
//static void send_onetaskdone_msg(struct connection *conn);
static void send_done_message(struct connection *conn);
static void send_size_message(struct connection *conn);
static void perform_rdma_op(struct connection *conn);
static void post_done_receives(struct connection *conn);
//static void send_message(struct connection *conn);

static struct context *s_ctx = NULL;
static enum mode s_mode = M_WRITE;

void die(const char *reason)
{
  fprintf(stderr, "%s\n", reason);
  exit(EXIT_FAILURE);
}

void build_connection(struct rdma_cm_id *id)
{
  struct connection *conn;
  struct ibv_qp_init_attr qp_attr;

  build_context(id->verbs);
  build_qp_attr(&qp_attr);

  TEST_NZ(rdma_create_qp(id, s_ctx->pd, &qp_attr));

  id->context = conn = (struct connection *)malloc(sizeof(struct connection));

  conn->id = id;
  conn->qp = id->qp;

  conn->send_state = SS_INIT;
  conn->recv_state = RS_INIT;

  conn->task_done = 0;
  
  conn->connected = 0;
  
  conn->mr_ready = 0;
  
  register_memory(conn);
  post_receives(conn);
//  printf("after post receive at build_connection\n");
}

void build_context(struct ibv_context *verbs)
{
  if (s_ctx) {
    if (s_ctx->ctx != verbs)
      die("cannot handle events in more than one context.");

    return;
  }

  s_ctx = (struct context *)malloc(sizeof(struct context));

  s_ctx->ctx = verbs;

  TEST_Z(s_ctx->pd = ibv_alloc_pd(s_ctx->ctx));
  TEST_Z(s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ctx));
  TEST_Z(s_ctx->cq = ibv_create_cq(s_ctx->ctx, 10, NULL, s_ctx->comp_channel, 0)); /* cqe=10 is arbitrary */
  TEST_NZ(ibv_req_notify_cq(s_ctx->cq, 0));

  TEST_NZ(pthread_create(&s_ctx->cq_poller_thread, NULL, poll_cq, NULL));
}

void build_params(struct rdma_conn_param *params)
{
  memset(params, 0, sizeof(*params));

  params->initiator_depth = params->responder_resources = 1;
  params->rnr_retry_count = 7; /* infinite retry */
}

void build_qp_attr(struct ibv_qp_init_attr *qp_attr)
{
  memset(qp_attr, 0, sizeof(*qp_attr));

  qp_attr->send_cq = s_ctx->cq;
  qp_attr->recv_cq = s_ctx->cq;
  qp_attr->qp_type = IBV_QPT_RC;

  qp_attr->cap.max_send_wr = 15000;
  qp_attr->cap.max_recv_wr = 15000;
  qp_attr->cap.max_send_sge = 1;
  qp_attr->cap.max_recv_sge = 1;
}

void destroy_connection(void *context)
{
  struct connection *conn = (struct connection *)context;

  rdma_destroy_qp(conn->id);

  ibv_dereg_mr(conn->send_mr);
  ibv_dereg_mr(conn->recv_mr);
  ibv_dereg_mr(conn->rdma_local_mr);
//  ibv_dereg_mr(conn->rdma_remote_mr);

  free(conn->send_msg);
  free(conn->recv_msg);
  free(conn->rdma_local_region);
//  free(conn->rdma_remote_region);

  rdma_destroy_id(conn->id);

  free(conn);
}

//void * get_local_message_region(void *context)  // this function doesn't get used anywhere btw
//{
//  if (s_mode == M_WRITE)
//    return ((struct connection *)context)->rdma_local_region;
//  else
//    return ((struct connection *)context)->rdma_remote_region;
//}

//char * get_peer_message_region(struct connection *conn)
//{
//  if (s_mode == M_WRITE)
//    return conn->rdma_remote_region;
//  else
//    return conn->rdma_local_region;
//}

void on_completion(struct ibv_wc *wc)
{
  struct connection *conn = (struct connection *)(uintptr_t)wc->wr_id;

  if (wc->status != IBV_WC_SUCCESS) {
    printf("error wc status: %s\n", ibv_wc_status_str(wc->status));
    die("on_completion: status is not IBV_WC_SUCCESS.");
  }
//    if (RDMA_BUFFER_SIZE >= 100) {
//        printf("'''passed in client common.c\n");
//    }
//  printf("send state: %d\n", conn->send_state);
//  printf("recv state: %d\n", conn->recv_state);

  if (wc->opcode == IBV_WC_RECV) { // the sample code online used bitwise & here. I'll just use == to be safe. I guess the reason bitwise & can work is that the opcode enums are probably bitwised(one-hot)
//    printf("1 wr in receive queue completed with success\n");
    conn->recv_state++;
    
    if (conn->recv_msg->type == MSG_MR) {
      
      memcpy(&conn->peer_mr, &conn->recv_msg->data.mr, sizeof(conn->peer_mr));
      printf("Client received and copied server's MR key. ");

      while (!conn->mr_ready);
      
      if (s_mode == M_WRITE)
        printf("Posting RDMA WRITE work request.\n");
      else
        printf("Posting RDMA READ work request.\n");
      
      checkpoint(3);
      post_done_receives(conn);
      perform_rdma_op(conn);
      
    } else {
      die("on_completion: received msg is not MSR_MR.\n");
    }

  } else if ((wc->opcode == IBV_WC_RDMA_WRITE) || (wc->opcode == IBV_WC_RDMA_READ)) {
//    printf("1 wr in send queue completed with success\n");
    if (conn->send_state == SS_INIT) {
      conn->send_state++;
    }
    else if (conn->send_state == SS_SZ_SENT) {
      if (conn->task_done == NUM_TASK) {
        send_done_message(conn);
        printf("sent done message to the server.\n");
      } else { // NO ACK
        perform_rdma_op(conn);  
        //printf("conn->task_done = %d\n", conn->task_done);
        //send_onetaskdone_msg(conn);
      }
    } 

    /*
    if (conn->send_state == SS_RDMA_SENT && conn->recv_state == RS_MR_RECV) { // this prevents sending done and disconnect again after we receive the done msg wc successfully. As you may have noticed, checking RS_MR_RECV is actually not needed in this simple situation. I put it there for a more clear look and a debugger for future code modification
      checkpoint(4);
      if (s_mode == M_WRITE)
        printf("RDMA write operation completed with success.\n");
      else
        printf("RDMA read operation completed with success.\n");
      send_done_message(conn);
      printf("MSG_DONE sent successfully. Client is ready to disconnect.\n");
      rdma_disconnect(conn->id);
    }
    */
  }
  else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
    if (ntohl(wc->imm_data) != 0x0083) {
      printf("imm_data: %x\n", ntohl(wc->imm_data));
      die("on_completion: the IMM MSG received from server might not be DONE MSG. Check if the input data size set is set to be 131\n");
    }
    // receive the cp done msg from server and perform the next RDMA op 
    if (conn->task_done != NUM_TASK) {
      // shouldn't enter here
      printf("received msg from server, conn->task_done = %d\n", conn->task_done);
      //post_done_receives(conn);
      //perform_rdma_op(conn);
      //send_onetaskdone_msg(conn);
    } else {
      conn->send_state++;
      checkpoint(4);
      if (s_mode == M_WRITE)
        printf("RDMA write operation completed with success.\n");
      else
        printf("RDMA read operation completed with success.\n");
      //send_done_message(conn);
      printf("MSG_DONE sent successfully. Client is ready to disconnect.\n");
      rdma_disconnect(conn->id);
    }

  }

}

void on_connect(void *context)
{
  ((struct connection *)context)->connected = 1;
}

void * poll_cq(void *ctx)
{
  struct ibv_cq *cq;
  struct ibv_wc wc;

  while (1) {
    TEST_NZ(ibv_get_cq_event(s_ctx->comp_channel, &cq, &ctx));
    ibv_ack_cq_events(cq, 1);
    TEST_NZ(ibv_req_notify_cq(cq, 0));

    while (ibv_poll_cq(cq, 1, &wc))
      on_completion(&wc);
  }

  return NULL;
}

void post_receives(struct connection *conn)
{
  struct ibv_recv_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  wr.wr_id = (uintptr_t)conn;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  sge.addr = (uintptr_t)conn->recv_msg;
  sge.length = sizeof(struct message);
  sge.lkey = conn->recv_mr->lkey;

  TEST_NZ(ibv_post_recv(conn->qp, &wr, &bad_wr));
}

void post_done_receives(struct connection *conn)
{
  struct ibv_recv_wr wr, *bad_wr = NULL;

  wr.wr_id = (uintptr_t)conn;
  wr.next = NULL;
  wr.sg_list = NULL; //&sge;
  wr.num_sge = 0;

  TEST_NZ(ibv_post_recv(conn->qp, &wr, &bad_wr));
}

void register_memory(struct connection *conn)
{
  conn->send_msg = malloc(sizeof(struct message));
  conn->recv_msg = malloc(sizeof(struct message));

//  conn->rdma_local_region = malloc(RDMA_BUFFER_SIZE);  // moved to register_memory_local function
//  conn->rdma_remote_region = malloc(RDMA_BUFFER_SIZE);

 TEST_Z(conn->send_mr = ibv_reg_mr(
   s_ctx->pd, 
   conn->send_msg, 
   sizeof(struct message), 
   IBV_ACCESS_LOCAL_WRITE));

  TEST_Z(conn->recv_mr = ibv_reg_mr(
    s_ctx->pd, 
    conn->recv_msg, 
    sizeof(struct message), 
    IBV_ACCESS_LOCAL_WRITE)); // updated: does not need remote access actually in our case

//  TEST_Z(conn->rdma_local_mr = ibv_reg_mr(  // moved to register_memory_local function
//    s_ctx->pd, 
//    conn->rdma_local_region, 
//    RDMA_BUFFER_SIZE, 
//    IBV_ACCESS_LOCAL_WRITE));

//  TEST_Z(conn->rdma_remote_mr = ibv_reg_mr(
//    s_ctx->pd, 
//    conn->rdma_remote_region, 
//    RDMA_BUFFER_SIZE, 
//    IBV_ACCESS_LOCAL_WRITE | ((s_mode == M_WRITE) ? IBV_ACCESS_REMOTE_WRITE : IBV_ACCESS_REMOTE_READ)));
}

void register_memory_local(struct connection *conn) // reg MR for RDMA READ/WRITE to the server
{
  conn->rdma_local_region = malloc(RDMA_BUFFER_SIZE);
  
  TEST_Z(conn->rdma_local_mr = ibv_reg_mr(
    s_ctx->pd,
    conn->rdma_local_region,
    RDMA_BUFFER_SIZE,
    IBV_ACCESS_LOCAL_WRITE));
}

void register_MR(void *context)
{
  struct connection *conn = (struct connection *)context;
  register_memory_local(conn);
  conn->mr_ready = 1;
}

void perform_rdma_op(struct connection *conn)
{
  struct ibv_send_wr *wr_head = NULL, *current_wr = NULL, *new_wr = NULL, *bad_wr = NULL;
  struct ibv_sge sge;
  
  int i = 0;
  int num_unsignaled = NUM_WR - 1;
  for (i = 0; i < num_unsignaled; i++) {
    new_wr = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr));
    memset(new_wr, 0, sizeof(struct ibv_send_wr));
    
    new_wr->wr_id = (uintptr_t)conn;
    new_wr->opcode = (s_mode == M_WRITE) ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
    new_wr->sg_list = &sge;
    new_wr->num_sge = 1;
    new_wr->send_flags = 0; //covered by memset though
    new_wr->wr.rdma.remote_addr = (uintptr_t)(conn->peer_mr.addr + RDMA_BUFFER_SIZE/NUM_WR * i);
    new_wr->wr.rdma.rkey = conn->peer_mr.rkey;
    
    sge.addr = (uintptr_t)(conn->rdma_local_region + RDMA_BUFFER_SIZE/NUM_WR * i);
    sge.length = RDMA_BUFFER_SIZE/NUM_WR;
    sge.lkey = conn->rdma_local_mr->lkey;
//        printf("0x%" PRIXPTR "\n", sge.addr);
    if (wr_head == NULL) {
      wr_head = new_wr;
      current_wr = wr_head;
    }
    else {
      current_wr->next = new_wr;
      current_wr = current_wr->next;
    }
  }

  // Last one with flag SIGNALED
  new_wr = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr));
  memset(new_wr, 0, sizeof(struct ibv_send_wr));
  
  new_wr->wr_id = (uintptr_t)conn;
  new_wr->opcode = (s_mode == M_WRITE) ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
  new_wr->sg_list = &sge;
  new_wr->num_sge = 1;
  new_wr->send_flags = IBV_SEND_SIGNALED;
  new_wr->wr.rdma.remote_addr = (uintptr_t)(conn->peer_mr.addr + RDMA_BUFFER_SIZE/NUM_WR * i);
  new_wr->wr.rdma.rkey = conn->peer_mr.rkey;
  
  sge.addr = (uintptr_t)(conn->rdma_local_region + RDMA_BUFFER_SIZE/NUM_WR * i);
  sge.length = RDMA_BUFFER_SIZE/NUM_WR;
  sge.lkey = conn->rdma_local_mr->lkey;
//      printf("0x%" PRIXPTR "\n", sge.addr);
  if (i == 0) {
    wr_head = new_wr;
  }
  else {
    current_wr->next = new_wr;
    current_wr = current_wr->next;
    current_wr->next = NULL;
  }
  
  TEST_NZ(ibv_post_send(conn->qp, wr_head, &bad_wr));
  // finish sending rdma op
  conn->task_done++;
  //printf("done posting new send request (task #%d)\n", conn->task_done);
}
/*
void send_onetaskdone_msg(struct connection *conn)
{
  printf("sending one task done msg to server\n");
  struct ibv_send_wr wr, *bad_wr = NULL;
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = (uintptr_t)conn;
  wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  wr.imm_data = htonl(0x0038);
  wr.sg_list = NULL;
  wr.num_sge = 0;
  wr.send_flags = IBV_SEND_SIGNALED;
  
  while (!conn->connected); ////NOTE: test if needed later
  
  TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}
*/
/*
void send_onetaskdone_msg(struct connection *conn) {  // change to using rdma send
  
  struct ibv_send_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)conn;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = 0;

  conn->send_msg->type = TASK_DONE;

  sge.addr = (uintptr_t)conn->send_msg;
  sge.length = sizeof(struct message);
  sge.lkey = conn->send_mr->lkey;

  while (!conn->connected);

  TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}
*/
void send_done_message(struct connection *conn) {  // here a zero-byte message is enough to do the work; no need to reg extra mem

  struct ibv_send_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)conn;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = 0;

  conn->send_msg->type = MSG_DONE;

  sge.addr = (uintptr_t)conn->send_msg;
  sge.length = sizeof(struct message);
  sge.lkey = conn->send_mr->lkey;

  while (!conn->connected);

  TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}
/*
void send_done_message(struct connection *conn) {  // here a zero-byte message is enough to do the work; no need to reg extra mem
  struct ibv_send_wr wr, *bad_wr = NULL;
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = (uintptr_t)conn;
  wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  wr.imm_data = htonl(0x0083);
  wr.sg_list = NULL;
  wr.num_sge = 0;
  wr.send_flags = IBV_SEND_SIGNALED;
  
  while (!conn->connected); ////NOTE: test if needed later
  
  TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}
*/

void send_size_message(struct connection *conn) {
  struct ibv_send_wr wr, *bad_wr = NULL;
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = (uintptr_t)conn;
  wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  wr.imm_data = htonl(RDMA_BUFFER_SIZE);
  wr.sg_list = NULL;
  wr.num_sge = 0;
  wr.send_flags = IBV_SEND_SIGNALED;
  
  while (!conn->connected); ////NOTE: test if needed later
  
  TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
  
}

//void send_message(struct connection *conn)
//{
//  struct ibv_send_wr wr, *bad_wr = NULL;
//  struct ibv_sge sge;
//
//  memset(&wr, 0, sizeof(wr));
//
//  wr.wr_id = (uintptr_t)conn;
//  wr.opcode = IBV_WR_SEND;
//  wr.sg_list = &sge;
//  wr.num_sge = 1;
//  wr.send_flags = IBV_SEND_SIGNALED;
//
//  sge.addr = (uintptr_t)conn->send_msg;
//  sge.length = sizeof(struct message);
//  sge.lkey = conn->send_mr->lkey;
//
//  while (!conn->connected);
//
//  TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
//}

void send_sz(void *context) {
  struct connection *conn = (struct connection *)context;
  send_size_message(conn);
}

//void send_mr(void *context)
//{
//  struct connection *conn = (struct connection *)context;
//  
//  conn->send_msg->type = MSG_MR;
//  memcpy(&conn->send_msg->data.mr, conn->rdma_remote_mr, sizeof(struct ibv_mr));
//  
//  send_message(conn);
//}


void set_mode(enum mode m)
{
  s_mode = m;
}

void checkpoint(int i) {
  struct timezone tz;
  gettimeofday(tv+i, &tz);
}

void measure_time() {
  long period[8] = {0,0,0,0,0,0,0,0};
  // period[0] is the time used for build_connection();
  period[0] = (long)(tv[2].tv_sec * 1000000 + tv[2].tv_usec) - (long)(tv[1].tv_sec * 1000000 + tv[1].tv_usec);
  // period[1] is the time used for the set-up work before performing RDMA read/write. In other words, it includes initialize context & register memory regions, build connections, send sz msg, prepost recv, wait server to reg mr according to sz msg, and recv the mr_key msg from server
  period[1] = (long)(tv[3].tv_sec * 1000000 + tv[3].tv_usec) - (long)(tv[0].tv_sec * 1000000 + tv[0].tv_usec);
  // period[2] is the time used for RDMA read/write, the bulk of the work we are interested in.
  period[2] = (long)(tv[4].tv_sec * 1000000 + tv[4].tv_usec) - (long)(tv[3].tv_sec * 1000000 + tv[3].tv_usec);
  // period[3] is the time used after RDMA read/write is done and before disconnection is completed.
  period[3] = (long)(tv[5].tv_sec * 1000000 + tv[5].tv_usec) - (long)(tv[4].tv_sec * 1000000 + tv[4].tv_usec);
  // period[4] is the total time
  period[4] = (long)(tv[5].tv_sec * 1000000 + tv[5].tv_usec) - (long)(tv[0].tv_sec * 1000000 + tv[0].tv_usec);
  // period[5] is the time used to resolve addr
  period[5] = (long)(tv[7].tv_sec * 1000000 + tv[7].tv_usec) - (long)(tv[6].tv_sec * 1000000 + tv[6].tv_usec);
  // period[6] is the time used to resolve route
  period[6] = (long)(tv[8].tv_sec * 1000000 + tv[8].tv_usec) - (long)(tv[2].tv_sec * 1000000 + tv[2].tv_usec);
  // period[6] is the time to receive sever's MR after client sends the sz msg; in this version of code, client will start to reg MR after send the sz msg rather than reg MR during the build_connection(conn) function.
  period[7] = (long)(tv[3].tv_sec * 1000000 + tv[3].tv_usec) - (long)(tv[9].tv_sec * 1000000 + tv[9].tv_usec);
  
  printf("1.setup time  2.RDMA R/W latency  3.cleanup time  4.total time\n");
  printf("  %-14ld%-20ld%-16ld%ld\n", period[1],period[2],period[3],period[4]);
//  printf("5.adr_resolve 6.route_resolve     7.build_conn    8.get_sz_msg\n");
//  printf("  %-14ld%-20ld%-16ld%ld\n", period[5],period[6],period[0],period[7]);
}


