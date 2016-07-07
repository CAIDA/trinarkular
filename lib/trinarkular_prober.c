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

#include "trinarkular_prober.h"
#include "trinarkular_driver_interface.h"
#include "trinarkular.h"
#include "trinarkular_driver.h"
#include "trinarkular_log.h"
#include "trinarkular_probelist.h"
#include "config.h"
#include "khash.h"
#include "utils.h"
#include <arpa/inet.h>
#include <assert.h>
#include <czmq.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

/** Print debugging info about adaptive/recovery probes */
/* #define DEBUG_PROBING */

/** To be run within a zloop handler */
#define CHECK_SHUTDOWN                                                         \
  do {                                                                         \
    if (zctx_interrupted != 0 || prober->shutdown != 0) {                      \
      trinarkular_log("Interrupted, shutting down");                           \
      return -1;                                                               \
    }                                                                          \
  } while (0)

// global metric prefix
#define METRIC_PREFIX "active.trinarkular"

// metric prefix for per-/24 info
#define METRIC_PREFIX_SLASH24 METRIC_PREFIX ""

// metric prefix for prober metadata
#define METRIC_PREFIX_PROBER METRIC_PREFIX ".probers"

#define CH_SLASH24 "__PFX_%s_24"

/** Possible probe types */
enum {
  UNPROBED = 0,
  PERIODIC = 1,
  ADAPTIVE = 2,
  RECOVERY = 3,
  PROBE_TYPE_CNT = 4
};

static char *probe_types[] = {
  "unprobed", // 0 => UNPROBED
  "periodic", // 1 => PERIODIC
  "adaptive", // 2 => ADAPTIVE
  "recovery", // 3 => RECOVERY
};

/** Possible bayesian inference states for a /24 */
enum { UNCERTAIN = 0, DOWN = 1, UP = 2, BELIEF_STATE_CNT = 3 };

static char *belief_states[] = {
  "uncertain", // 0 => UNCERTAIN
  "down",      // 1 => DOWN
  "up",        // 2 => UP
};

#ifdef DEBUG_PROBING
static char *belief_icons[] = {
  "?", // 0 => UNCERTAIN
  "✘", // 1 => DOWN
  "✔", // 2 => UP
};
#endif

/** The number of recovery probes that may be sent for various A(E(b)) values.
 * To use this table, scale A(E(b)) by 100, and then cast to int and use the
 * result as an index.
 * E.g. A(E(b)) = 0.4325 => 43.25 => 43 => recovery_probe_cnt[43] = XX
 *
 * Array was generated using:
 * perl -e 'use POSIX ceil; foreach $i (1..99) { $x =
 * ceil(log(0.2)/log(1-$i/100)); $x=-1 if ($i < 10); print "$x, "; }'
 */
static int recovery_probe_cnt[] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, 16, 14, 13, 12, 11, 10, 10, 9, 9, 8, 8,
  7,  7,  7,  6,  6,  6,  6,  5,  5,  5,  5,  5,  5,  4,  4,  4,  4, 4, 4, 4,
  4,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  2, 2, 2, 2,
  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, 2, 2, 1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1,
};

#define AEB_TO_RECOVERY(s24) recovery_probe_cnt[(int)((s24)->aeb * 100)]

/** Stop exponential backoff once rounds_since_up reaches 16 */
#define RECOVERY_BACKOFF_MAX 16

#define RECOVERY_ELIGIBLE(state)                                               \
  ((state)->rounds_since_up <= 4 || (state)->rounds_since_up == 8 ||           \
   (state)->rounds_since_up % 16 == 0)

// set of ids that correspond to metrics in the kp
struct metrics {

  /** Value is the current round ID */
  int round_id;

  /** Value is NOW - round_start */
  int round_duration;

  /** Value is the number of /24s probed in the round */
  int round_probe_cnt[PROBE_TYPE_CNT];

  /** Value is the number of /24s that we got responses for in the round */
  int round_probe_complete_cnt[PROBE_TYPE_CNT];

  /** Value is the number of /24s that appeared alive this round */
  int round_responsive_cnt[PROBE_TYPE_CNT];

  /** Values are the number of /24s in each state */
  int slash24_state_cnts[BELIEF_STATE_CNT];

  /** Value is the number of /24s that we are probing */
  int slash24_cnt;
};

/** Max number of rounds that are currently being tracked. Most of the time this
    will just be 2 */
#define MAX_IN_PROGRESS_ROUNDS 100

typedef struct probing_stats {

  /** The walltime that the round started at */
  uint64_t start_time;

  /** The number of probes sent this round (PERIODIC and ADAPTIVE) */
  uint32_t probe_cnt[PROBE_TYPE_CNT];

  /** The number of responses received this round */
  uint32_t probe_complete_cnt[PROBE_TYPE_CNT];

  /** The number of responsive probes this round */
  uint32_t responsive_cnt[PROBE_TYPE_CNT];

  /** The number of /24s in each state */
  uint32_t slash24_state_cnts[BELIEF_STATE_CNT];

  /** The number of /24s that we are probing */
  uint32_t slash24_cnt;

} probing_stats_t;

