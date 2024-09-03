#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/time.h>
#include <stdlib.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#include <infiniband/verbs.h>

#define WC_BATCH (10)

enum {
    PINGPONG_RECV_WRID = 1,
    PINGPONG_SEND_WRID = 2,
};

static int page_size;

struct sub_context{
    struct ibv_pd		*pd;
    struct ibv_mr		*mr;
    struct ibv_qp		*qp;
    void			    *buf;

} typedef sub_context;


struct pingpong_context {
    struct ibv_context		*context;
    struct ibv_comp_channel	*channel;
    struct ibv_cq		*cq;
    sub_context         *in_ctx;
    sub_context         *out_ctx;
    int				size;
    int				rx_depth;
    int				routs;
    struct ibv_port_attr	portinfo;
};


struct pingpong_dest {
    int lid;
    int qpn;
    int psn;
    union ibv_gid gid;
};

enum ibv_mtu pp_mtu_to_enum(int mtu)
{
  switch (mtu) {
      case 256:  return IBV_MTU_256;
      case 512:  return IBV_MTU_512;
      case 1024: return IBV_MTU_1024;
      case 2048: return IBV_MTU_2048;
      case 4096: return IBV_MTU_4096;
      default:   return -1;
    }
}

uint16_t pp_get_local_lid(struct ibv_context *context, int port)
{
  struct ibv_port_attr attr;

  if (ibv_query_port(context, port, &attr))
    return 0;

  return attr.lid;
}

int pp_get_port_info(struct ibv_context *context, int port,
                     struct ibv_port_attr *attr)
{
  return ibv_query_port(context, port, attr);
}

void wire_gid_to_gid(const char *wgid, union ibv_gid *gid)
{
  char tmp[9];
  uint32_t v32;
  int i;

  for (tmp[8] = 0, i = 0; i < 4; ++i) {
      memcpy(tmp, wgid + i * 8, 8);
      sscanf(tmp, "%x", &v32);
      *(uint32_t *)(&gid->raw[i * 4]) = ntohl(v32);
    }
}

void gid_to_wire_gid(const union ibv_gid *gid, char wgid[])
{
  int i;

  for (i = 0; i < 4; ++i)
    sprintf(&wgid[i * 8], "%08x", htonl(*(uint32_t *)(gid->raw + i * 4)));
}

static int pp_connect_ctx(struct pingpong_context *ctx, int port, int my_psn,
                          enum ibv_mtu mtu, int sl,
                          struct pingpong_dest *dest, int sgid_idx, int in_out)
{
  struct ibv_qp_attr attr = {
      .qp_state		= IBV_QPS_RTR,
      .path_mtu		= mtu,
      .dest_qp_num		= dest->qpn,
      .rq_psn			= dest->psn,
      .max_dest_rd_atomic	= 1,
      .min_rnr_timer		= 12,
      .ah_attr		= {
          .is_global	= 0,
          .dlid		= dest->lid,
          .sl		= sl,
          .src_path_bits	= 0,
          .port_num	= port
      }
  };

  if (dest->gid.global.interface_id) {
      attr.ah_attr.is_global = 1;
      attr.ah_attr.grh.hop_limit = 1;
      attr.ah_attr.grh.dgid = dest->gid;
      attr.ah_attr.grh.sgid_index = sgid_idx;
    }

  struct ibv_qp *qp = ((in_out == 0) ? ctx->in_ctx->qp : ctx->out_ctx->qp);

  if (ibv_modify_qp(qp, &attr,
                    IBV_QP_STATE              |
                    IBV_QP_AV                 |
                    IBV_QP_PATH_MTU           |
                    IBV_QP_DEST_QPN           |
                    IBV_QP_RQ_PSN             |
                    IBV_QP_MAX_DEST_RD_ATOMIC |
                    IBV_QP_MIN_RNR_TIMER)) {
      fprintf(stderr, "Failed to modify QP to RTR\n");
      return 1;
    }

  attr.qp_state	    = IBV_QPS_RTS;
  attr.timeout	    = 14;
  attr.retry_cnt	    = 7;
  attr.rnr_retry	    = 7;
  attr.sq_psn	    = my_psn;
  attr.max_rd_atomic  = 1;
  if (ibv_modify_qp(qp, &attr,
                    IBV_QP_STATE              |
                    IBV_QP_TIMEOUT            |
                    IBV_QP_RETRY_CNT          |
                    IBV_QP_RNR_RETRY          |
                    IBV_QP_SQ_PSN             |
                    IBV_QP_MAX_QP_RD_ATOMIC)) {
      fprintf(stderr, "Failed to modify QP to RTS\n");
      return 1;
    }

