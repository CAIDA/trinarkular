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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <czmq.h>

#include "khash.h"
#include "utils.h"

#include "trinarkular_log.h"
#include "trinarkular_driver_interface.h"

#include "trinarkular_driver_test.h"

/** The maximum RTT that will be simulated */
#define MAX_RTT 3000

/** % of unresponsive probes */
#define UNRESP_PROBES 0

/** % of unresponsive targets */
#define UNRESP_TARGETS 0

/** Check for responses every 500ms */
#define RESP_TIMER 500

#define MY(drv) ((test_driver_t*)(drv))

struct req_wrap {
  uint64_t rx_time; // when should the response be generated

  seq_num_t seq_num;
  trinarkular_probe_req_t req;
  uint64_t rtt; // if 0 this probe was not responded to
  uint8_t responsive_target;
  int probe_tx;

  struct req_wrap *next; // higher rx_time
  struct req_wrap *prev; // lower rx_time
};

/** Our 'subclass' of the generic driver */
typedef struct test_driver {
  TRINARKULAR_DRIVER_HEAD_DECLARE

  // add our fields here

  /** Maximum simulated RTT */
  uint64_t max_rtt;

  /** % of unresponsive probes */
  int unresp_probes;

  /** % of unresponsive targets */
  int unresp_targets;

  /** Ordered queue of requests (next to expire) (pop from here) */
  struct req_wrap *next_req_rx;

  /** Ordered queue of requests (last to expire) (add here) */
  struct req_wrap *last_req_rx;

} test_driver_t;

/** Our class instance */
static test_driver_t clz = {
  TRINARKULAR_DRIVER_HEAD_INIT(TRINARKULAR_DRIVER_ID_TEST, "test", test)
};

#if 0
static void dump_queues(trinarkular_driver_t *drv)
{
  int i;
  struct req_wrap *this_rx;

  this_rx = MY(drv)->next_req_rx;
  i = 0;
  while (this_rx != NULL) {
    fprintf(stdout, "FWD: %d: %"PRIu64"\n",
            i, this_rx->rx_time);
    this_rx = this_rx->next;
    i++;
  }

  this_rx = MY(drv)->last_req_rx;
  i = 0;
  while (this_rx != NULL) {
    fprintf(stdout, "BCK: %d: %"PRIu64"\n",
            i, this_rx->rx_time);
    this_rx = this_rx->prev;
    i++;
  }

  fprintf(stdout, "----------\n\n");
}
#endif

static int queue_probe(trinarkular_driver_t *drv, struct req_wrap *rw)
{
  // start from last_req_rx and search for correct insertion point
  struct req_wrap *this_rx = MY(drv)->last_req_rx;
  struct req_wrap *prev_rx = NULL;

  //trinarkular_log("inserting req with rx time: %"PRIu64, rw->rx_time);

  // the lists are empty
  if (this_rx == NULL) {
    assert(MY(drv)->next_req_rx == NULL);
    MY(drv)->last_req_rx = rw;
    MY(drv)->next_req_rx = rw;
    return 0;
  }

  // find the first node with rx_time < rw->rx_time
  while (this_rx != NULL && this_rx->rx_time > rw->rx_time) {
    prev_rx = this_rx;
    this_rx = this_rx->prev;
  }

  // this_rx is either NULL or has lower or eq time
  rw->prev = this_rx;
  if (this_rx != NULL) {
    this_rx->next = rw;
  }

  // prev_rx is either NULL or has higher time
  rw->next = prev_rx;
  if (prev_rx != NULL) {
    prev_rx->prev = rw;
  }

  // update end pointers
  if (this_rx == MY(drv)->last_req_rx) {
    MY(drv)->last_req_rx = rw;
  }

  if (prev_rx == MY(drv)->next_req_rx) {
    MY(drv)->next_req_rx = rw;
  }

  return 0;
}

static struct req_wrap *check_for_response(trinarkular_driver_t *drv,
                                           uint64_t time)
{
  struct req_wrap *this_rw = MY(drv)->next_req_rx;

  //trinarkular_log("popping at time %"PRIu64, time);
  //dump_queues(drv);

  // pop next_req_rx if rx_time <= time
  if (this_rw != NULL && this_rw->rx_time <= time) {
    // detach it
    if (this_rw->next != NULL) {
      this_rw->next->prev = NULL;
    }

    // set new head
    MY(drv)->next_req_rx = this_rw->next;

    // set new tail?
    if (MY(drv)->last_req_rx == this_rw) {
      MY(drv)->last_req_rx = this_rw->next;
    }
  } else {
    this_rw = NULL;
  }

  //dump_queues(drv);
  //trinarkular_log("popped %"PRIu64, this_rw ? this_rw->rx_time : 0);

  return this_rw;
}

static int send_probe(trinarkular_driver_t *drv, struct req_wrap *rw)
{
  uint64_t timeout;

  // should this probe be responsive?
  if (rw->responsive_target != 0 && (rand() % 100) >= MY(drv)->unresp_probes) {
    // generate an rtt
    rw->rtt = timeout = rand() % MY(drv)->max_rtt;
  } else { // unresponsive probe
    rw->rtt = 0;
    timeout = rw->req.wait;
  }
  if (rw->rtt > (rw->req.wait)) { // probe timeout
    rw->rtt = 0;
    timeout = rw->req.wait;
  }

  rw->probe_tx++;
  rw->rx_time = zclock_time() + timeout;

  // "send" the request
  if (queue_probe(drv, rw) != 0) {
    return -1;
  }

  return 0;
}