struct driver_wrap {
  int id;
  trinarkular_driver_t *driver;
  trinarkular_prober_t *prober;
};

struct params {

  /** Defaults to TRINARKULAR_PERIODIC_ROUND_DURATION_DEFAULT */
  uint64_t periodic_round_duration;

  /** Defaults to TRINARKULAR_PERIODIC_ROUND_SLICES_DEFAULT */
  int periodic_round_slices;

  /** Defaults to -1, unlimited */
  int periodic_round_limit;

  /** Defaults to 3 */
  uint16_t periodic_probe_timeout;

  /** Defaults to 1 (sleep for alignment) */
  int sleep_align_start;
};

#define PARAM(pname) (prober->params.pname)
#define STAT(sname) (prober->stats.sname)

/** How often do we expect packet loss?
    (Taken from the paper) */
#define PACKET_LOSS_FREQUENCY 0.01

#define BELIEF_UP_FRAC 0.9
#define BELIEF_DOWN_FRAC 0.1
#define BELIEF_STATE(s)                                                        \
  (((s) < BELIEF_DOWN_FRAC) ? DOWN : ((s) > BELIEF_UP_FRAC) ? UP : UNCERTAIN)

/* Structure representing a prober instance */
struct trinarkular_prober {

  /** Prober name */
  char *name;

  /** Prober name (timeseries safe) */
  char *name_ts;

  /** Configuration parameters */
  struct params params;

  /** Libtimeseries instance */
  timeseries_t *ts;

  /** Key package */
  timeseries_kp_t *kp;

  /** Indexes into the KP */
  struct metrics metrics;

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

  /** Probe Driver instances */
  struct driver_wrap drivers[TRINARKULAR_PROBER_DRIVER_MAX_CNT];

  /** Number of prober drivers in use */
  int drivers_cnt;

  /** Index of the next driver to use for probing */
  int drivers_next;

  /** Probing statistics */
  probing_stats_t stats;

  /** Number of probes queued with the driver(s) */
  uint64_t outstanding_probe_cnt;

  /* ==== Periodic Probing State ==== */

  /** The number of /24s that are in a slice */
  int slice_size;

  /** The current slice (i.e. how many times the slice timer has fired) */
  uint64_t current_slice;

  /** Has probing started yet? */
  int probing_started;
};

static char *graphite_safe(char *p)
{
  if (p == NULL) {
    return p;
  }

  char *r = p;
  while (*p != '\0') {
    if (*p == '.') {
      *p = '-';
    }
    if (*p == '*') {
      *p = '-';
    }
    p++;
  }
  return r;
}

#define BUFFER_LEN 1024

static int init_kp(trinarkular_prober_t *prober)
{
  char buf[BUFFER_LEN];
  int i;

  // round id
  snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.meta.round_id",
           prober->name_ts);
  if ((prober->metrics.round_id = timeseries_kp_add_key(prober->kp, buf)) ==
      -1) {
    return -1;
  }

  // round duration
  snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.meta.round_duration",
           prober->name_ts);
  if ((prober->metrics.round_duration =
         timeseries_kp_add_key(prober->kp, buf)) == -1) {
    return -1;
  }

  for (i = PERIODIC; i <= RECOVERY; i++) {
    // probe cnt
    snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.probing.%s.probe_cnt",
             prober->name_ts, probe_types[i]);
    if ((prober->metrics.round_probe_cnt[i] =
           timeseries_kp_add_key(prober->kp, buf)) == -1) {
      return -1;
    }

    // probe complete cnt
    snprintf(buf, BUFFER_LEN,
             METRIC_PREFIX_PROBER ".%s.probing.%s.completed_probe_cnt",
             prober->name_ts, probe_types[i]);
    if ((prober->metrics.round_probe_complete_cnt[i] =
           timeseries_kp_add_key(prober->kp, buf)) == -1) {
      return -1;
    }

    // responsive cnt
    snprintf(buf, BUFFER_LEN,
             METRIC_PREFIX_PROBER ".%s.probing.%s.responsive_probe_cnt",
             prober->name_ts, probe_types[i]);
    if ((prober->metrics.round_responsive_cnt[i] =
           timeseries_kp_add_key(prober->kp, buf)) == -1) {
      return -1;
    }
  }

  for (i = UNCERTAIN; i < BELIEF_STATE_CNT; i++) {
    snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.states.%s_slash24_cnt",
             prober->name_ts, belief_states[i]);
    if ((prober->metrics.slash24_state_cnts[i] =
           timeseries_kp_add_key(prober->kp, buf)) == -1) {
      return -1;
    }
  }

  snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.slash24_cnt",
           prober->name_ts);
  if ((prober->metrics.slash24_cnt = timeseries_kp_add_key(prober->kp, buf)) ==
      -1) {
    return -1;
  }

  return 0;
}

