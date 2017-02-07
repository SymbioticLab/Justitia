// NOTE: This file should really be called rdma-serverside.c since this is used exclusively for the server in the
// single direction communication. I don't bother change the name so that I don't have to modify the Makefile.
#include "rdma-common.h"
//#include <inttypes.h>

//extern long RDMA_BUFFER_SIZE;

struct message {
  enum {
    MSG_MR,
    MSG_DONE,
    TASK_DONE
  } type;

  union {
    struct ibv_mr mr;
  } data;
}; // although it might be obvious, keeping the size of the struct msg in both side equal will be convient when reg mr for the other side


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

  int data_size;

  struct ibv_mr *recv_mr;
  struct ibv_mr *send_mr;
//  struct ibv_mr *rdma_local_mr;
  struct ibv_mr *rdma_remote_mr;

  struct ibv_mr peer_mr;

  struct message *recv_msg;
  struct message *send_msg;
  struct message *recv_data_msg;
//  char *rdma_local_region;
  char *rdma_remote_region;
  /*
  enum {
    SS_INIT,
    SS_MR_SENT,
    SS_RDMA_SENT,
    SS_DONE_SENT
  } send_state;  // not needed. we'll just do 1 send req -- send the MR_key to the client
  */
  
  enum {
    RS_INIT,
    RS_SZ_RECV,
    RS_MR_SENT,
    RS_DONE_RECV
  } recv_state;
  
};

static void build_context(struct ibv_context *verbs);
static void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
//static char * get_peer_message_region(struct connection *conn);
static void on_completion(struct ibv_wc *);
static void * poll_cq(void *);
//static void post_recv_send(struct connection *conn);
static void post_recv_data(struct connection *conn);
static void post_receives(struct connection *conn);
static void register_memory(struct connection *conn);
static void register_mem_for_remote(struct connection *conn, long buffer_size);
static void send_message(struct connection *conn);
static void send_done_message(struct connection *conn);

static struct context *s_ctx = NULL;
//static enum mode s_mode = M_WRITE;

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

//  conn->send_state = SS_INIT;
  conn->recv_state = RS_INIT;

  conn->connected = 0;

  register_memory(conn);
  post_receives(conn);
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

  qp_attr->cap.max_send_wr = 150;
  qp_attr->cap.max_recv_wr = 150;
  qp_attr->cap.max_send_sge = 1;
  qp_attr->cap.max_recv_sge = 1;
}

void destroy_connection(void *context)
{
  struct connection *conn = (struct connection *)context;

  rdma_destroy_qp(conn->id);

  ibv_dereg_mr(conn->send_mr);
  ibv_dereg_mr(conn->recv_mr);
//  ibv_dereg_mr(conn->rdma_local_mr);
  ibv_dereg_mr(conn->rdma_remote_mr);

  free(conn->send_msg);
  free(conn->recv_msg);
//  free(conn->rdma_local_region);
  free(conn->rdma_remote_region);

  rdma_destroy_id(conn->id);

  free(conn);
}

//void * get_local_message_region(void *context)
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
//    printf("error wc status #: %d\n", wc->status);
    printf("error wc status: %s\n", ibv_wc_status_str(wc->status));
    die("on_completion: status is not IBV_WC_SUCCESS.");
  }

  if (wc->opcode == IBV_WC_RECV) {
    if (conn->recv_state == RS_MR_SENT) {
    }
    if (conn->recv_msg->type == TASK_DONE) {
      /*
      printf("Receiving done msg from client. Ready to perform memcpy.\n");
      post_recv_send(conn);
      // Perform memcpy...
      send_done_message(conn);
      */
    } else if (conn->recv_msg->type == MSG_DONE) {
      send_done_message(conn);
      printf("Received done msg from client. Ready to disconnect.\n");
      rdma_disconnect(conn->id);
    } else {
      printf("Receiving data msg from client. Ready to perform memcpy.\n");
      post_recv_data(conn);
      // Perform memcpy...
      send_done_message(conn);
    }

  }

  if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
    //else if (conn->recv_state == RS_SZ_RECV) {
    if (conn->recv_state == RS_INIT) {
      conn->recv_state++;
      register_mem_for_remote(conn, ntohl(wc->imm_data));
      conn->data_size = ntohl(wc->imm_data);
      printf("Registered memory region for the specified data size(%u)\n", ntohl(wc->imm_data));
      
      printf("Sending MSG_MR to the client...\n");
      send_mr(conn);
      post_recv_data(conn);
      //post_receives(conn);  // rearm for done msg
      conn->recv_state++;
      
    }
    /*
    if (conn->recv_state == RS_DONE_RECV) {
      if (ntohl(wc->imm_data) != 0x0083) {
        printf("imm_data: %x\n", ntohl(wc->imm_data));
//        printf("wr_id: %" PRIu64 "\n", wc->wr_id);
        die("on_completion: the IMM MSG received from client might not be DONE MSG. Check if the input data size set is set to be 131\n");
      }

      // once received the done msg, ready to disconnect.
      printf("Received done msg from client. Ready to disconnect.\n");
      rdma_disconnect(conn->id);
      
    }
    */
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