  return 0;
}

static struct pingpong_dest *pp_client_exch_dest(const char *servername, int port,
                                                 const struct pingpong_dest *my_dest)
{
  struct addrinfo *res, *t;
  struct addrinfo hints = {
      .ai_family   = AF_INET,
      .ai_socktype = SOCK_STREAM
  };
  char *service;
  char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
  int n;
  int sockfd = -1;
  struct pingpong_dest *rem_dest = NULL;
  char gid[33];

  if (asprintf(&service, "%d", port) < 0)
    return NULL;

  n = getaddrinfo(servername, service, &hints, &res);

  if (n < 0) {
      fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
      free(service);
      return NULL;
    }

  for (t = res; t; t = t->ai_next) {
      sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
      if (sockfd >= 0) {
          if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
            break;
          close(sockfd);
          sockfd = -1;
        }
    }

  freeaddrinfo(res);
  free(service);

  if (sockfd < 0) {
      fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
      return NULL;
    }

  gid_to_wire_gid(&my_dest->gid, gid);
  sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn, my_dest->psn, gid);
  if (write(sockfd, msg, sizeof msg) != sizeof msg) {
      fprintf(stderr, "Couldn't send local address\n");
      goto out;
    }

  if (read(sockfd, msg, sizeof msg) != sizeof msg) {
      perror("client read");
      fprintf(stderr, "Couldn't read remote address\n");
      goto out;
    }

  write(sockfd, "done", sizeof "done");

  rem_dest = malloc(sizeof *rem_dest);
  if (!rem_dest)
    goto out;

  sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn, gid);
  wire_gid_to_gid(gid, &rem_dest->gid);

  out:
  close(sockfd);
  return rem_dest;
}

static struct pingpong_dest *pp_server_exch_dest(struct pingpong_context *ctx,
                                                 int ib_port, enum ibv_mtu mtu,
                                                 int port, int sl,
                                                 const struct pingpong_dest *my_dest,
                                                 int sgid_idx)
{
  struct addrinfo *res, *t;
  struct addrinfo hints = {
      .ai_flags    = AI_PASSIVE,
      .ai_family   = AF_INET,
      .ai_socktype = SOCK_STREAM
  };
  char *service;
  char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
  int n;
  int sockfd = -1, connfd;
  struct pingpong_dest *rem_dest = NULL;
  char gid[33];

  if (asprintf(&service, "%d", port) < 0)
    return NULL;

  n = getaddrinfo(NULL, service, &hints, &res);

  if (n < 0) {
      fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
      free(service);
      return NULL;
    }

  for (t = res; t; t = t->ai_next) {
      sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
      if (sockfd >= 0) {
          n = 1;

          setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);

          if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
            break;
          close(sockfd);
          sockfd = -1;
        }
    }

  freeaddrinfo(res);
  free(service);

  if (sockfd < 0) {
      fprintf(stderr, "Couldn't listen to port %d\n", port);
      return NULL;
    }

  listen(sockfd, 1);
  connfd = accept(sockfd, NULL, 0);
  close(sockfd);
  if (connfd < 0) {
      fprintf(stderr, "accept() failed\n");
      return NULL;
    }

  n = read(connfd, msg, sizeof msg);
  if (n != sizeof msg) {
      perror("server read");
      fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg);
      goto out;
    }

  rem_dest = malloc(sizeof *rem_dest);
  if (!rem_dest)
    goto out;

  sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn, gid);
  wire_gid_to_gid(gid, &rem_dest->gid);

  if (pp_connect_ctx(ctx, ib_port, my_dest->psn, mtu, sl, rem_dest, sgid_idx, 0)) {
      fprintf(stderr, "Couldn't connect to remote QP\n");
      free(rem_dest);
      rem_dest = NULL;
      goto out;
    }


  gid_to_wire_gid(&my_dest->gid, gid);
  sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn, my_dest->psn, gid);
  if (write(connfd, msg, sizeof msg) != sizeof msg) {
      fprintf(stderr, "Couldn't send local address\n");
      free(rem_dest);
      rem_dest = NULL;
      goto out;
    }

  read(connfd, msg, sizeof msg);

  out:
  close(connfd);
  return rem_dest;
}