static void set_default_params(struct params *params)
{
  // round duration (10 bin)
  params->periodic_round_duration =
    TRINARKULAR_PROBER_PERIODIC_ROUND_DURATION_DEFAULT;

  // slices (60, i.e. every 10s)
  params->periodic_round_slices =
    TRINARKULAR_PROBER_PERIODIC_ROUND_SLICES_DEFAULT;

  // round limit (unlimited)
  params->periodic_round_limit = -1;

  // probe timeout
  params->periodic_probe_timeout =
    TRINARKULAR_PROBER_PERIODIC_PROBE_TIMEOUT_DEFAULT;

  // sleep to align
  params->sleep_align_start = 1;
}

static int slash24_metrics_create(trinarkular_prober_t *prober,
                                  trinarkular_slash24_metrics_t *metrics,
                                  const char *slash24_string, const char *md)
{
  char buf[BUFFER_LEN];
  int i;
  uint64_t tmp;

  // belief
  snprintf(buf, BUFFER_LEN,
           METRIC_PREFIX_SLASH24 ".%s.probers.%s.blocks." CH_SLASH24 ".belief",
           md, prober->name_ts, slash24_string);
  if ((metrics->belief = timeseries_kp_add_key(prober->kp, buf)) == -1) {
    return -1;
  }

  // state
  snprintf(buf, BUFFER_LEN,
           METRIC_PREFIX_SLASH24 ".%s.probers.%s.blocks." CH_SLASH24 ".state",
           md, prober->name_ts, slash24_string);
  if ((metrics->state = timeseries_kp_add_key(prober->kp, buf)) == -1) {
    return -1;
  }

  // overall per-state stats
  for (i = UNCERTAIN; i < BELIEF_STATE_CNT; i++) {
    snprintf(buf, BUFFER_LEN,
             METRIC_PREFIX_SLASH24 ".%s.probers.%s.%s_slash24_cnt", md,
             prober->name_ts, belief_states[i]);
    if ((metrics->overall[i] = timeseries_kp_get_key(prober->kp, buf)) == -1 &&
        (metrics->overall[i] = timeseries_kp_add_key(prober->kp, buf)) == -1) {
      return -1;
    }
  }

  // add this /24 to the UP state
  tmp = timeseries_kp_get(prober->kp, metrics->overall[UP]);
  timeseries_kp_set(prober->kp, metrics->overall[UP], tmp + 1);

  return 0;
}

static trinarkular_slash24_state_t *
slash24_state_create(trinarkular_prober_t *prober, trinarkular_slash24_t *s24)
{
  trinarkular_slash24_state_t *state = NULL;
  uint32_t slash24_ip;
  char slash24_str[INET_ADDRSTRLEN];
  int i;

  // need to first create the state
  if ((state = trinarkular_slash24_state_create(s24->md_cnt)) == NULL) {
    trinarkular_log("ERROR: Could not create slash24 state");
    return NULL;
  }

  // initialize things
  state->last_probe_type = UNPROBED;
  ADAPTIVE_BUDGET_SET(state, TRINARKULAR_PROBER_ROUND_PROBE_BUDGET);
  RECOVERY_BUDGET_SET(state, AEB_TO_RECOVERY(s24));
  state->current_belief = 0.99; // as per paper
  state->current_state = BELIEF_STATE(state->current_belief);
  state->rounds_since_up = 0;

  // convert the /24 to a string
  slash24_ip = htonl(s24->network_ip);
  inet_ntop(AF_INET, &slash24_ip, slash24_str, INET_ADDRSTRLEN);
  graphite_safe(slash24_str);
  for (i = 0; i < s24->md_cnt; i++) {
    // create metrics for this metadata
    if (slash24_metrics_create(prober, &state->metrics[i], slash24_str,
                               s24->md[i]) != 0) {
      trinarkular_log("ERROR: Could not create slash24 metrics for %s",
                      s24->md[i]);
      return NULL;
    }
    // to save a little memory, we free the metadata strings
    free(s24->md[i]);
  }
  free(s24->md);
  s24->md = NULL;
  s24->md_cnt = 0;

  STAT(slash24_state_cnts[UP])++;
  STAT(slash24_cnt)++;

  // now, attach it
  if (trinarkular_probelist_save_slash24_state(prober->pl, s24, state) != 0) {
    goto err;
  }

  // and destroy it
  trinarkular_slash24_state_destroy(state);

  return state;

err:
  trinarkular_slash24_state_destroy(state);
  return NULL;
}

