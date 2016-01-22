/*
 * This file is part of trinarkular
 *
 * Copyright (C) 2015 The Regents of the University of California.
 * Authors: Alistair King
 *
 * All rights reserved.
 *
 * This code has been developed by CAIDA at UC San Diego.
 * For more information, contact alistair@caida.org
 *
 * This source code is proprietary to the CAIDA group at UC San Diego and may
 * not be redistributed, published or disclosed without prior permission from
 * CAIDA.
 *
 * Report any bugs, questions or comments to alistair@caida.org
 *
 */

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <scamper_addr.h>
#include <scamper_list.h>
#include <scamper_dealias.h>
#include <scamper_file.h>
#include <scamper_writebuf.h>
#include <scamper_linepoll.h>

#include "utils.h"

#include "trinarkular_log.h"
#include "trinarkular_driver_interface.h"

#include "trinarkular_driver_scamper.h"

// hax to get scamper's utils
extern int sockaddr_compose(struct sockaddr *sa,
                     const int af, const void *addr, const int port);
extern int sockaddr_compose_un(struct sockaddr *sa, const char *name);
extern int sockaddr_compose_str(struct sockaddr *sa, const char *ip,
                                const int port);
extern int sockaddr_len(const struct sockaddr *sa);
extern struct sockaddr *sockaddr_dup(const struct sockaddr *sa);
extern char *sockaddr_tostr(const struct sockaddr *sa, char *buf,
                            const size_t len);
extern int fcntl_set(const int fd, const int flags);
extern int fcntl_unset(const int fd, const int flags);
extern int uuencode(const uint8_t *in, size_t ilen, uint8_t **out, size_t *olen);
extern size_t uuencode_len(size_t ilen, size_t *complete, size_t *leftover);
extern size_t uuencode_bytes(const uint8_t *in, size_t len, size_t *off,
                             uint8_t *out, size_t olen);
extern void *uudecode(const char *in, size_t len);
extern int uudecode_line(const char *in, size_t ilen, uint8_t *out, size_t *olen);
extern int   string_isnumber(const char *str);
extern int   string_tolong(const char *str, long *l);



#define REQ_QUEUE_LEN 100000

#define MIN_REQ_PER_COMMAND 500
#define MAX_REQ_PER_COMMAND 500

#define CMD_BUF_LEN (256 + (MAX_REQ_PER_COMMAND * (INET_ADDRSTRLEN+1)))

#define TV_TO_MS(timeval)                       \
  ((timeval.tv_sec * (uint64_t)1000) + timeval.tv_usec)

/** Our 'subclass' of the generic driver */
typedef struct scamper_driver {
  TRINARKULAR_DRIVER_HEAD_DECLARE

  // port for scamper daemon
  uint16_t port;
  // unix socket for remote controlled scamper
  char *unix_socket;

  // list of queued requests
  trinarkular_probe_req_t req_queue[REQ_QUEUE_LEN];
  // number of queued requests
  int req_queue_cnt;
  // index of next request to send (head of the queue)
  int req_queue_next_idx;
  // index of last request to send (tail of the queue)
  int req_queue_last_idx;
  // the number of probes that we have handled
  uint64_t probe_cnt;
  // the number of probes that we have dropped
  uint64_t dropped_cnt;

  // scamper goo
  scamper_writebuf_t *scamper_wb;
  int scamper_fd;
  scamper_linepoll_t *scamper_lp;
  scamper_writebuf_t *decode_wb;
  scamper_file_t *decode_in;
  int decode_in_fd;
  int decode_out_fd;
  int data_left;
  int more;
  int probing_cnt;
  scamper_file_filter_t *ffilter;

  // loop pollitems
  zmq_pollitem_t scamper_pollin;
  int scamper_pollin_active;

  zmq_pollitem_t scamper_pollout;
  int scamper_pollout_active;

  zmq_pollitem_t decode_in_pollin;
  int decode_in_pollin_active;

  zmq_pollitem_t decode_out_pollout;
  int decode_out_pollout_active;

} scamper_driver_t;

#define MY(drv) ((scamper_driver_t*)(drv))

/** Our class instance */
static scamper_driver_t clz = {
  TRINARKULAR_DRIVER_HEAD_INIT(TRINARKULAR_DRIVER_ID_SCAMPER, "scamper", scamper)
};

