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

#include <czmq.h>

#include "khash.h"

#include "trinarkular_log.h"
#include "trinarkular_driver_interface.h"

#include "trinarkular_driver_test.h"

/** The maximum RTT that will be simulated */
#define MAX_RTT 3000

#define MY(drv) ((test_driver_t*)(drv))

struct req_wrap {
  uint64_t seq_num;
  trinarkular_probe_req_t req;
  uint64_t rtt;
};

KHASH_INIT(int_req, int, struct req_wrap, 1,
           kh_int_hash_func, kh_int_hash_equal);

/** Our 'subclass' of the generic driver */
typedef struct test_driver {
  TRINARKULAR_DRIVER_HEAD_DECLARE

  // add our fields here

  /** Hash of outstanding requests */
  khash_t(int_req) *reqs;

} test_driver_t;

/** Our class instance */
static test_driver_t clz = {
  TRINARKULAR_DRIVER_HEAD_INIT(TRINARKULAR_DRIVER_ID_TEST, "test", test)
};

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

  // create a response
  resp.seq_num = rw->seq_num;
  resp.target_ip = rw->req.target_ip;
  resp.rtt = rw->rtt;

  // yield this response to the user thread
  if (trinarkular_driver_yield_resp(drv, &resp) != 0) {
    return -1;
  }

 done:
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

int trinarkular_driver_test_handle_req(trinarkular_driver_t *drv,
                                       uint64_t seq_num,
                                       trinarkular_probe_req_t *req)
{
  uint64_t rtt;
  int timer_id;

  khiter_t k;
  int khret;

  // generate an rtt
  rtt = rand() % MAX_RTT;

  // "send" the request
  if((timer_id = zloop_timer(TRINARKULAR_DRIVER_ZLOOP(drv),
                             rtt, 1,
                             handle_probe_resp, drv)) < 0) {
    trinarkular_log("ERROR: Could not send probe");
    goto err;
  }

  // add to our hash of outstanding requests
  k = kh_put(int_req, MY(drv)->reqs, seq_num, &khret);
  if (khret == -1) {
    trinarkular_log("ERROR: Could not add request to hash");
    goto err;
  }
  kh_val(MY(drv)->reqs, k).seq_num = seq_num;
  kh_val(MY(drv)->reqs, k).req = *req;
  kh_val(MY(drv)->reqs, k).rtt = rtt;

  // done:
  return 0;

 err:
  return -1;
}