void post_recv_data(struct connection *conn)
{
  struct ibv_recv_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  wr.wr_id = (uintptr_t)conn;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  sge.addr = (uintptr_t)conn->rdma_remote_region;
  sge.length = conn->data_size;
  sge.lkey = conn->rdma_remote_mr->lkey;

  //printf("sge.length = %d\n", sge.length);

  TEST_NZ(ibv_post_recv(conn->qp, &wr, &bad_wr)); 
}
/*
void post_recv_send(struct connection *conn)
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
*/
void post_receives(struct connection *conn)
{ // since we know we all msgs we are gonna receive are zero-byte msgs:
  struct ibv_recv_wr wr, *bad_wr = NULL;
//  struct ibv_sge sge;

  wr.wr_id = (uintptr_t)conn;
  wr.next = NULL;
  wr.sg_list = NULL; //&sge;
  wr.num_sge = 0;

  // these 3 sge params don't matter; they shouldn't be used anyway
//  sge.addr = 0; //(uintptr_t)conn->recv_msg;
//  sge.length = 1; //sizeof(struct message);
//  sge.lkey = 0; //conn->recv_mr->lkey;

  TEST_NZ(ibv_post_recv(conn->qp, &wr, &bad_wr));
}

void register_memory(struct connection *conn)
{
  conn->send_msg = malloc(sizeof(struct message));
  conn->recv_msg = malloc(sizeof(struct message));

//  conn->rdma_local_region = malloc(RDMA_BUFFER_SIZE);
//  conn->rdma_remote_region = malloc(RDMA_BUFFER_SIZE);

  TEST_Z(conn->send_mr = ibv_reg_mr(
    s_ctx->pd, 
    conn->send_msg, 
    sizeof(struct message), 
    IBV_ACCESS_LOCAL_WRITE));

  // since the done message sent by the client is a zero-byte msg, no need to reg mem
 TEST_Z(conn->recv_mr = ibv_reg_mr(
   s_ctx->pd, 
   conn->recv_msg, 
   sizeof(struct message), 
   IBV_ACCESS_LOCAL_WRITE));
  
  // since there will be no RDMA read/write operations performed by the server
//  TEST_Z(conn->rdma_local_mr = ibv_reg_mr(
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

void register_mem_for_remote(struct connection *conn, long buffer_size) { // used to reg mem for client's RDMA read/write
  conn->rdma_remote_region = malloc(buffer_size);
  
  TEST_Z(conn->rdma_remote_mr = ibv_reg_mr(
    s_ctx->pd,
    conn->rdma_remote_region,
    buffer_size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ));
}

void send_message(struct connection *conn)
{
  struct ibv_send_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)conn;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = IBV_SEND_SIGNALED;

  sge.addr = (uintptr_t)conn->send_msg;
  sge.length = sizeof(struct message);
  sge.lkey = conn->send_mr->lkey;

  while (!conn->connected);

  TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}

void send_mr(void *context)
{
  struct connection *conn = (struct connection *)context;

  conn->send_msg->type = MSG_MR;
  memcpy(&conn->send_msg->data.mr, conn->rdma_remote_mr, sizeof(struct ibv_mr));

  send_message(conn);
}

//void set_mode(enum mode m)
//{
//  s_mode = m;
//}