static void reset_round_stats(trinarkular_prober_t *prober, uint64_t start_time)
{
  int i;

  STAT(start_time) = start_time;

  for (i = UNPROBED; i < PROBE_TYPE_CNT; i++) {
    STAT(probe_cnt[i]) = 0;
    STAT(probe_complete_cnt[i]) = 0;
    STAT(responsive_cnt[i]) = 0;
  }
}

/** Queue a probe for the given /24 */
static int queue_slash24_probe(trinarkular_prober_t *prober,
                               trinarkular_slash24_t *s24,
                               trinarkular_slash24_state_t *state,
                               int probe_type)
{
  uint32_t host_ip;
  char ipbuf[INET_ADDRSTRLEN];

  trinarkular_probe_req_t req = {
    0, PARAM(periodic_probe_timeout),
  };
  struct driver_wrap *dw;

  int ret;

  assert(state != NULL);
  // slash24_state is valid here

  // identify the appropriate host to probe
  host_ip = trinarkular_probelist_get_next_host(s24, state);
  req.target_ip = htonl(host_ip);
  inet_ntop(AF_INET, &req.target_ip, ipbuf, INET_ADDRSTRLEN);

  // indicate that we are waiting for a response
  state->last_probe_type = probe_type;
  STAT(probe_cnt[probe_type])++;

  // decrement the probe budget (periodic doesn't affect this)
  // (its up to the caller to ensure that we have enough probes in the budget)
  if (probe_type == ADAPTIVE) {
    assert(ADAPTIVE_BUDGET(state) > 0);
    ADAPTIVE_BUDGET_SET(state, ADAPTIVE_BUDGET(state) - 1);
  } else if (probe_type == RECOVERY) {
    assert(RECOVERY_BUDGET(state) > 0);
    RECOVERY_BUDGET_SET(state, RECOVERY_BUDGET(state) - 1);
  }

  dw = &prober->drivers[prober->drivers_next];
  if ((ret = trinarkular_driver_queue_req(dw->driver, &req)) < 0) {
    return -1;
  }
  if (ret == REQ_DROPPED) {
    // reset the probe state
    state->last_probe_type = UNPROBED;
    // we leave probe budget and counters as they are
  } else {
    prober->outstanding_probe_cnt++;
  }

  prober->drivers_next = (prober->drivers_next + 1) % prober->drivers_cnt;

  // save the state
  if (trinarkular_probelist_save_slash24_state(prober->pl, s24, state) != 0) {
    return -1;
  }

  return 0;
}

static int end_of_round(trinarkular_prober_t *prober, int round_id)
{
  uint64_t now = zclock_time();
  uint64_t aligned_start =
    ((uint64_t)(STAT(start_time) / PARAM(periodic_round_duration))) *
    PARAM(periodic_round_duration);
  int i;

  timeseries_kp_set(prober->kp, prober->metrics.round_id, round_id);
  timeseries_kp_set(prober->kp, prober->metrics.round_duration,
                    now - STAT(start_time));

  for (i = PERIODIC; i < PROBE_TYPE_CNT; i++) {
    timeseries_kp_set(prober->kp, prober->metrics.round_probe_cnt[i],
                      STAT(probe_cnt[i]));
    timeseries_kp_set(prober->kp, prober->metrics.round_probe_complete_cnt[i],
                      STAT(probe_complete_cnt[i]));
    timeseries_kp_set(prober->kp, prober->metrics.round_responsive_cnt[i],
                      STAT(responsive_cnt[i]));
  }

  for (i = UNCERTAIN; i < BELIEF_STATE_CNT; i++) {
    timeseries_kp_set(prober->kp, prober->metrics.slash24_state_cnts[i],
                      STAT(slash24_state_cnts[i]));
  }

  timeseries_kp_set(prober->kp, prober->metrics.slash24_cnt, STAT(slash24_cnt));

  trinarkular_log("round %d completed in %" PRIu64 "ms (ideal: %" PRIu64 "ms)",
                  round_id, now - STAT(start_time),
                  PARAM(periodic_round_duration));
  trinarkular_log("round periodic response rate: %d/%d (%0.0f%%)",
                  STAT(responsive_cnt[PERIODIC]), STAT(probe_cnt[PERIODIC]),
                  STAT(responsive_cnt[PERIODIC]) * 100.0 /
                    STAT(probe_cnt[PERIODIC]));

  // flush the kp
  return timeseries_kp_flush(prober->kp, aligned_start / 1000);
}

