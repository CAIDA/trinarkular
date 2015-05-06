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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <czmq.h>

#include "utils.h"

#include "trinarkular.h"
#include "trinarkular_log.h"
#include "trinarkular_probelist.h"
#include "trinarkular_prober.h"

/** Number of msec that the prober has to complete one round of periodic
    probing (default: 10min) */
#define TRINARKULAR_PERIODIC_ROUND_DURATION 600000

/** Number of periodic "slices" that the round is divided into. The probelist is
    divided into slices, and probes for all targets in a slice are queued
    simultaneously. */
#define TRINARKULAR_PERIODIC_ROUND_SLICES 60

/** To be run within a zloop handler */
#define CHECK_INTERRUPTS                                        \
  do {                                                          \
    if (zctx_interrupted != 0 || prober->shutdown != 0) {       \
      trinarkular_log("Caught SIGINT");                         \
      return -1;                                                \
    }                                                           \
  } while(0)


/* Structure representing a prober instance */
struct trinarkular_prober {

  /** Has this prober been started? */
  int started;

  /** Should the prober shut down? */
  int shutdown;

  /** Probelist that this prober is probing */
  trinarkular_probelist_t *pl;

  /** zloop reactor instance */
  zloop_t *loop;

  /** Periodic probe timer ID */
  int periodic_timer_id;


  /* ==== Periodic Probing State ==== */

  /** The number of /24s that are in a slice */
  int slice_size;

  /** The current slice (i.e. how many times the slice timer has fired) */
  uint64_t current_slice;

};

/** Structure to be attached to a /24 in the probelist */
typedef struct prober_slash24_state {

  /** When was this /24 last probed? */
  uint64_t last_periodic_probe_time;

  /** When did we last handle a periodic response for this /24? (or timeout) */
  uint64_t last_periodic_resp_time;

} prober_slash24_state_t;

static void prober_slash24_state_destroy(void *user)
{
  prober_slash24_state_t *state = (prober_slash24_state_t*)user;

  free(state);
}

static uint32_t get_next_host(trinarkular_prober_t *prober)
{
  // have we reached the end of the hosts?
  if (trinarkular_probelist_has_more_host(prober->pl) == 0) {
    trinarkular_probelist_first_host(prober->pl);
  } else {
    // move to the next host
    trinarkular_probelist_next_host(prober->pl);
  }

  // we have have reached the end now
  if (trinarkular_probelist_has_more_host(prober->pl) == 0) {
    trinarkular_probelist_first_host(prober->pl);
  }
  // now there MUST be a valid host!
  assert(trinarkular_probelist_has_more_host(prober->pl) != 0);

  return trinarkular_probelist_get_host_ip(prober->pl);
}

/** Queue a probe for the currently iterated /24 */
static int queue_periodic_slash24(trinarkular_prober_t *prober)
{
  prober_slash24_state_t *slash24_state = NULL;
  uint32_t network_ip;
  uint32_t host_ip;
  uint32_t tmp;
  char netbuf[INET_ADDRSTRLEN];
  char ipbuf[INET_ADDRSTRLEN];

  // first, get the user state for this /24
  if ((slash24_state =
       trinarkular_probelist_get_slash24_user(prober->pl)) == NULL) {
    // need to first create the state
    if ((slash24_state = malloc_zero(sizeof(prober_slash24_state_t))) == NULL) {
      trinarkular_log("ERROR: Could not create slash24 state");
      return -1;
    }

    // now, attach it
    if (trinarkular_probelist_set_slash24_user(prober->pl,
                                               prober_slash24_state_destroy,
                                               slash24_state) != 0) {
      prober_slash24_state_destroy(slash24_state);
      return -1;
    }

    // and ask for the hosts to be randomized
    if (trinarkular_probelist_slash24_randomize_hosts(prober->pl,
                                                      zclock_time()) != 0) {
      prober_slash24_state_destroy(slash24_state);
      return -1;
    }
  }

  assert(slash24_state != NULL);
  // slash24_state is valid here

  network_ip =
    trinarkular_probelist_get_network_ip(prober->pl);
  tmp = htonl(network_ip);
  inet_ntop(AF_INET, &tmp, netbuf, INET_ADDRSTRLEN);

  // issue a warning if we did not get a response during the previous round
  if (slash24_state->last_periodic_probe_time > 0 &&
      slash24_state->last_periodic_resp_time == 0) {
    trinarkular_log("WARN: Re-probing %s before response received",
                    netbuf);
  }

  // identify the appropriate host to probe
  host_ip = get_next_host(prober);
  tmp = htonl(host_ip);
  inet_ntop(AF_INET, &tmp, ipbuf, INET_ADDRSTRLEN);

  // indicate that we are waiting for a response
  slash24_state->last_periodic_resp_time = 0;

  // mark when we requested the probe
  slash24_state->last_periodic_probe_time = zclock_time();

  trinarkular_log("queueing probe to %s in %s", ipbuf, netbuf);

  // TODO: Send to probe driver

  return 0;
}