#include <sys/param.h>

static struct pingpong_context *pp_init_ctx(struct ibv_device *ib_dev, int size,
                                            int rx_depth, int tx_depth, int port,
                                            int use_event, int is_server)
{
  struct pingpong_context *ctx;

  ctx = calloc(1, sizeof *ctx);
  if (!ctx)
    return NULL;

  ctx->size     = size;
  ctx->rx_depth = rx_depth;
  ctx->routs    = rx_depth;

  ctx->in_ctx = calloc(1, sizeof(sub_context));
  ctx->out_ctx = calloc(1, sizeof(sub_context));


  ctx->in_ctx->buf = malloc(roundup(size, page_size));
  ctx->out_ctx->buf = malloc(roundup(size, page_size));
  if ((!ctx->out_ctx->buf) || (!ctx->in_ctx->buf)) {
      fprintf(stderr, "Couldn't allocate work buf.\n");
      return NULL;
    }

  memset(ctx->in_ctx->buf, 0x7b + is_server, size);
  memset(ctx->out_ctx->buf, 0x7b + is_server, size);

  ctx->context = ibv_open_device(ib_dev);
  if (!ctx->context) {
      fprintf(stderr, "Couldn't get context for %s\n",
              ibv_get_device_name(ib_dev));
      return NULL;
    }

  if (use_event) {
      ctx->channel = ibv_create_comp_channel(ctx->context);
      if (!ctx->channel) {
          fprintf(stderr, "Couldn't create completion channel\n");
          return NULL;
        }
    } else
    ctx->channel = NULL;

  ctx->in_ctx->pd = ibv_alloc_pd(ctx->context);
  ctx->out_ctx->pd = ibv_alloc_pd(ctx->context);
  if ((!ctx->out_ctx->pd) ||(!ctx->in_ctx->pd)) {
      fprintf(stderr, "Couldn't allocate PD\n");
      return NULL;
    }

  ctx->in_ctx->mr = ibv_reg_mr(ctx->in_ctx->pd, ctx->in_ctx->buf, size, IBV_ACCESS_LOCAL_WRITE);
  if (!ctx->in_ctx->mr) {
      fprintf(stderr, "Couldn't register MR\n");
      return NULL;
    }
  ctx->out_ctx->mr = ibv_reg_mr(ctx->out_ctx->pd, ctx->out_ctx->buf, size, IBV_ACCESS_LOCAL_WRITE);
  if (!ctx->out_ctx->mr) {
      fprintf(stderr, "Couldn't register MR\n");
      return NULL;
    }

  ctx->cq = ibv_create_cq(ctx->context, rx_depth + tx_depth, NULL,
                          ctx->channel, 0);
  if (!ctx->cq) {
      fprintf(stderr, "Couldn't create CQ\n");
      return NULL;
    }

  {
    struct ibv_qp_init_attr attr = {
        .send_cq = ctx->cq,
        .recv_cq = ctx->cq,
        .cap     = {
            .max_send_wr  = tx_depth,
            .max_recv_wr  = rx_depth,
            .max_send_sge = 1,
            .max_recv_sge = 1
        },
        .qp_type = IBV_QPT_RC
    };

    ctx->in_ctx->qp = ibv_create_qp(ctx->in_ctx->pd, &attr);
    ctx->out_ctx->qp = ibv_create_qp(ctx->out_ctx->pd, &attr);
    if ((!ctx->in_ctx->qp) || (!ctx->out_ctx->qp))  {
        fprintf(stderr, "Couldn't create QP\n");
        return NULL;
      }
  }

  {
    struct ibv_qp_attr attr = {
        .qp_state        = IBV_QPS_INIT,
        .pkey_index      = 0,
        .port_num        = port,
        .qp_access_flags = IBV_ACCESS_REMOTE_READ |
                           IBV_ACCESS_REMOTE_WRITE
    };

    if (ibv_modify_qp(ctx->in_ctx->qp, &attr,
                      IBV_QP_STATE              |
                      IBV_QP_PKEY_INDEX         |
                      IBV_QP_PORT               |
                      IBV_QP_ACCESS_FLAGS)) {
        fprintf(stderr, "Failed to modify IN QP to INIT\n");
        return NULL;
      }
    if (ibv_modify_qp(ctx->out_ctx->qp, &attr,
                    IBV_QP_STATE              |
                    IBV_QP_PKEY_INDEX         |
                    IBV_QP_PORT               |
                    IBV_QP_ACCESS_FLAGS)) {
      fprintf(stderr, "Failed to modify OUT QP to INIT\n");
      return NULL;
    }
  }
  return ctx;
}

