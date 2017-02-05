// NOTE: This file should really be called rdma-serverside.c since this is used exclusively for the server in the
// single direction communication. I don't bother change the name so that I don't have to modify the Makefile.
// NOTENOTE: This version is then updated for the small MR reuse / flow control model
// NOTE*: This one is used to test scalability. memcpy will be turned off. Only one MR in server.
#include "rdma-common.h"
//#include <inttypes.h>

//extern long RDMA_BUFFER_SIZE;

int NUM_TASK = 10000;   // indicates how many RDMA_WRITEs in between each reg/dereg

struct message {
  enum {
    MSG_INFO,
    MSG_MR
//    MSG_DONE
  } type;

  union {
    int num_wr;   // used when sending MSG_INFO by RDMA_WRITE_WITH_IMM; IMM field carries total data SIZE
    struct ibv_mr mr;
  } data;
}; // although it might be obvious, keeping the size of the struct msg in both side equal is necessary

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
  int num_chunk;
  int chunks_received;
  int task_done;    // number of tasks done
  int num_task;
  
  struct ibv_mr *recv_mr;
  struct ibv_mr *send_mr;
//  struct ibv_mr *rdma_local_mr;
  struct ibv_mr *rdma_remote_mr;

//  struct ibv_mr peer_mr;

  struct message *recv_msg;
  struct message *send_msg;

//  char *rdma_local_region;
  char *rdma_remote_region;   // used to hold the data for remote access by the client
  
  char *local_storage;        // used to hold the data copied from the MR
  
  /*
  enum {
    SS_INIT,
    SS_MR_SENT,
    SS_RDMA_SENT,
    SS_DONE_SENT
  } send_state;  // not needed. we'll just do 1 send req -- send the MR_key to the client
  */
  
//  enum {
//    RS_INIT,
//    RS_SZ_RECV,
//    RS_DONE_RECV
//  } recv_state;
  
};

static void build_context(struct ibv_context *verbs);
static void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
//static char * get_peer_message_region(struct connection *conn);
static void on_completion(struct ibv_wc *);
static void * poll_cq(void *);
static void post_receives_INFO(struct connection *conn);
static void post_receives_WRITE_IMM(struct connection *conn);
static void register_memory(struct connection *conn);
static void register_mem_for_remote(struct connection *conn, long buffer_size, long mr_size);
static void send_message(struct connection *conn);
static void send_done_message(struct connection *conn);
static void write_next_chunk(struct connection *conn, int i, long mr_size);

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
//  conn->recv_state = RS_INIT;

  conn->connected = 0;
  
  conn->num_chunk = 0;
  
  conn->chunks_received = 0;
  
  conn->task_done = 0;

  register_memory(conn);
  post_receives_INFO(conn);
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

  qp_attr->cap.max_send_wr = 10240;
  qp_attr->cap.max_recv_wr = 10240;
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
  free(conn->local_storage);

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

//  printf("E: ");
  
  if (wc->status != IBV_WC_SUCCESS) {
//    printf("error wc status #: %d\n", wc->status);
    printf("error wc status: %s\n", ibv_wc_status_str(wc->status));
    die("on_completion: status is not IBV_WC_SUCCESS.");
  }

  if (wc->opcode == IBV_WC_RECV) { // when receive info msg (SEND_WITH_IMM)
//    printf("IBV_WC_RECV\n");
    memcpy(&conn->num_chunk, &conn->recv_msg->data.num_wr, sizeof(int));
//    printf("num_chunk: %i\n", conn->num_chunk);
    long buffer_size = ntohl(wc->imm_data);
    long mr_size = buffer_size/conn->num_chunk;
    printf("Received Info message. Going to receive a total %lu Bytes of data in %u chunk(s).\n", buffer_size, conn->num_chunk);
    
    register_mem_for_remote(conn, buffer_size, mr_size);
    printf("Registered memory region of size(%lu)\n", mr_size);
    
    printf("Sending MSG_MR to the client...\n");
    send_mr(conn);
    post_receives_WRITE_IMM(conn);  // prepare for the first chunk's RDMA_WRITE_WITH_IMM
  } else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
