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
#include "trinarkular_probe_io.h"

#include "trinarkular_driver_test.h"

/** The maximum RTT that will be simulated */
#define MAX_RTT 3000

struct req_wrap {
  uint64_t seq_num;
  trinarkular_probe_req_t req;
  uint64_t rtt;
};

KHASH_INIT(int_req, int, struct req_wrap, 1,
           kh_int_hash_func, kh_int_hash_equal);

typedef struct simulator_state {

  /** The simulator has shutdown */
  int shutdown;

  /** The simulator's reactor */
  zloop_t *loop;

  /** The simulator's connection to the parent driver */
  void *parent;

  /** Hash of outstanding requests */
  khash_t(int_req) *reqs;

} simulator_state_t;

/** Our 'subclass' of the generic driver */
typedef struct test_driver {
  TRINARKULAR_DRIVER_HEAD_DECLARE

  // add our fields here

  zactor_t *sim_actor;

  simulator_state_t sim_state;

} test_driver_t;

/** Our class instance */
static test_driver_t clz = {
  TRINARKULAR_DRIVER_HEAD_INIT(TRINARKULAR_DRIVER_ID_TEST, "test", test)
};

static int handle_probe_resp(zloop_t *loop, int timer_id, void *arg)
{
  simulator_state_t *state = (simulator_state_t *)arg;
  khiter_t k;
  struct req_wrap *rw = NULL;
  trinarkular_probe_resp_t resp;

  // get the req that matches this timer
  if ((k = kh_get(int_req, state->reqs, timer_id)) == kh_end(state->reqs)) {
    trinarkular_log("WARN: Response received for unknown request (%"PRIu64")");
    goto done;
  }
  rw = &kh_val(state->reqs, k);
  assert(rw != NULL);

  // create a response
  resp.seq_num = rw->seq_num;
  resp.target_ip = rw->req.target_ip;
  resp.rtt = rw->rtt;

  // send the response to our parent thread
  if (trinarkular_probe_resp_send(state->parent, &resp) != 0) {
    return -1;
  }

 done:
  return 0;
}

static int handle_parent_msg(zloop_t *loop, zsock_t *reader, void *arg)
{
  simulator_state_t *state = (simulator_state_t *)arg;

  char *command = NULL;

  uint64_t seq_num;
  trinarkular_probe_req_t req;

  uint64_t rtt;
  int timer_id;

  khiter_t k;
  int khret;

  if ((command = trinarkular_probe_recv_str(state->parent, 0)) == NULL) {
    goto shutdown;
  }

  if (strcmp("$TERM", command) == 0) {
    goto shutdown;
  }

  if (strcmp("REQ", command) == 0) {
    if ((seq_num = trinarkular_probe_req_recv(state->parent, &req)) == 0) {
      goto shutdown;
    }

    // generate an rtt
    rtt = rand() % MAX_RTT;

    // "send" the request
    if((timer_id = zloop_timer(state->loop, rtt, 1,
                               handle_probe_resp, state)) < 0)
    {
      trinarkular_log("ERROR: Could not send probe");
      goto shutdown;
    }

    // add to our hash of outstanding requests
    k = kh_put(int_req, state->reqs, seq_num, &khret);
    if (khret == -1) {
      trinarkular_log("ERROR: Could not add request to hash");
      goto shutdown;
    }
    kh_val(state->reqs, k).seq_num = seq_num;
    kh_val(state->reqs, k).req = req;
    kh_val(state->reqs, k).rtt = rtt;
  }

  // done:
  free(command);
  return 0;

 shutdown:
  free(command);
  state->shutdown = 1;
  return -1;
}

static void sim_destroy(simulator_state_t *state)
{
  if (state == NULL) {
    return;
  }

  kh_destroy(int_req, state->reqs);
  state->reqs = NULL;

  zloop_destroy(&state->loop);
}

/* Our silly 'prober' actor */
static void sim_run(zsock_t *pipe, void *args)
{
  simulator_state_t *state = (simulator_state_t *)args;

  // create our request hash
  if ((state->reqs = kh_init(int_req)) == NULL) {
    trinarkular_log("ERROR: Could not create req hash");
    goto shutdown;
  }

  state->parent = zsock_resolve(pipe);

  if ((state->loop = zloop_new()) == NULL) {
    trinarkular_log("ERROR: Could not create loop");
    goto shutdown;
  }

  // poll the parent socket for commands
  if(zloop_reader(state->loop, state->parent, handle_parent_msg, state) != 0) {
    trinarkular_log("ERROR: Could not add reader to loop");
    goto shutdown;
  }

  // signal that we are ready for messages
  if(zsock_signal(pipe, 0) != 0) {
    trinarkular_log("ERROR: Could not send ready signal to parent");
    goto shutdown;
  }

  zloop_start(state->loop);

  trinarkular_log("probe simulator shutting down");

 shutdown:
  state->shutdown = 1;
  sim_destroy(state);
  return;
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

  memset(drv, 0, sizeof(test_driver_t));
  memcpy(drv, &clz, sizeof(trinarkular_driver_t));

  return (trinarkular_driver_t *)drv;
}

int trinarkular_driver_test_init(trinarkular_driver_t *drv,
                                 int argc, char **argv)
{
  test_driver_t *this = (test_driver_t *)drv;

  trinarkular_log("starting probe simulator");

  if((this->sim_actor =
      zactor_new(sim_run, &this->sim_state)) == NULL)
    {
      trinarkular_log("ERROR: Could not start simulator");
      return -1;
    }

  /* by the time the zactor_new function returns, the simulator has been
   initialized, so lets check for any error messages that it has signaled */
  if (this->sim_state.shutdown != 0) {
    trinarkular_log("Simulator has already shut down");
    return -1;
  }

  // store the actor's socket for users to poll looking for responses
  TRINARKULAR_DRIVER_RESP_SOCKET(drv) = zactor_resolve(this->sim_actor);
  assert(TRINARKULAR_DRIVER_RESP_SOCKET(drv) != NULL);

  trinarkular_log("done");

  return 0;
}

void trinarkular_driver_test_destroy(trinarkular_driver_t *drv)
{
  test_driver_t *this = (test_driver_t *)drv;

  /* shut the simulator down */
  zactor_destroy(&this->sim_actor);

  free(drv);
}

uint64_t trinarkular_driver_test_queue(trinarkular_driver_t *drv,
                                       trinarkular_probe_req_t req)
{
  uint64_t seq_num = TRINARKULAR_DRIVER_NEXT_SEQ_NUM(drv);

  // use a helper func to send the request to the simulator
  if (trinarkular_probe_req_send(TRINARKULAR_DRIVER_RESP_SOCKET(drv),
                                 seq_num, &req) != 0) {
    return 0;
  }

  return seq_num;
}

int trinarkular_driver_test_recv(trinarkular_driver_t *drv,
                                 trinarkular_probe_resp_t *resp,
                                 int blocking)
{
  char *command;

  // check for a RESP command
  if ((command =
       trinarkular_probe_recv_str(TRINARKULAR_DRIVER_RESP_SOCKET(drv),
                                  blocking ? 0 : ZMQ_DONTWAIT))
      == NULL) {
    goto err;
  }
  if (blocking == 0 && command == NULL) {
    return 0;
  }
  if (strcmp("RESP", command) != 0) {
    trinarkular_log("ERROR: Invalid command (%s) received", command);
  }

  if (trinarkular_probe_resp_recv(TRINARKULAR_DRIVER_RESP_SOCKET(drv),
                                  resp) != 0) {
    goto err;
  }

  free(command);
  return 1;

 err:
  free(command);
  return -1;
}