int pp_close_ctx(struct pingpong_context *ctx)
{
  if (ibv_destroy_qp(ctx->in_ctx->qp)) {
      fprintf(stderr, "Couldn't destroy QP\n");
      return 1;
    }
  if (ibv_destroy_qp(ctx->out_ctx->qp)) {
    fprintf(stderr, "Couldn't destroy QP\n");
    return 1;
  }

  if (ibv_destroy_cq(ctx->cq)) {
      fprintf(stderr, "Couldn't destroy CQ\n");
      return 1;
    }

  if (ibv_dereg_mr(ctx->in_ctx->mr)) {
      fprintf(stderr, "Couldn't deregister MR\n");
      return 1;
    }

  if (ibv_dereg_mr(ctx->out_ctx->mr)) {
      fprintf(stderr, "Couldn't deregister MR\n");
      return 1;
    }

  if (ibv_dealloc_pd(ctx->in_ctx->pd)) {
      fprintf(stderr, "Couldn't deallocate PD\n");
      return 1;
    }

  if (ibv_dealloc_pd(ctx->out_ctx->pd)) {
      fprintf(stderr, "Couldn't deallocate PD\n");
      return 1;
    }

  if (ctx->channel) {
      if (ibv_destroy_comp_channel(ctx->channel)) {
          fprintf(stderr, "Couldn't destroy completion channel\n");
          return 1;
        }
    }

  if (ibv_close_device(ctx->context)) {
      fprintf(stderr, "Couldn't release context\n");
      return 1;
    }

  free(ctx->in_ctx->buf);
  free(ctx->out_ctx->buf);
  free(ctx);

  return 0;
}

static int pp_post_recv(struct pingpong_context *ctx, int n)
{
  struct ibv_sge list = {
      .addr	= (uintptr_t) ctx->in_ctx->buf,
      .length = ctx->size,
      .lkey	= ctx->in_ctx->mr->lkey
  };
  struct ibv_recv_wr wr = {
      .wr_id	    = PINGPONG_RECV_WRID,
      .sg_list    = &list,
      .num_sge    = 1,
      .next       = NULL
  };
  struct ibv_recv_wr *bad_wr;
  int i;

  for (i = 0; i < n; ++i)
    if (ibv_post_recv(ctx->in_ctx->qp, &wr, &bad_wr))
      break;

  return i;
}

static int pp_post_send(struct pingpong_context *ctx)
{
  struct ibv_sge list = {
      .addr	= (uint64_t)ctx->out_ctx->buf,
      .length = ctx->size,
      .lkey	= ctx->out_ctx->mr->lkey
  };

  struct ibv_send_wr *bad_wr, wr = {
      .wr_id	    = PINGPONG_SEND_WRID,
      .sg_list    = &list,
      .num_sge    = 1,
      .opcode     = IBV_WR_SEND,
      .send_flags = IBV_SEND_SIGNALED,
      .next       = NULL
  };

  return ibv_post_send(ctx->out_ctx->qp, &wr, &bad_wr);
}

int pp_wait_completions(struct pingpong_context *ctx, int iters)
{
  int rcnt = 0, scnt = 0;
  while (rcnt + scnt < iters) {
      struct ibv_wc wc[WC_BATCH];
      int ne, i;

      do {
          ne = ibv_poll_cq(ctx->cq, WC_BATCH, wc);
          if (ne < 0) {
              fprintf(stderr, "poll CQ failed %d\n", ne);
              return 1;
            }

        } while (ne < 1);

      for (i = 0; i < ne; ++i) {
          if (wc[i].status != IBV_WC_SUCCESS) {
              fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                      ibv_wc_status_str(wc[i].status),
                      wc[i].status, (int) wc[i].wr_id);
              return 1;
            }

          switch ((int) wc[i].wr_id) {
              case PINGPONG_SEND_WRID:
                ++scnt;
              break;

              case PINGPONG_RECV_WRID:
                if (--ctx->routs <= 10) {
                    ctx->routs += pp_post_recv(ctx, ctx->rx_depth - ctx->routs);
                    if (ctx->routs < ctx->rx_depth) {
                        fprintf(stderr,"Couldn't post receive (%d)\n",ctx->routs);
                        return 1;
                      }
                  }
              ++rcnt;
              break;

              default:
                fprintf(stderr, "Completion for unknown wr_id %d\n",
                        (int) wc[i].wr_id);
              return 1;
            }
        }

    }
  return 0;
}