static int handle_scamper_fd_read(zloop_t *loop, zmq_pollitem_t *pi, void *arg)
{
  trinarkular_driver_t *drv = (trinarkular_driver_t *)arg;
  ssize_t rc;
  uint8_t buf[512];

  if((rc = read(MY(drv)->scamper_fd, buf, sizeof(buf))) > 0) {
    scamper_linepoll_handle(MY(drv)->scamper_lp, buf, rc);
    return 0;
  } else if(rc == 0) {
    close(MY(drv)->scamper_fd);
    MY(drv)->scamper_fd = -1;
    return 0;
  } else if(errno == EINTR || errno == EAGAIN) {
    return 0;
  }

  trinarkular_log("ERROR: could not read: errno %d", errno);
  return -1;
}

static int handle_scamper_fd_write(zloop_t *loop, zmq_pollitem_t *pi, void *arg)
{
  trinarkular_driver_t *drv = (trinarkular_driver_t *)arg;
  if (scamper_writebuf_write(MY(arg)->scamper_fd, MY(arg)->scamper_wb) != 0) {
    trinarkular_log("ERROR: Scamper writebuf write failed");
    return -1;
  }

  if(scamper_writebuf_len(MY(arg)->scamper_wb) == 0) {
    // remove from poller
    zloop_poller_end(TRINARKULAR_DRIVER_ZLOOP(drv),
                     &MY(drv)->scamper_pollout);
    // zloop is kinda dumb. need to re-add pollin for scamper
    if (zloop_poller(TRINARKULAR_DRIVER_ZLOOP(drv),
                     &MY(drv)->scamper_pollin,
                     handle_scamper_fd_read, drv) != 0) {
      trinarkular_log("ERROR: Could not add scamper [read] to event loop");
      return -1;
    }
    MY(drv)->scamper_pollout_active = 0;
  }
  return 0;
}

static int scamper_writebuf_send_wrap(trinarkular_driver_t *drv,
                                      char *cmd, size_t len)
{
  // do the actual write
  if(scamper_writebuf_send(MY(drv)->scamper_wb, cmd, len) != 0) {
    trinarkular_log("ERROR: could not send '%s' to scamper", cmd);
    return -1;
  }

  // if this is not being polled for, add it back
  if (MY(drv)->scamper_pollout_active == 0) {
    if (zloop_poller(TRINARKULAR_DRIVER_ZLOOP(drv),
                     &MY(drv)->scamper_pollout,
                     handle_scamper_fd_write, drv) != 0) {
      trinarkular_log("ERROR: Could not add scamper [write] to event loop");
      return -1;
    }
    MY(drv)->scamper_pollout_active = 1;
  }
  return 0;
}

static int handle_decode_out_fd_write(zloop_t *loop, zmq_pollitem_t *pi,
                                      void *arg)
{
  trinarkular_driver_t *drv = (trinarkular_driver_t *)arg;
  if (scamper_writebuf_write(MY(arg)->decode_out_fd, MY(arg)->decode_wb) != 0) {
    trinarkular_log("ERROR: Decode write failed");
    return -1;
  }

  if(scamper_writebuf_len(MY(arg)->decode_wb) == 0) {
    // remove from poller
    zloop_poller_end(TRINARKULAR_DRIVER_ZLOOP(drv),
                     &MY(drv)->decode_out_pollout);
    MY(drv)->decode_out_pollout_active = 0;
  }
  return 0;
}

static int decode_out_writebuf_send_wrap(trinarkular_driver_t *drv,
                                         void *data, size_t len)
{
  // do the actual write
  if(scamper_writebuf_send(MY(drv)->decode_wb, data, len) != 0) {
    trinarkular_log("ERROR: could not send to decoder");
    return -1;
  }

  // if this is not being polled for, add it back
  if (MY(drv)->decode_out_pollout_active == 0) {
    if (zloop_poller(TRINARKULAR_DRIVER_ZLOOP(drv),
                     &MY(drv)->decode_out_pollout,
                     handle_decode_out_fd_write, drv) != 0) {
      trinarkular_log("ERROR: Could not add decode out [write] to event loop");
      return -1;
    }
    MY(drv)->decode_out_pollout_active = 1;
  }
  return 0;
}

