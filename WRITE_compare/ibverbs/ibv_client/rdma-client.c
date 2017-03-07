// NOTE: This is a modified version of rdma-client.c for a single direction communication.
#include "rdma-common.h"

const int TIMEOUT_IN_MS = 500; /* ms */

#define Ntask 1000

static int on_addr_resolved(struct rdma_cm_id *id);
static int on_connection(struct rdma_cm_id *id);
static int on_disconnect(struct rdma_cm_id *id);
static int on_event(struct rdma_cm_event *event);
static int on_route_resolved(struct rdma_cm_id *id);
static void usage(const char *argv0);
long RDMA_BUFFER_SIZE;
int NUM_WR;
int NUM_TASK = Ntask;
long time_arr[Ntask];
long task_done_sofar[Ntask];
long lat_arr[Ntask];

int main(int argc, char **argv)
{
  struct addrinfo *addr;
  struct rdma_cm_event *event = NULL;
  struct rdma_cm_id *conn= NULL;
  struct rdma_event_channel *ec = NULL;

  
  if (argc != 7)
    usage(argv[0]);

  RDMA_BUFFER_SIZE = atol(argv[4]);
  NUM_WR = atoi(argv[5]);
  if (strcmp(argv[1], "write") == 0)
    set_mode(M_WRITE);
  else if (strcmp(argv[1], "read") == 0)
    set_mode(M_READ);
  else
    usage(argv[0]);

  checkpoint(0);
  TEST_NZ(getaddrinfo(argv[2], argv[3], NULL, &addr));

  TEST_Z(ec = rdma_create_event_channel());
  TEST_NZ(rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP));
  //checkpoint(6);
  TEST_NZ(rdma_resolve_addr(conn, NULL, addr->ai_addr, TIMEOUT_IN_MS));
  //checkpoint(7);
  freeaddrinfo(addr);

  
  while (rdma_get_cm_event(ec, &event) == 0) {
    struct rdma_cm_event event_copy;

    memcpy(&event_copy, event, sizeof(*event));
    rdma_ack_cm_event(event);

    if (on_event(&event_copy))
      break;
  }

  rdma_destroy_event_channel(ec);
  checkpoint(5);
  measure_time();
  FILE *f = fopen(argv[6], "w");
  fprintf(f, "\tTime(us)\tLatency\t\tTask_done\n");
  int i;
  for (i = 0; i < NUM_TASK; i++) {
    fprintf(f, "%d\t%ld\t\t%ld\t\t%ld\n", i, time_arr[i], lat_arr[i], task_done_sofar[i]);
  } 
  fclose(f);
  return 0;
}

int on_addr_resolved(struct rdma_cm_id *id)
{
  printf("address resolved.\n");
  checkpoint(1);
  build_connection(id);
  checkpoint(2);
//  sprintf(get_local_message_region(id->context), "message from active/client side with pid %d", getpid());
  TEST_NZ(rdma_resolve_route(id, TIMEOUT_IN_MS));
  checkpoint(8);
  return 0;
}

int on_connection(struct rdma_cm_id *id)
{
  //checkpoint(6);
  on_connect(id->context);
  send_sz(id->context); // send size of data to server
  printf("Size message sent to the server.\n");
  checkpoint(9);
  register_MR(id->context);
  return 0;
}

int on_disconnect(struct rdma_cm_id *id)
{
  printf("disconnected.\n");

  destroy_connection(id->context);
  
  return 1; /* exit event loop */
}

int on_event(struct rdma_cm_event *event)
{
  int r = 0;

  if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED)
    r = on_addr_resolved(event->id);
  else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED)
    r = on_route_resolved(event->id);
  else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
    r = on_connection(event->id);
  else if (event->event == RDMA_CM_EVENT_DISCONNECTED)
    r = on_disconnect(event->id);
  else {
    fprintf(stderr, "on_event: %d\n", event->event);
    die("on_event: unknown event.");
  }

  return r;
}

int on_route_resolved(struct rdma_cm_id *id)
{
  struct rdma_conn_param cm_params;
  printf("route resolved.\n");
  build_params(&cm_params);
  TEST_NZ(rdma_connect(id, &cm_params));
//  printf("rdma_connect passed.\n");

  return 0;
}

void usage(const char *argv0)
{
  fprintf(stderr, "usage: %s <mode> <server-address> <server-port> <size> <num_wr>\n  mode = \"read\", \"write\"\n", argv0);
  exit(1);
}