static void usage(const char *argv0)
{
  printf("Usage:\n");
  printf("  %s            start a server and wait for connection\n", argv0);
  printf("  %s <host>     connect to server at <host>\n", argv0);
  printf("\n");
  printf("Options:\n");
  printf("  -p, --port=<port>      listen on/connect to port <port> (default 18515)\n");
  printf("  -d, --ib-dev=<dev>     use IB device <dev> (default first device found)\n");
  printf("  -i, --ib-port=<port>   use port <port> of IB device (default 1)\n");
  printf("  -s, --size=<size>      size of message to exchange (default 4096)\n");
  printf("  -m, --mtu=<size>       path MTU (default 1024)\n");
  printf("  -r, --rx-depth=<dep>   number of receives to post at a time (default 500)\n");
  printf("  -n, --iters=<iters>    number of exchanges (default 1000)\n");
  printf("  -l, --sl=<sl>          service level value\n");
  printf("  -e, --events           sleep on CQ events (default poll)\n");
  printf("  -g, --gid-idx=<gid index> local port gid index\n");
}

struct vector{
    int array[4];
} typedef vector;

struct options{
    struct ibv_device      **dev_list;
    struct ibv_device       *ib_dev;
    struct pingpong_context *ctx;
    struct pingpong_dest     my_dest;
    struct pingpong_dest    *rem_dest;
    char                    *ib_devname;
    char                    *servername;
    int                      port;
    int                      ib_port;
    enum ibv_mtu             mtu;
    int                      rx_depth;
    int                      tx_depth;
    int                      iters;
    int                      use_event;
    int                      size;
    int                      sl;
    int                      gidx;
    char                     gid[33];
    struct vector*           vec;
    struct vector*           sol;
    int                      rank;
} typedef options;


int validate_ctx(options* opts, int flag){
  if (!(opts->ctx))
    return 1;

  struct pingpong_context* ctx = opts->ctx;

  ctx->routs = pp_post_recv(ctx, ctx->rx_depth);
  if (ctx->routs < ctx->rx_depth) {
      fprintf(stderr, "Couldn't post receive (%d)\n", ctx->routs);
      return 1;
    }

  if (opts->use_event)
    if (ibv_req_notify_cq(ctx->cq, 0)) {
        fprintf(stderr, "Couldn't request CQ notification\n");
        return 1;
      }


  if (pp_get_port_info(ctx->context, opts->ib_port, &(ctx->portinfo))) {
      fprintf(stderr, "Couldn't get port info\n");
      return 1;
    }

  opts->my_dest.lid = ctx->portinfo.lid;
  if (ctx->portinfo.link_layer == IBV_LINK_LAYER_INFINIBAND && !(opts->my_dest).lid) {
      fprintf(stderr, "Couldn't get local LID\n");
      return 1;
    }

  if (opts->gidx >= 0) {
      if (ibv_query_gid(ctx->context, opts->ib_port, opts->gidx, &(opts->my_dest).gid)) {
          fprintf(stderr, "Could not get local gid for gid index %d\n", opts->gidx);
          return 1;
        }
    } else
    memset(&(opts->my_dest).gid, 0, sizeof opts->my_dest.gid);

  opts->my_dest.qpn = ctx->in_ctx->qp->qp_num;
  opts->my_dest.psn = lrand48() & 0xffffff;
  inet_ntop(AF_INET6, &(opts->my_dest).gid, opts->gid, sizeof opts->gid);
  printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
         opts->my_dest.lid, opts->my_dest.qpn, opts->my_dest.psn, opts->gid);
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode"
int get_int (options *opts)
{

  if (opts->rank == 0)  //client
    opts->rem_dest = pp_client_exch_dest(opts->servername, opts->port, &(opts->my_dest));
  else  // server
    opts->rem_dest = pp_server_exch_dest(opts->ctx, opts->ib_port, opts->mtu, opts->port, opts->sl, &(opts->my_dest), opts->gidx);

  if (!(opts->rem_dest))
    return 1;

  inet_ntop(AF_INET6, &(opts->rem_dest->gid), opts->gid, sizeof opts->gid);
  printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
         opts->rem_dest->lid, opts->rem_dest->qpn, opts->rem_dest->psn,opts->gid);

  if (opts->rank == 0)
    if (pp_connect_ctx(opts->ctx, opts->ib_port, opts->my_dest.psn, opts->mtu, opts->sl, opts->rem_dest, opts->gidx,1))
      return 1;

  if (opts->rank == 0) {
      memcpy(opts->ctx->out_ctx->buf,opts->sol,sizeof(struct vector));
      for (int i = 0; i < opts->iters; i++) {
          if ((i != 0) && (i % opts->tx_depth == 0)) {
              pp_wait_completions(opts->ctx, opts->tx_depth);
            }
          if (pp_post_send(opts->ctx)) {
              fprintf(stderr, "Client couldn't post send\n");
              return 1;
            }
        }
      printf("Client Done.\n");
    } else {
      if (pp_post_send(opts->ctx)) {
          fprintf(stderr, "Server couldn't post send\n");
          return 1;
        }
      pp_wait_completions(opts->ctx, opts->iters);
//      opts->sol->array[0] += ((struct vector*)opts->ctx->in_buf)->array[0];
//      opts->sol->array[1] += ((struct vector*)opts->ctx->in_buf)->array[1];
//      opts->sol->array[2] += ((struct vector*)opts->ctx->in_buf)->array[2];
//      opts->sol->array[3] += ((struct vector*)opts->ctx->in_buf)->array[3];
//
      printf("%d %d %d %d\n",opts->sol->array[0],opts->sol->array[1],opts->sol->array[2],opts->sol->array[3]);
      printf("Server Done.\n");
    }