/** Queue the next set of periodic probes */
static int handle_timer(zloop_t *loop, int timer_id, void *arg)
{
  trinarkular_prober_t *prober = (trinarkular_prober_t *)arg;
  int slice_cnt = 0;
  int queued_cnt = 0;

  uint64_t probing_round = prober->current_slice / PARAM(periodic_round_slices);

  uint64_t now = zclock_time();

  trinarkular_slash24_t *s24 = NULL;         // BORROWED
  trinarkular_slash24_state_t *state = NULL; // BORROWED

  CHECK_SHUTDOWN;

  // have we reached the end of the probelist and need to start over?
  if (prober->probing_started == 0 ||
      trinarkular_probelist_has_more_slash24(prober->pl) == 0) {
    // only reset if the round has ended (should only happen when probelist is
    // smaller than slice count
    if (prober->current_slice % PARAM(periodic_round_slices) != 0) {
      trinarkular_log("No /24s left to probe in round %" PRIu64, probing_round);
      goto done;
    }

    // dump end of round stats
    if (probing_round > 0) {
      trinarkular_log("ending round %d", probing_round - 1);

      if (end_of_round(prober, probing_round - 1) != 0) {
        trinarkular_log("WARN: Could not dump end-of-round stats");
      }
    }

    // check if we reached the round limit
    if (PARAM(periodic_round_limit) > 0 &&
        probing_round >= PARAM(periodic_round_limit)) {
      trinarkular_log("round limit (%d) reached, shutting down",
                      PARAM(periodic_round_limit));
      return -1;
    }

    trinarkular_log("starting round %d", probing_round);
    trinarkular_probelist_reset_slash24_iter(prober->pl);
    // reset round stats
    reset_round_stats(prober, now);

    prober->probing_started = 1;
  }

  // check if there is still 10 entire slices worth of requests
  // outstanding. this is a good indication that we are not keeping up with the
  // probing rate.
  // 2015-12-23 AK removes since now the driver will just drop probes
  // if (kh_size(prober->probes) > (prober->slice_size * 10)) {
  //  trinarkular_log("ERROR: %d outstanding requests (slice size is %d)",
  //                  kh_size(prober->probes), prober->slice_size);
  //  return -1;
  //}

  // trinarkular_log("INFO: %"PRIu64" outstanding requests (slice size is %d)",
  //                prober->outstanding_probe_cnt, prober->slice_size);

  for (slice_cnt = 0; slice_cnt < prober->slice_size; slice_cnt++) {
    // get a slash24 to probe
    if ((s24 = trinarkular_probelist_get_next_slash24(prober->pl)) == NULL) {
      // end of /24s
      break;
    }
    // get the state for this /24
    if ((state = trinarkular_probelist_get_slash24_state(prober->pl, s24)) ==
        NULL) {
      return -1;
    }

    // if we still haven't got a response to our last probe, lets give up on it
    // and send a new probe
    if (state->last_probe_type != UNPROBED) {
      trinarkular_log("INFO: re-probing /24 with last_probe_type of %d",
                      state->last_probe_type);
      state->last_probe_type = UNPROBED;
    }

    // reset the probe budgets
    ADAPTIVE_BUDGET_SET(state, TRINARKULAR_PROBER_ROUND_PROBE_BUDGET);
    RECOVERY_BUDGET_SET(state, AEB_TO_RECOVERY(s24));
    if (BELIEF_STATE(state->current_belief) == UP) {
      state->rounds_since_up = 0;
    } else {
      if (state->rounds_since_up < 255) {
        state->rounds_since_up++;
      } else {
        state->rounds_since_up = RECOVERY_BACKOFF_MAX;
      }
    }
    // queue a probe to a random host
    if (queue_slash24_probe(prober, s24, state, PERIODIC) != 0) {
      return -1;
    }

    queued_cnt++;
  }

  trinarkular_log("Queued %d /24s in slice %" PRIu64 " (round: %" PRIu64 ")",
                  queued_cnt, prober->current_slice, probing_round);

done:
  prober->current_slice++;
  CHECK_SHUTDOWN;
  return 0;
}

// update the bayesian model given a positive (1) or negative (0) probe response
static float update_bayesian_belief(trinarkular_slash24_t *s24,
                                    trinarkular_slash24_state_t *state,
                                    int probe_response)
{
  float Ppd;
  float new_belief_down;

  // P(p|~U)
  Ppd = (((1.0 - PACKET_LOSS_FREQUENCY) / TRINARKULAR_SLASH24_HOST_CNT) *
         (1.0 - state->current_belief));

  if (probe_response) {
    new_belief_down = Ppd / (Ppd + (s24->aeb * state->current_belief));
  } else {
    new_belief_down =
      (1.0 - Ppd) / ((1.0 - Ppd) + ((1 - s24->aeb) * state->current_belief));
  }

  // capping as per sec 4.2 of paper
  if (new_belief_down > 0.99) {
    new_belief_down = 0.99;
  } else if (new_belief_down < 0.01) {
    new_belief_down = 0.01;
  }

  return 1 - new_belief_down;
}