static int send_req(trinarkular_driver_t *drv)
{
  trinarkular_probe_req_t *req;
  char cmd[CMD_BUF_LEN];
  size_t len;
  int targets_added = 0;

  uint16_t wait;

  assert(MY(drv)->more > 0);

  if (MY(drv)->req_queue_cnt < MIN_REQ_PER_COMMAND) {
    //trinarkular_log("DEBUG: Waiting while req queue is %d",
    //                MY(drv)->req_queue_cnt);
    return 0;
  }

  // build scamper command (grab the config from the first request)
  req = &MY(drv)->req_queue[MY(drv)->req_queue_next_idx];
  wait = req->wait;
  if ((len = snprintf(cmd, CMD_BUF_LEN,
                      "dealias -m radargun -p \"-P icmp-echo\" "
                      "-w %d -q 1 -W 1",
                      wait
                      )) >= CMD_BUF_LEN) {
    trinarkular_log("ERROR: Could not build scamper command");
    return -1;
  }

  // pop a req from the queue up to our limit and build a scamper command
  while (MY(drv)->req_queue_cnt > 0 && targets_added < MAX_REQ_PER_COMMAND) {

    req = &MY(drv)->req_queue[MY(drv)->req_queue_next_idx];

    // if this request has different parameters to the previous, then we can't
    // batch it together
    if (req->wait != wait) {
      trinarkular_log("WARN: Stopping batch due to mismatched params");
      break;
    }

    MY(drv)->req_queue_next_idx =
      (MY(drv)->req_queue_next_idx + 1) % REQ_QUEUE_LEN;
    MY(drv)->req_queue_cnt--;

    // add IP to scamper command
    if (CMD_BUF_LEN-len < 1+INET_ADDRSTRLEN) {
      trinarkular_log("ERROR: Could not convert IP address to string");
      return -1;
    }

    cmd[len] = ' ';
    len++;
    if (inet_ntop(AF_INET, &req->target_ip,
                  &cmd[len], INET_ADDRSTRLEN) == NULL) {
      trinarkular_log("ERROR: Could not convert IP address to string");
      return -1;
    }

    while(cmd[len] != '\0') {
      len++;
    }

    targets_added++;
  }

  // add newline
  if (CMD_BUF_LEN-len < 2) {
    trinarkular_log("ERROR: Could not build scamper command");
    return -1;
  }
  cmd[len] = '\n';
  len++;
  cmd[len] = '\0';

  //fprintf(stderr, "cmd: %s", cmd);

  if(scamper_writebuf_send_wrap(drv, cmd, len) != 0) {
    return -1;
  }

  MY(drv)->probing_cnt++;
  MY(drv)->more--;

  return targets_added;
}

static int handle_scamperread_line(void *param, uint8_t *buf, size_t linelen)
{
  trinarkular_driver_t *drv = (trinarkular_driver_t *)param;
  char *head = (char *)buf;
  uint8_t uu[64];
  size_t uus;
  long lo;

  /* skip empty lines */
  if(head[0] == '\0') {
    return 0;
  }

  /* if currently decoding data, then pass it to uudecode */
  if(MY(drv)->data_left > 0) {
    uus = sizeof(uu);
    if(uudecode_line(head, linelen, uu, &uus) != 0) {
      trinarkular_log("ERROR: could not uudecode_line");
      return -1;
    }
    if(uus != 0) {
      decode_out_writebuf_send_wrap(drv, uu, uus);
    }
    MY(drv)->data_left -= (linelen + 1);
    return 0;
  }

  /* feedback letting us know that the command was accepted */
  if(linelen >= 2 && strncasecmp(head, "OK", 2) == 0) {
    return 0;
  }

  /* if the scamper process is asking for more tasks, give it more */
  if(linelen == 4 && strncasecmp(head, "MORE", linelen) == 0) {
    MY(drv)->more++;
    if (send_req(drv) < 0) {
      return -1;
    }
    return 0;
  }

  /* new piece of data */
  if(linelen > 5 && strncasecmp(head, "DATA ", 5) == 0) {
    if(string_isnumber(head+5) == 0 || string_tolong(head+5, &lo) != 0) {
      trinarkular_log("could not parse %s", head);
      return -1;
    }
    MY(drv)->data_left = lo;
    return 0;
  }

  /* feedback letting us know that the command was not accepted */
  if(linelen >= 3 && strncasecmp(head, "ERR", 3) == 0) {
    trinarkular_log("ERROR: Command not accepted by scamper");
    return -1;
  }

  trinarkular_log("ERROR: unknown response '%s'", head);
  return -1;
}