//    printf("IBV_WC_RECV_RDMA_WITH_IMM\n");

//    printf("imm_data: %u\n", ntohl(wc->imm_data));
//    long size = ntohl(wc->imm_data);
//    memcpy(conn->local_storage, conn->rdma_remote_region, ntohl(wc->imm_data)); ////NOTE: fix later
    
    write_next_chunk(conn, conn->chunks_received, ntohl(wc->imm_data));  // fixed
    
    conn->chunks_received++;
//    printf("conn->chunks_received: %i; conn->num_chunk: %i\n", conn->chunks_received, conn->num_chunk);
//    printf("Finished copying data chunk #%d of size(%u) from MR to local storage.\n", conn->chunks_received, ntohl(wc->imm_data));
    
    if (conn->chunks_received < conn->num_chunk) {  // if it's not the last chunk
      post_receives_WRITE_IMM(conn);    // rearm for the next chunk's RDMA_WRITE_WITH_IMM
    } else if (conn->chunks_received == conn->num_chunk) {
      conn->chunks_received = 0;
      conn->task_done++;
      if (conn->task_done % 100 == 0) {
        printf("task #%i done\n", conn->task_done);
      }
      
      if (conn->task_done < NUM_TASK) {
        post_receives_WRITE_IMM(conn);    // rearm for the next chunk's RDMA_WRITE_WITH_IMM
      }
    }

//    printf("Sending ready msg to the client...\n");
    send_done_message(conn);    // send ready(done) msg
 
  }
  else if (wc->opcode == IBV_WC_RDMA_WRITE) {
//    printf("IBV_WC_RDMA_WRITE\n");
    if (conn->task_done == NUM_TASK) {
      printf("All data copied to local storage. Server is ready to disconnect.\n");
      rdma_disconnect(conn->id);
      printf("disconnected.\n");
    }
  
  }
//  else if (wc->opcode == IBV_WC_SEND) { printf("IBV_WC_SEND\n"); }
//  else { printf("else\n"); }
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

void post_receives_INFO(struct connection *conn)
{ // used to catch INFO msg from client
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

void post_receives_WRITE_IMM(struct connection *conn)
{ // used to receive RDMA WRITE WITH IMM wr; i think we can treat it as a zero-byte msg
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
  conn->recv_msg = malloc(sizeof(struct message));  // used for MSG_INFO
  

//  conn->rdma_local_region = malloc(RDMA_BUFFER_SIZE);
//  conn->rdma_remote_region = malloc(RDMA_BUFFER_SIZE);

  TEST_Z(conn->send_mr = ibv_reg_mr(
    s_ctx->pd, 
    conn->send_msg,
    sizeof(struct message), 
    IBV_ACCESS_LOCAL_WRITE));

  // since the info message sent by the client is no longer zero-byte msg, do need to reg mem
  TEST_Z(conn->recv_mr = ibv_reg_mr(
    s_ctx->pd, 
    conn->recv_msg, 
    sizeof(struct message), 
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ));
  
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

void register_mem_for_remote(struct connection *conn, long buffer_size, long mr_size) { // used to reg mem for client's RDMA read/write
  conn->rdma_remote_region = malloc(mr_size);
  conn->local_storage = malloc(buffer_size);
  
  TEST_Z(conn->rdma_remote_mr = ibv_reg_mr(
    s_ctx->pd,
    conn->rdma_remote_region,
    mr_size,
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

void write_next_chunk(struct connection *conn, int i, long mr_size) {
//  printf("Copying chunk #%i to local storage...\n", i+1);
//  memcpy((conn->local_storage + mr_size * i), conn->rdma_remote_region, mr_size);
}

//void set_mode(enum mode m)
//{
//  s_mode = m;
//}
