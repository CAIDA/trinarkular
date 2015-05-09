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

#include "trinarkular_log.h"
#include "trinarkular_driver_interface.h"

#include "trinarkular_driver_test.h"

/** The maximum RTT that will be simulated */
#define MAX_RTT 3000

/** % of unresponsive probes */
#define UNRESP_PROBES 0

/** % of unresponsive targets */
#define UNRESP_TARGETS 0

#define MY(drv) ((test_driver_t*)(drv))

struct req_wrap {
  seq_num_t seq_num;
  trinarkular_probe_req_t req;
  uint64_t rtt; // if 0 this probe was not responded to
  uint8_t responsive_target;
  int probe_tx;
};

KHASH_INIT(int_req, int, struct req_wrap, 1,
           kh_int_hash_func, kh_int_hash_equal);

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

  /** Hash of outstanding requests */
  khash_t(int_req) *reqs;

} test_driver_t;

/** Our class instance */
static test_driver_t clz = {
  TRINARKULAR_DRIVER_HEAD_INIT(TRINARKULAR_DRIVER_ID_TEST, "test", test)
};

static int handle_probe_resp(zloop_t *loop, int timer_id, void *arg);

static int send_probe(trinarkular_driver_t *drv, struct req_wrap *rw)
{
  int timer_id;
  khiter_t k;
  int khret;
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

  // "send" the request
  if((timer_id = zloop_timer(TRINARKULAR_DRIVER_ZLOOP(drv),
                             timeout, 1,
                             handle_probe_resp, drv)) < 0) {
    trinarkular_log("ERROR: Could not send probe");
    return -1;
  }

  rw->probe_tx++;

  k = kh_put(int_req, MY(drv)->reqs, timer_id, &khret);
  if (khret == -1) {
    trinarkular_log("ERROR: Could not add request to hash");
    return -1;
  }
  kh_val(MY(drv)->reqs, k) = *rw;

  return 0;
}

// runs in the driver thread
static int handle_probe_resp(zloop_t *loop, int timer_id, void *arg)
{
  trinarkular_driver_t *drv = (trinarkular_driver_t *)arg;
  khiter_t k;
  struct req_wrap *rw = NULL;
  trinarkular_probe_resp_t resp;


  // get the req that matches this timer
  if ((k = kh_get(int_req, MY(drv)->reqs, timer_id)) == kh_end(MY(drv)->reqs)) {
    trinarkular_log("WARN: Response received for unknown request (%"PRIu64")");
    goto done;
  }
  rw = &kh_val(MY(drv)->reqs, k);
  assert(rw != NULL);

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

 done:
  // remove from the hash
  kh_del(int_req, MY(drv)->reqs, k);
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
  // create our request hash
  if ((MY(drv)->reqs = kh_init(int_req)) == NULL) {
    trinarkular_log("ERROR: Could not create req hash");
    return -1;
  }

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
  if (drv == NULL) {
    return;
  }

  kh_destroy(int_req, MY(drv)->reqs);
  MY(drv)->reqs = NULL;
}

// called with driver thread

int trinarkular_driver_test_init_thr(trinarkular_driver_t *drv)
{
  // we don't need any special state in the thread
  return 0;
}

int trinarkular_driver_test_handle_req(trinarkular_driver_t *drv,
                                       seq_num_t seq_num,
                                       trinarkular_probe_req_t *req)
{
  struct req_wrap rw;

  rw.seq_num = seq_num;
  rw.req = *req;
  rw.rtt = 0; // set in send_probe
  rw.responsive_target = ((rand() % 100) >= MY(drv)->unresp_targets) ? 1 : 0;
  rw.probe_tx = 0; // incremented in send_probe

  if (send_probe(drv, &rw) != 0) {
    return -1;
  }

  return 0;
}