static int handle_decode_in_fd_read(zloop_t *loop, zmq_pollitem_t *pi,
                                    void *arg)
{
  void     *data;
  uint16_t  type;
  scamper_dealias_t *dealias;
  scamper_dealias_radargun_t *radargun;
  scamper_dealias_probedef_t *def;
  scamper_dealias_probe_t *probe;
  scamper_dealias_reply_t *reply;
  trinarkular_probe_resp_t resp;
  struct timeval rtt;
  uint64_t rtt_tmp;
  int i, j;

  /* try and read a ping from the warts decoder */
  if(scamper_file_read(MY(arg)->decode_in,
                       MY(arg)->ffilter, &type, &data) != 0) {
    trinarkular_log("ERROR: scamper_file_read errno %d", errno);
    return -1;
  }
  if(data == NULL) {
    return 0;
  }
  MY(arg)->probing_cnt--;

  assert(type == SCAMPER_FILE_OBJ_DEALIAS);

  dealias = (scamper_dealias_t *)data;
  assert(dealias->method == SCAMPER_DEALIAS_METHOD_RADARGUN);
  radargun = (scamper_dealias_radargun_t *)dealias->data;

  for (i=0; i<dealias->probec; i++) {
    probe = dealias->probes[i];
    def = probe->def;

    assert(def->dst->type == SCAMPER_ADDR_TYPE_IPV4);
    memcpy(&resp.target_ip, def->dst->addr, sizeof(uint32_t));

    resp.rtt = 0;

    resp.verdict = TRINARKULAR_PROBE_UNRESPONSIVE;
    // look for the first responsive reply
    for (j=0; j<probe->replyc; j++) {
      reply = probe->replies[j];
      if (reply != NULL &&
          SCAMPER_DEALIAS_REPLY_FROM_TARGET(probe, reply)) {
        resp.verdict = TRINARKULAR_PROBE_RESPONSIVE;
        timeval_subtract(&rtt, &probe->tx, &reply->rx);
        rtt_tmp = TV_TO_MS(rtt);
        assert(rtt_tmp < UINT32_MAX);
        resp.rtt = rtt_tmp;
        break;
      }
    }

    // yield this response to the user thread
    if (trinarkular_driver_yield_resp((trinarkular_driver_t*)arg, &resp) != 0) {
      return -1;
    }
  }

  scamper_dealias_free(data);

  return 0;
}

/*
 * allocate socket and connect to scamper process listening on the port
 * specified.
 */