static int handle_driver_resp(zloop_t *loop, zsock_t *reader, void *arg)
{
  struct driver_wrap *dw = (struct driver_wrap *)arg;
  trinarkular_prober_t *prober = dw->prober;
  trinarkular_probe_resp_t resp;
  trinarkular_slash24_t *s24 = NULL;
  trinarkular_slash24_state_t *state = NULL;
  float new_belief_up;
  int i;
  uint64_t tmp;
  int key;

  CHECK_SHUTDOWN;

  if (trinarkular_driver_recv_resp(dw->driver, &resp, 0) != 1) {
    trinarkular_log("ERROR: Could not receive response");
    goto err;
  }

  // TARGET IP IS IN NETWORK BYTE ORDER

  // find the /24 for this probe
  if ((s24 = trinarkular_probelist_get_slash24(
         prober->pl, (ntohl(resp.target_ip) & TRINARKULAR_SLASH24_NETMASK))) ==
      NULL) {
    trinarkular_log("ERROR: Missing /24 for %x", ntohl(resp.target_ip));
    goto err;
  }
  prober->outstanding_probe_cnt--;

  // grab the state for this /24
  if ((state = trinarkular_probelist_get_slash24_state(prober->pl, s24)) ==
      NULL) {
    trinarkular_log("ERROR: Missing state for %x", ntohl(resp.target_ip));
    goto err;
  }

  // since periodic probing will reset adaptive probing state, this can cause
  // responses to become out of sync. simply ignore a response that we don't
  // care about. (NB: in practice this should rarely, if ever, happen since a
  // round is much longer than the timeout
  if (state->last_probe_type == UNPROBED) {
    return 0;
  }

  // update the overall per-round statistics
  STAT(probe_complete_cnt[state->last_probe_type])++;
  STAT(responsive_cnt[state->last_probe_type]) += resp.verdict;

  new_belief_up = update_bayesian_belief(s24, state, resp.verdict);

#ifdef DEBUG_PROBING
  fprintf(stdout, "%f (%s) -> %f (%s)", state->current_belief,
          belief_icons[BELIEF_STATE(state->current_belief)], new_belief_up,
          belief_icons[BELIEF_STATE(new_belief_up)]);
#endif

  // if belief is not stable, then we send another probe
  if (new_belief_up != state->current_belief) {
    // we'd like to send an adaptive probe, but do we have any left in the
    // budget?
    if (ADAPTIVE_BUDGET(state) > 0) {
      if (queue_slash24_probe(prober, s24, state, ADAPTIVE) != 0) {
        return -1;
      }
#ifdef DEBUG_PROBING
      fprintf(stdout, " ADAPTIVE");
#endif

      // no more adaptive probes left, but if we are changing state away from
      // UP, we'd better be darn sure, so lets switch to recovery probing right
      // now
    } else if (state->current_state == UP && RECOVERY_BUDGET(state) > 0) {
      // queue a recovery probe
      if (queue_slash24_probe(prober, s24, state, RECOVERY) != 0) {
        return -1;
      }
#ifdef DEBUG_PROBING
      fprintf(stdout, " ADAPTIVE-RECOVERY");
#endif

      // we ideally would have liked to send an adaptive probe since belief has
      // changed, but we're out of probes, and we have probably run out of
      // recovery probes too, so we give up and leave the block as uncertain.
    } else {
      new_belief_up = 0.5; // => UNCERTAIN
      state->last_probe_type = UNPROBED;
#ifdef DEBUG_PROBING
      fprintf(stdout, " DONE-ADAPTIVE");
#endif
    }

    // otherwise, if we are down and staying down, send recovery probes to try
    // and get back up. this is a separate case as we don't want to go into the
    // uncertain state when we finish.
  } else if (BELIEF_STATE(state->current_belief) == DOWN &&
             BELIEF_STATE(new_belief_up) == DOWN &&
             RECOVERY_ELIGIBLE(state) != 0) {
    if (RECOVERY_BUDGET(state) > 0) {
      // queue a recovery probe
      if (queue_slash24_probe(prober, s24, state, RECOVERY) != 0) {
        return -1;
      }
#ifdef DEBUG_PROBING
      fprintf(stdout, " RECOVERY");
#endif
    } else {
      // we wanted to send a recovery probe, but we're out of probes
      state->last_probe_type = UNPROBED;
#ifdef DEBUG_PROBING
      fprintf(stdout, " DONE-RECOVERY");
#endif
    }
  } else {
    // No adaptive/recovery probe sent
    state->last_probe_type = UNPROBED;
#ifdef DEBUG_PROBING
    fprintf(stdout, " DONE");
#endif
  }

#ifdef DEBUG_PROBING
  fprintf(stdout, " %d %d %d\n", ADAPTIVE_BUDGET(state), RECOVERY_BUDGET(state),
          state->rounds_since_up);
#endif

  // only update statistics if we have stopped probing this /24 (otherwise we
  // run the risk of dumping info about a /24 while the prober is converging on
  // a decision)
  if (state->last_probe_type == UNPROBED) {
    // update overall belief stats
    // decrement current state
    STAT(slash24_state_cnts[state->current_state])--;
    // increment new state
    STAT(slash24_state_cnts[BELIEF_STATE(new_belief_up)])++;

    // update the timeseries
    for (i = 0; i < state->metrics_cnt; i++) {
      timeseries_kp_set(prober->kp, state->metrics[i].belief,
                        new_belief_up * 100);
      timeseries_kp_set(prober->kp, state->metrics[i].state,
                        BELIEF_STATE(new_belief_up));

      // update the overall stats for this metric
      // decrement the old state
      key = state->metrics[i].overall[state->current_state];
      tmp = timeseries_kp_get(prober->kp, key);
      assert(tmp > 0);
      timeseries_kp_set(prober->kp, key, tmp - 1);
      // increment the new state
      key = state->metrics[i].overall[BELIEF_STATE(new_belief_up)];
      tmp = timeseries_kp_get(prober->kp, key);
      timeseries_kp_set(prober->kp, key, tmp + 1);
    }

    // update the stable state
    state->current_state = BELIEF_STATE(new_belief_up);
  }

  // update the belief
  state->current_belief = new_belief_up;

  return 0;

err:
  return -1;
}