////////////////////////////server/////////////////////////////////////////////


  if (opts->rank != 0)  //client
    opts->rem_dest = pp_client_exch_dest(opts->servername, opts->port, &(opts->my_dest));
  else  // server
    opts->rem_dest = pp_server_exch_dest(opts->ctx, opts->ib_port, opts->mtu, opts->port, opts->sl, &(opts->my_dest), opts->gidx);

  if (!(opts->rem_dest))
    return 1;

  inet_ntop(AF_INET6, &(opts->rem_dest->gid), opts->gid, sizeof opts->gid);
  printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
         opts->rem_dest->lid, opts->rem_dest->qpn, opts->rem_dest->psn,opts->gid);

  if (opts->rank != 0)
    if (pp_connect_ctx(opts->ctx, opts->ib_port, opts->my_dest.psn, opts->mtu, opts->sl, opts->rem_dest, opts->gidx, 1))
      return 1;

  if (opts->rank != 0) {
      memcpy(opts->ctx->out_ctx->buf,opts->sol,sizeof(struct vector));
      for (int i = 0; i < opts->iters; i++) {
          if ((i != 0) && (i % opts->tx_depth == 0)) {
              pp_wait_completions(opts->ctx, opts->tx_depth);
            }
          if (pp_post_send(opts->ctx)) {
              fprintf(stderr, "Client couldn't post send\n");
              return 1;
            }
        }
      printf("Client Done.\n");
    } else {
      if (pp_post_send(opts->ctx)) {
          fprintf(stderr, "Server couldn't post send\n");
          return 1;
        }

      pp_wait_completions(opts->ctx, opts->iters);
//      opts->sol->array[0] += ((struct vector*)opts->ctx->in_buf)->array[0];
//      opts->sol->array[1] += ((struct vector*)opts->ctx->in_buf)->array[1];
//      opts->sol->array[2] += ((struct vector*)opts->ctx->in_buf)->array[2];
//      opts->sol->array[3] += ((struct vector*)opts->ctx->in_buf)->array[3];
//
      printf("%d %d %d %d\n",opts->sol->array[0],opts->sol->array[1],opts->sol->array[2],opts->sol->array[3]);
      printf("Server Done.\n");
    }
  return 0;
}
#pragma clang diagnostic pop