static int scamper_connect(trinarkular_driver_t *drv)
{
  struct sockaddr_un sun;
  struct sockaddr_in sin;
  struct in_addr in;

  if (MY(drv)->unix_socket == NULL) {
    // local port
    inet_aton("127.0.0.1", &in);
    sockaddr_compose((struct sockaddr *)&sin, AF_INET, &in, MY(drv)->port);
    if ((MY(drv)->scamper_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
      trinarkular_log("ERROR: could not allocate new socket");
      return -1;
    }
    if (connect(MY(drv)->scamper_fd,
               (const struct sockaddr *)&sin, sizeof(sin)) != 0) {
      trinarkular_log("ERROR: could not connect to scamper process");
      return -1;
    }
  } else {
    // unix socket
    if (sockaddr_compose_un((struct sockaddr *)&sun,
                            MY(drv)->unix_socket) != 0) {
      trinarkular_log("ERROR: could not build sockaddr_un");
      return -1;
    }
    if ((MY(drv)->scamper_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
      trinarkular_log("ERROR:could not allocate unix domain socket");
      return -1;
    }
    if (connect(MY(drv)->scamper_fd,
                (const struct sockaddr *)&sun, sizeof(sun)) != 0) {
      trinarkular_log("ERROR: could not connect to scamper process");
      return -1;
    }
  }

  if(fcntl_set(MY(drv)->scamper_fd, O_NONBLOCK) == -1)
    {
      trinarkular_log("ERRPOR: could not set nonblock on scamper_fd\n");
      return -1;
    }

  return 0;
}


static void usage(char *name)
{
  fprintf(stderr,
          "Driver usage: %s [options] [-p|-R]\n"
          "       -p <port>      port to find scamper on\n"
          "       -R <unix>      unix domain socket for remote controlled scamper\n",
          name);
}

static int parse_args(trinarkular_driver_t *drv, int argc, char **argv)
{
  int opt;
  int prevoptind;

  int port_set = 0;

  optind = 1;
  while(prevoptind = optind,
	(opt = getopt(argc, argv, ":p:R:?")) >= 0)
    {
      if (optind == prevoptind + 2 &&
          optarg && *optarg == '-' && *(optarg+1) != '\0') {
        opt = ':';
        -- optind;
      }
      switch(opt)
	{
        case 'p':
          MY(drv)->port = strtoul(optarg, NULL, 10);
          port_set = 1;
          break;

	case 'R':
          MY(drv)->unix_socket = strdup(optarg);
          assert(MY(drv)->unix_socket != NULL);
          break;

	case ':':
	  fprintf(stderr, "ERROR: Missing option argument for -%c\n", optopt);
	  usage(argv[0]);
	  return -1;
	  break;

	case '?':
	  usage(argv[0]);
          return -1;
	  break;

	default:
	  usage(argv[0]);
          return -1;
	}
    }

  // either the port or a unix socket must be specified
  if (port_set == 0 && MY(drv)->unix_socket == NULL) {
    fprintf(stderr,
            "ERROR: Either a port (-p) or unix socket (-R) must be specified\n");
    usage(argv[0]);
    return -1;
  }

  return 0;
}

/* ==================== PUBLIC API FUNCTIONS ==================== */

trinarkular_driver_t *
trinarkular_driver_scamper_alloc()
{
  scamper_driver_t *drv = NULL;

  if ((drv = malloc_zero(sizeof(scamper_driver_t))) == NULL) {
    trinarkular_log("ERROR: failed");
    return NULL;
  }

  // copy the class
  memcpy(drv, &clz, sizeof(scamper_driver_t));

  return (trinarkular_driver_t *)drv;
}

int trinarkular_driver_scamper_init(trinarkular_driver_t *drv,
                                 int argc, char **argv)
{
  int pair[2];
  uint16_t types[] = {SCAMPER_FILE_OBJ_DEALIAS};
  int typec = 1;

  if (parse_args(drv, argc, argv) != 0) {
    return -1;
  }

  // scamper_wb
  if ((MY(drv)->scamper_wb = scamper_writebuf_alloc()) == NULL) {
    trinarkular_log("ERROR: Could not alloc scamper_wb");
    return -1;
  }

  // scamper_lp
  if ((MY(drv)->scamper_lp =
       scamper_linepoll_alloc(handle_scamperread_line, drv)) == NULL) {
    trinarkular_log("ERROR: Could not alloc scamper_lp");
    return -1;
  }

  // decode_wb
  if ((MY(drv)->decode_wb = scamper_writebuf_alloc()) == NULL) {
    trinarkular_log("ERROR: Could not alloc decode_wb");
    return -1;
  }

  // connect to scamper
  if (scamper_connect(drv) != 0) {
    return -1;
  }

  // set up the decode socket pair
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
    trinarkular_log("ERROR: Could not create socket pair");
    return -1;
  }
  if ((MY(drv)->decode_in =
       scamper_file_openfd(pair[0], NULL, 'r', "warts")) == NULL) {
    trinarkular_log("ERROR: Could not create warts decoder");
    return -1;
  }
  if (fcntl_set(pair[0], O_NONBLOCK) == -1 || fcntl_set(pair[1], O_NONBLOCK)) {
    trinarkular_log("ERROR: Could not set non-blocking on socket pair");
    return -1;
  }
  MY(drv)->decode_in_fd = pair[0];
  MY(drv)->decode_out_fd = pair[1];

  // create the filter
  if ((MY(drv)->ffilter = scamper_file_filter_alloc(types, typec)) == NULL) {
    trinarkular_log("ERROR: Could not create file filter");
    return -1;
  }

  trinarkular_log("done");

  return 0;
}

void trinarkular_driver_scamper_destroy(trinarkular_driver_t *drv)
{
  if (drv == NULL) {
    return;
  }

  if(MY(drv)->scamper_wb != NULL) {
    scamper_writebuf_free(MY(drv)->scamper_wb);
    MY(drv)->scamper_wb = NULL;
  }

  if(MY(drv)->scamper_lp != NULL) {
    scamper_linepoll_free(MY(drv)->scamper_lp, 0);
    MY(drv)->scamper_lp = NULL;
  }

  if(MY(drv)->decode_wb != NULL) {
    scamper_writebuf_free(MY(drv)->decode_wb);
    MY(drv)->decode_wb = NULL;
  }

  if(MY(drv)->decode_in != NULL) {
    scamper_file_close(MY(drv)->decode_in);
    MY(drv)->decode_in = NULL;
  }

  close(MY(drv)->decode_in_fd);
  close(MY(drv)->decode_out_fd);
  close(MY(drv)->scamper_fd);

  if(MY(drv)->ffilter != NULL) {
    scamper_file_filter_free(MY(drv)->ffilter);
  }

  free(MY(drv)->unix_socket);
  MY(drv)->unix_socket = NULL;
}

int trinarkular_driver_scamper_init_thr(trinarkular_driver_t *drv)
{
  // do not write to any writebuf before this function is called

  // scamper_fd read from socket
  MY(drv)->scamper_pollin.fd = MY(drv)->scamper_fd;
  MY(drv)->scamper_pollin.events = ZMQ_POLLIN;
  if (zloop_poller(TRINARKULAR_DRIVER_ZLOOP(drv),
                   &MY(drv)->scamper_pollin,
                   handle_scamper_fd_read, drv) != 0) {
    trinarkular_log("ERROR: Could not add scamper [read] to event loop");
    return -1;
  }
  MY(drv)->scamper_pollin_active = 1;

  // scamper fd write to socket
  MY(drv)->scamper_pollout.fd = MY(drv)->scamper_fd;
  MY(drv)->scamper_pollout.events = ZMQ_POLLOUT;
  if (zloop_poller(TRINARKULAR_DRIVER_ZLOOP(drv),
                   &MY(drv)->scamper_pollout,
                   handle_scamper_fd_write, drv) != 0) {
    trinarkular_log("ERROR: Could not add scamper [write] to event loop");
    return -1;
  }
  MY(drv)->scamper_pollout_active = 1;

  // decode in read from socket
  MY(drv)->decode_in_pollin.fd = MY(drv)->decode_in_fd;
  MY(drv)->decode_in_pollin.events = ZMQ_POLLIN;
  if (zloop_poller(TRINARKULAR_DRIVER_ZLOOP(drv),
                   &MY(drv)->decode_in_pollin,
                   handle_decode_in_fd_read, drv) != 0) {
    trinarkular_log("ERROR: Could not add decode_in [read] to event loop");
    return -1;
  }
  MY(drv)->decode_in_pollin_active = 1;

  // decode out write to socket
  MY(drv)->decode_out_pollout.fd = MY(drv)->decode_out_fd;
  MY(drv)->decode_out_pollout.events = ZMQ_POLLOUT;
  if (zloop_poller(TRINARKULAR_DRIVER_ZLOOP(drv),
                   &MY(drv)->decode_out_pollout,
                   handle_decode_out_fd_write, drv) != 0) {
    trinarkular_log("ERROR: Could not add decode_out [write] to event loop");
    return -1;
  }
  MY(drv)->decode_out_pollout_active = 1;


  // attach to scamper
  scamper_writebuf_send_wrap(drv, "attach\n", 7);

  return 0;
}

int trinarkular_driver_scamper_handle_req(trinarkular_driver_t *drv,
                                          trinarkular_probe_req_t *req)
{
  trinarkular_probe_req_t *q_req;
  int ret = 0, cnt = 0;

  // append to list of outstanding requests
  // if list is too long, drop the probe
  if (MY(drv)->req_queue_cnt < REQ_QUEUE_LEN) {
    // guaranteed to be able to queue this request
    q_req = & MY(drv)->req_queue[MY(drv)->req_queue_last_idx];
    *q_req = *req;
    MY(drv)->req_queue_last_idx =
      (MY(drv)->req_queue_last_idx + 1) % REQ_QUEUE_LEN;
    MY(drv)->req_queue_cnt++;
    MY(drv)->probe_cnt++;
  } else {
    MY(drv)->dropped_cnt++;
    if ((MY(drv)->dropped_cnt % 1000) == 0) {
      trinarkular_log("WARN: %d requests have been dropped", MY(drv)->dropped_cnt);
    }
    ret = REQ_DROPPED;
  }

  // send all the requests that scamper can handle
  while (MY(drv)->more > 0) {
    if ((cnt = send_req(drv)) < 0) {
      return -1;
    } else if (cnt == 0) { // sent nothing, so lets stop
      break;
    }
  }

  if ((MY(drv)->probe_cnt % 1000) == 0) {
    trinarkular_log("INFO: %d requests are queued", MY(drv)->req_queue_cnt);
  }

  return ret;
}