static int start_driver(struct driver_wrap *dw, char *driver_name,
                        char *driver_config)
{
  // start user-specified driver
  if ((dw->driver = trinarkular_driver_create_by_name(driver_name,
                                                      driver_config)) == NULL) {
    return -1;
  }

  // add the driver to our event loop
  if (zloop_reader(dw->prober->loop,
                   trinarkular_driver_get_recv_socket(dw->driver),
                   handle_driver_resp, dw) != 0) {
    trinarkular_log("ERROR: Could not add driver to prober event loop");
    return -1;
  }

  return 0;
}

trinarkular_prober_t *trinarkular_prober_create(const char *name,
                                                timeseries_t *timeseries)
{
  trinarkular_prober_t *prober;

  if ((prober = malloc_zero(sizeof(trinarkular_prober_t))) == NULL) {
    trinarkular_log("ERROR: Could not create prober object");
    return NULL;
  }

  // initialize default params
  set_default_params(&prober->params);

  assert(timeseries != NULL);
  prober->ts = timeseries;

  prober->name = strdup(name);
  assert(prober->name != NULL);
  prober->name_ts = strdup(name);
  assert(prober->name_ts != NULL);

  // create a key package and init metrics
  if ((prober->kp = timeseries_kp_init(prober->ts, 0)) == NULL ||
      init_kp(prober) != 0) {
    trinarkular_log("ERROR: Could not create timeseries key package");
    goto err;
  }

  // create the reactor
  if ((prober->loop = zloop_new()) == NULL) {
    trinarkular_log("ERROR: Could not initialize reactor");
    goto err;
  }

  trinarkular_log("done");

  return prober;

err:
  trinarkular_prober_destroy(prober);
  return NULL;
}

void trinarkular_prober_destroy(trinarkular_prober_t *prober)
{
  int i;

  if (prober == NULL) {
    return;
  }

  free(prober->name);
  prober->name = NULL;
  free(prober->name_ts);
  prober->name_ts = NULL;

  zloop_destroy(&prober->loop);

  if (prober->outstanding_probe_cnt != 0) {
    trinarkular_log("WARN: %d outstanding probes at shutdown",
                    prober->outstanding_probe_cnt);
  }

  // shut down the probe driver(s)
  for (i = 0; i < prober->drivers_cnt; i++) {
    trinarkular_driver_destroy(prober->drivers[i].driver);
  }
  prober->drivers_cnt = 0;

  trinarkular_probelist_destroy(prober->pl);
  prober->pl = NULL;

  // destroy the key package
  timeseries_kp_free(&prober->kp);

  // timeseries doesn't belong to us

  free(prober);
}

int trinarkular_prober_assign_probelist(trinarkular_prober_t *prober,
                                        trinarkular_probelist_t *pl)
{
  trinarkular_slash24_t *s24 = NULL;

  assert(prober != NULL);
  assert(prober->started == 0);

  prober->pl = pl;

  // and create all the state (including timeseries metrics)
  trinarkular_probelist_reset_slash24_iter(prober->pl);
  while ((s24 = trinarkular_probelist_get_next_slash24(prober->pl)) != NULL) {
    if (slash24_state_create(prober, s24) == NULL) {
      trinarkular_log("ERROR: Could not create /24 state");
      return -1;
    }
  }

  trinarkular_log("Probelist size: %d /24s",
                  trinarkular_probelist_get_slash24_cnt(pl));
  return 0;
}