/** Queue the next set of periodic probes */
static int handle_timer(zloop_t *loop, int timer_id, void *arg)
{
  trinarkular_prober_t *prober = (trinarkular_prober_t *)arg;
  int slice_cnt = 0;
  int queued_cnt = 0;

  uint64_t probing_round =
    prober->current_slice / TRINARKULAR_PERIODIC_ROUND_SLICES;

  // have we reached the end of the probelist and need to start over?
  if (trinarkular_probelist_has_more_slash24(prober->pl) == 0) {
    // only reset if the round has ended (should only happen when probelist is
    // smaller than slice count
    if ((prober->current_slice % TRINARKULAR_PERIODIC_ROUND_SLICES) != 0) {
      trinarkular_log("No /24s left to probe in round %"PRIu64, probing_round);
      goto done;
    }

    trinarkular_log("Starting probing round %d", probing_round);
    trinarkular_probelist_first_slash24(prober->pl);
  }

  for (slice_cnt=0;
       trinarkular_probelist_has_more_slash24(prober->pl) &&
         slice_cnt < prober->slice_size;
       trinarkular_probelist_next_slash24(prober->pl),
         slice_cnt++) {
    if (queue_periodic_slash24(prober) != 0) {
      return -1;
    }
    queued_cnt++;
  }

  trinarkular_log("Queued %d /24s in slice %"PRIu64" (round: %"PRIu64")",
                  queued_cnt,
                  prober->current_slice,
                  probing_round);

 done:
  prober->current_slice++;
  CHECK_INTERRUPTS;
  return 0;
}

trinarkular_prober_t *
trinarkular_prober_create()
{
  trinarkular_prober_t *prober;
  uint32_t periodic_timeout;

  if ((prober = malloc_zero(sizeof(trinarkular_prober_t))) == NULL) {
    trinarkular_log("ERROR: Could not create prober object");
    return NULL;
  }

  // create the reactor
  if ((prober->loop = zloop_new()) == NULL) {
    trinarkular_log("ERROR: Could not initialize reactor");
    goto err;
  }

  // create the periodic probe timer
  periodic_timeout =
    TRINARKULAR_PERIODIC_ROUND_DURATION / TRINARKULAR_PERIODIC_ROUND_SLICES;
  if (periodic_timeout < 100) {
    trinarkular_log("WARN: Periodic timer is set to fire every %dms",
                    periodic_timeout);
  }
  if ((prober->periodic_timer_id = zloop_timer(prober->loop,
                                               periodic_timeout, 0,
                                               handle_timer, prober)) < 0) {
    trinarkular_log("ERROR: Could not create periodic timer");
    goto err;
  }

  trinarkular_log("done");

  return prober;

 err:
  trinarkular_prober_destroy(prober);
  return NULL;
}

void
trinarkular_prober_destroy(trinarkular_prober_t *prober)
{
  zloop_destroy(&prober->loop);

  // are there any outstanding probes?
  // TODO

  trinarkular_probelist_destroy(prober->pl);
  prober->pl = NULL;

  free(prober);
}

void
trinarkular_prober_assign_probelist(trinarkular_prober_t *prober,
                                    trinarkular_probelist_t *pl)
{
  assert(prober != NULL);
  assert(prober->started == 0);

  prober->pl = pl;

  // we now randomize the /24 ordering
  trinarkular_probelist_randomize_slash24s(pl, zclock_time());

  // reset to the first /24
  trinarkular_probelist_first_slash24(pl);

  // compute the slice size
  prober->slice_size = trinarkular_probelist_get_slash24_cnt(pl) /
    TRINARKULAR_PERIODIC_ROUND_SLICES;
  if (prober->slice_size * TRINARKULAR_PERIODIC_ROUND_SLICES <
      trinarkular_probelist_get_slash24_cnt(pl)) {
    // round up to ensure we cover everything in the interval
    prober->slice_size++;
  }
  trinarkular_log("Probelist size: %d /24s",
                  trinarkular_probelist_get_slash24_cnt(pl));
  trinarkular_log("Periodic Probing Slice Size: %d", prober->slice_size);
}

int
trinarkular_prober_start(trinarkular_prober_t *prober)
{
  assert(prober != NULL);
  assert(prober->started == 0);

  if (prober->pl == NULL ||
      trinarkular_probelist_get_slash24_cnt(prober->pl) == 0) {
    trinarkular_log("ERROR: Missing or empty probelist. Refusing to start");
    return -1;
  }

  prober->started = 1;

  // set up handlers for scamper daemon
  // TODO

  trinarkular_log("prober up and running");

  zloop_start(prober->loop);

  return 0;
}

void
trinarkular_prober_stop(trinarkular_prober_t *prober)
{
  assert(prober != NULL);

  prober->shutdown = 1;

  trinarkular_log("waiting to shut down");
}