// runs in the driver thread
static int handle_resp_timer(zloop_t *loop, int timer_id, void *arg)
{
  trinarkular_driver_t *drv = (trinarkular_driver_t *)arg;
  struct req_wrap *rw = NULL;
  trinarkular_probe_resp_t resp;

  uint64_t now = zclock_time();

  while ((rw = check_for_response(drv, now)) != NULL) {
    // do we need to send another probe?
    if (rw->rtt == 0 && rw->probe_tx < rw->req.probecount) {
      if (send_probe(drv, rw) != 0) {
        return -1;
      }
      goto done;
    }

    resp.seq_num = rw->seq_num;
    resp.target_ip = rw->req.target_ip;
    resp.rtt = rw->rtt;
    resp.probes_sent = rw->probe_tx;

    // was this a "response" or a timeout?
    if (rw->rtt != 0) {
      resp.verdict = TRINARKULAR_PROBE_RESPONSIVE;
    } else {
      // give up
      assert(rw->probe_tx == rw->req.probecount);
      resp.verdict = TRINARKULAR_PROBE_UNRESPONSIVE;
    }

    // yield this response to the user thread
    if (trinarkular_driver_yield_resp(drv, &resp) != 0) {
      return -1;
    }

    // destroy the state
    free(rw);
    rw = NULL;
  }

 done:
  return 0;
}

static void usage(char *name)
{
  fprintf(stderr,
          "Driver usage: %s [options]\n"
          "       -r <max-rtt>      maximum simulated RTT (default: %d)\n"
          "       -u <0 - 100>     %% of unresponsive probes (default: %d%%)\n"
          "       -U <0 - 100>     %% of unresponsive targets (default: %d%%)\n",
          name,
          MAX_RTT,
          UNRESP_PROBES,
          UNRESP_TARGETS);
}

static int parse_args(trinarkular_driver_t *drv, int argc, char **argv)
{
  int opt;
  int prevoptind;

    while(prevoptind = optind,
	(opt = getopt(argc, argv, ":r:u:U:?")) >= 0)
    {
      if (optind == prevoptind + 2 &&
          optarg && *optarg == '-' && *(optarg+1) != '\0') {
        opt = ':';
        -- optind;
      }
      switch(opt)
	{
	case 'r':
          MY(drv)->max_rtt = strtoull(optarg, NULL, 10);
          break;

        case 'u':
          MY(drv)->unresp_probes = strtol(optarg, NULL, 10);
          break;

        case 'U':
          MY(drv)->unresp_targets = strtol(optarg, NULL, 10);
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

    return 0;
}

/* ==================== PUBLIC API FUNCTIONS ==================== */

trinarkular_driver_t *
trinarkular_driver_test_alloc()
{
  test_driver_t *drv = NULL;

  if ((drv = malloc(sizeof(test_driver_t))) == NULL) {
    trinarkular_log("ERROR: failed");
    return NULL;
  }

  // set our subclass fields to 0
  memset(drv, 0, sizeof(test_driver_t));

  // copy the superclass onto our class
  memcpy(drv, &clz, sizeof(trinarkular_driver_t));

  return (trinarkular_driver_t *)drv;
}

int trinarkular_driver_test_init(trinarkular_driver_t *drv,
                                 int argc, char **argv)
{
  // set default args
  MY(drv)->max_rtt = MAX_RTT;
  MY(drv)->unresp_probes = UNRESP_PROBES;
  MY(drv)->unresp_targets = UNRESP_TARGETS;

  if (parse_args(drv, argc, argv) != 0) {
    return -1;
  }

  trinarkular_log("done");

  return 0;
}

void trinarkular_driver_test_destroy(trinarkular_driver_t *drv)
{
  struct req_wrap *this_rw = NULL;
  struct req_wrap *next_rw = NULL;

  if (drv == NULL) {
    return;
  }

  // destroy any outstanding reqs
  this_rw = MY(drv)->next_req_rx;
  while(this_rw != NULL) {
    // preserve next
    next_rw = this_rw->next;

    if (this_rw->prev != NULL) {
      this_rw->prev->next = this_rw->next;
    }
    if (next_rw != NULL) {
      next_rw->prev = this_rw->prev;
    }

    free(this_rw);

    this_rw = next_rw;
  }

  MY(drv)->next_req_rx = NULL;
  MY(drv)->last_req_rx = NULL;
}

// called with driver thread

int trinarkular_driver_test_init_thr(trinarkular_driver_t *drv)
{
  if(zloop_timer(TRINARKULAR_DRIVER_ZLOOP(drv),
                 RESP_TIMER, 0,
                 handle_resp_timer, drv) < 0) {
    trinarkular_log("ERROR: Could not send probe");
    return -1;
  }

  return 0;
}

int trinarkular_driver_test_handle_req(trinarkular_driver_t *drv,
                                       seq_num_t seq_num,
                                       trinarkular_probe_req_t *req)
{
  struct req_wrap *rw = NULL;

  if ((rw = malloc_zero(sizeof(struct req_wrap))) == NULL) {
    trinarkular_log("ERROR: Could not allocate request");
    return -1;
  }

  rw->seq_num = seq_num;
  rw->req = *req;
  rw->rtt = 0; // set in send_probe
  rw->responsive_target = ((rand() % 100) >= MY(drv)->unresp_targets) ? 1 : 0;
  rw->probe_tx = 0; // incremented in send_probe

  if (send_probe(drv, rw) != 0) {
    free(rw);
    return -1;
  }

  return 0;
}