int trinarkular_prober_start(trinarkular_prober_t *prober)
{
  uint32_t periodic_timeout;
  uint64_t now;
  uint64_t aligned_start;

  assert(prober != NULL);
  assert(prober->started == 0);

  if (prober->pl == NULL ||
      trinarkular_probelist_get_slash24_cnt(prober->pl) == 0) {
    trinarkular_log("ERROR: Missing or empty probelist. Refusing to start");
    return -1;
  }

  // create the periodic probe timer
  periodic_timeout =
    PARAM(periodic_round_duration) / PARAM(periodic_round_slices);
  if (periodic_timeout < 100) {
    trinarkular_log("WARN: Periodic timer is set to fire every %dms",
                    periodic_timeout);
  }
  if ((prober->periodic_timer_id = zloop_timer(prober->loop, periodic_timeout,
                                               0, handle_timer, prober)) < 0) {
    trinarkular_log("ERROR: Could not create periodic timer");
    return -1;
  }

  // compute the slice size
  prober->slice_size = trinarkular_probelist_get_slash24_cnt(prober->pl) /
                       PARAM(periodic_round_slices);
  if (prober->slice_size * PARAM(periodic_round_slices) <
      trinarkular_probelist_get_slash24_cnt(prober->pl)) {
    // round up to ensure we cover everything in the interval
    prober->slice_size++;
  }
  trinarkular_log("Periodic Probing Slice Size: %d", prober->slice_size);

  // start the default driver if needed
  if (prober->drivers_cnt == 0 &&
      trinarkular_prober_add_driver(prober, TRINARKULAR_PROBER_DRIVER_DEFAULT,
                                    TRINARKULAR_PROBER_DRIVER_ARGS_DEFAULT) !=
        0) {
    return -1;
  }

  // force libtimeseries to resolve all keys
  trinarkular_log("Resolving %d timeseries keys",
                  timeseries_kp_size(prober->kp));
  while (timeseries_kp_resolve(prober->kp) != 0) {
    trinarkular_log("WARN: Could not resolve timeseries keys. Retrying");
    if (sleep(10) != 0) {
      trinarkular_log("WARN: Sleep interrupted, exiting");
      return -1;
    }
  }

  prober->started = 1;

  // wait so that our round starts at a nice time
  now = zclock_time();
  aligned_start = (((uint64_t)(now / PARAM(periodic_round_duration))) *
                   PARAM(periodic_round_duration)) +
                  PARAM(periodic_round_duration);

  if (PARAM(sleep_align_start) != 0) {
    trinarkular_log("Sleeping for %d seconds to align with round duration",
                    (aligned_start / 1000) - (now / 1000));
    if (sleep((aligned_start / 1000) - (now / 1000)) != 0) {
      trinarkular_log("WARN: Sleep interrupted, exiting");
    }
  }

  trinarkular_log("prober up and running");

  zloop_start(prober->loop);

  return 0;
}

void trinarkular_prober_stop(trinarkular_prober_t *prober)
{
  assert(prober != NULL);

  prober->shutdown = 1;

  trinarkular_log("waiting to shut down");
}

void trinarkular_prober_set_periodic_round_duration(
  trinarkular_prober_t *prober, uint64_t duration)
{
  assert(prober != NULL);

  trinarkular_log("%" PRIu64, duration);
  PARAM(periodic_round_duration) = duration;
}

void trinarkular_prober_set_periodic_round_slices(trinarkular_prober_t *prober,
                                                  int slices)
{
  assert(prober != NULL);

  trinarkular_log("%d", slices);
  PARAM(periodic_round_slices) = slices;
}

void trinarkular_prober_set_periodic_round_limit(trinarkular_prober_t *prober,
                                                 int rounds)
{
  assert(prober != NULL);

  trinarkular_log("%d", rounds);
  PARAM(periodic_round_limit) = rounds;
}

void trinarkular_prober_set_periodic_probe_timeout(trinarkular_prober_t *prober,
                                                   uint32_t timeout)
{
  assert(prober != NULL);
  assert(timeout < UINT16_MAX);

  trinarkular_log("%" PRIu16, timeout);
  PARAM(periodic_probe_timeout) = timeout;
}

void trinarkular_prober_disable_sleep_align_start(trinarkular_prober_t *prober)
{
  assert(prober != NULL);

  PARAM(sleep_align_start) = 0;
}

int trinarkular_prober_add_driver(trinarkular_prober_t *prober,
                                  char *driver_name, char *driver_args)
{
  assert(prober != NULL);

  trinarkular_log("%s %s", driver_name, driver_args);

  prober->drivers[prober->drivers_cnt].id = prober->drivers_cnt;
  prober->drivers[prober->drivers_cnt].prober = prober;

  // initialize the driver
  if (start_driver(&prober->drivers[prober->drivers_cnt], driver_name,
                   driver_args) != 0) {
    return -1;
  }

  prober->drivers_cnt++;

  trinarkular_log("%d drivers", prober->drivers_cnt);

  return 0;
}