int main(int argc, char *argv[])
{
  options* opts = calloc (1, sizeof(options));
  opts->ib_devname = NULL;
  opts->port = 9898;
  opts->ib_port = 1;
  opts->mtu = IBV_MTU_2048;
  opts->rx_depth = 100;
  opts->tx_depth = 100;
  opts->iters = 1000;
  opts->use_event = 0;
  opts->size = 4096;
  opts->sl = 0;
  opts->gidx = -1;
  opts->gid[33];

  opts->vec = calloc(1, sizeof(struct vector));
  opts->sol = calloc(1, sizeof(struct vector));
  srand48(getpid() * time(NULL));

  while (1) {
      int c;

      static struct option long_options[] = {
          { .name = "port",     .has_arg = 1, .val = 'p' },
          { .name = "ib-dev",   .has_arg = 1, .val = 'd' },
          { .name = "ib-port",  .has_arg = 1, .val = 'i' },
          { .name = "size",     .has_arg = 1, .val = 's' },
          { .name = "mtu",      .has_arg = 1, .val = 'm' },
          { .name = "rx-depth", .has_arg = 1, .val = 'r' },
          { .name = "iters",    .has_arg = 1, .val = 'n' },
          { .name = "sl",       .has_arg = 1, .val = 'l' },
          { .name = "events",   .has_arg = 0, .val = 'e' },
          { .name = "gid-idx",  .has_arg = 1, .val = 'g' },
          { .name = "vec",      .has_arg = 1, .val = 'v' },
          { .name = "rank",      .has_arg = 1, .val = 'k' },
          { 0 }
      };

      c = getopt_long(argc, argv, "p:d:i:s:m:r:n:l:e:g:v:k:", long_options, NULL);
      if (c == -1)
        break;

      switch (c) {
          case 'p':
            opts->port = strtol(optarg, NULL, 0);
          if (opts->port < 0 || opts->port > 65535) {
              usage(argv[0]);
              return 1;
            }
          break;

          case 'd':
            opts->ib_devname = strdup(optarg);
          break;

          case 'i':
            opts->ib_port = strtol(optarg, NULL, 0);
          if (opts->ib_port < 0) {
              usage(argv[0]);
              return 1;
            }
          break;

          case 's':
            opts->size = strtol(optarg, NULL, 0);
          break;

          case 'm':
            opts->mtu = pp_mtu_to_enum(strtol(optarg, NULL, 0));
          if (opts->mtu < 0) {
              usage(argv[0]);
              return 1;
            }
          break;

          case 'r':
            opts->rx_depth = strtol(optarg, NULL, 0);
          break;

          case 'n':
            opts->iters = strtol(optarg, NULL, 0);
          break;

          case 'l':
            opts->sl = strtol(optarg, NULL, 0);
          break;

          case 'e':
            ++opts->use_event;
          break;

          case 'g':
            opts->gidx = strtol(optarg, NULL, 0);
          break;

          case 'v':
            opts->vec->array[0] = atoi(strtok(optarg, ","));
            opts->vec->array[1] = atoi(strtok(NULL, ","));
            opts->vec->array[2] = atoi(strtok(NULL, ","));
            opts->vec->array[3] = atoi(strtok(NULL, ","));

            opts->sol->array[0] = opts->vec->array[0];
            opts->sol->array[1] = opts->vec->array[1];
            opts->sol->array[2] = opts->vec->array[2];
            opts->sol->array[3] = opts->vec->array[3];
          break;

          case 'k':
            opts->rank = strtol(optarg,NULL,0);
          break;

          default:
            usage(argv[0]);
          return 1;
        }
    }

  if (optind == argc - 1)
    opts->servername = strdup(argv[optind]);
  else if (optind < argc) {
      usage(argv[0]);
      return 1;
    }

  page_size = sysconf(_SC_PAGESIZE);

  opts->dev_list = ibv_get_device_list(NULL);
  if (!(opts->dev_list)) {
      perror("Failed to get IB devices list");
      return 1;
    }

  if (!(opts->ib_devname)) {
      opts->ib_dev = *(opts->dev_list);
      if (!(opts->ib_dev)) {
          fprintf(stderr, "No IB devices found\n");
          return 1;
        }
    } else {
      int i;
      for (i = 0; opts->dev_list[i]; ++i)
        if (!strcmp(ibv_get_device_name(opts->dev_list[i]), opts->ib_devname))
          break;
      opts->ib_dev = opts->dev_list[i];
      if (!(opts->ib_dev)) {
          fprintf(stderr, "IB device %s not found\n", opts->ib_devname);
          return 1;
        }
    }

  opts->ctx = pp_init_ctx(opts->ib_dev, opts->size, opts->rx_depth, opts->tx_depth, opts->ib_port, opts->use_event, !(opts->servername));
  int error =  validate_ctx (opts, 1);


///////////////////client//////////////////////////////////////////////////////
  for (int i=0; i<4; ++i)
  {
    get_int(opts);
  }

  return 0;

}