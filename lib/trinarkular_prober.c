/*
 * This file is part of trinarkular
 *
 * Copyright (C) 2015 The Regents of the University of California.
 * Authors: Alistair King
 *
 * This software is Copyright (c) 2015 The Regents of the University of
 * California. All Rights Reserved. Permission to copy, modify, and distribute this
 * software and its documentation for academic research and education purposes,
 * without fee, and without a written agreement is hereby granted, provided that
 * the above copyright notice, this paragraph and the following three paragraphs
 * appear in all copies. Permission to make use of this software for other than
 * academic research and education purposes may be obtained by contacting:
 *
 * Office of Innovation and Commercialization
 * 9500 Gilman Drive, Mail Code 0910
 * University of California
 * La Jolla, CA 92093-0910
 * (858) 534-5815
 * invent@ucsd.edu
 *
 * This software program and documentation are copyrighted by The Regents of the
 * University of California. The software program and documentation are supplied
 * "as is", without any accompanying services from The Regents. The Regents does
 * not warrant that the operation of the program will be uninterrupted or
 * error-free. The end-user understands that the program was developed for research
 * purposes and is advised not to rely exclusively on the program for any reason.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST
 * PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
 * THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE. THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS
 * IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE
 * MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
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
#include "trinarkular_signal.h"
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
#include <pthread.h>

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
#define METRIC_PREFIX "active.ping-slash24"

// metric prefix for per-/24 info
#define METRIC_PREFIX_SLASH24 METRIC_PREFIX ""

// metric prefix for prober metadata
#define METRIC_PREFIX_PROBER METRIC_PREFIX ".probers"

#define CH_SLASH24 "__PFX_%s_24"

// used to coordinate between main thread and probelist reload thread
#define PROBELIST_RELOAD_NONE      0
#define PROBELIST_RELOAD_SCHEDULED 1
#define PROBELIST_RELOAD_RUNNING   2
#define PROBELIST_RELOAD_DONE      3

// the number of probelist states we have (ACTIVE and NEXT)
#define PROBELIST_STATES_CNT 2

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
#define ACTIVE_STAT(sname)                                                     \
  (prober->pl_states[prober->pl_state_active_idx].stats.sname)
#define NEXT_STAT(sname)                                                       \
  (prober->pl_states[!prober->pl_state_active_idx].stats.sname)

/** How often do we expect packet loss?
    (Taken from the paper) */
#define PACKET_LOSS_FREQUENCY 0.01

#define BELIEF_UP_FRAC 0.9
#define BELIEF_DOWN_FRAC 0.1
#define BELIEF_STATE(s)                                                        \
  (((s) < BELIEF_DOWN_FRAC) ? DOWN : ((s) > BELIEF_UP_FRAC) ? UP : UNCERTAIN)

// convenience macros to ease data structure access
#define ACTIVE_PL_STATE(p)   (p->pl_states[p->pl_state_active_idx])
#define ACTIVE_PL(p)         (p->pl_states[p->pl_state_active_idx].pl)
#define ACTIVE_KP_AGGR(p)    (p->pl_states[p->pl_state_active_idx].kp_aggr)
#define ACTIVE_KP_SLASH24(p) (p->pl_states[p->pl_state_active_idx].kp_slash24)
#define ACTIVE_METRICS(p)    (p->pl_states[p->pl_state_active_idx].metrics)
#define NEXT_PL_STATE(p)     (p->pl_states[!p->pl_state_active_idx])
#define NEXT_PL(p)           (p->pl_states[!p->pl_state_active_idx].pl)
#define NEXT_KP_AGGR(p)      (p->pl_states[!p->pl_state_active_idx].kp_aggr)
#define NEXT_KP_SLASH24(p)   (p->pl_states[!p->pl_state_active_idx].kp_slash24)

/* Structure representing a probelist state.  We maintain two of these to
 * facilitate probelist reloads.
 */
typedef struct probelist_state {

  /** Probelist */
  trinarkular_probelist_t *pl;

  /** Key package (Per-/24 data) */
  timeseries_kp_t *kp_slash24;

  /** Key package (non-/24 data) */
  timeseries_kp_t *kp_aggr;

  /** Indexes into the KP for overall metrics */
  struct metrics metrics;

  /** Probing statistics */
  probing_stats_t stats;
} probelist_state_t;

/* Structure representing a prober instance */
struct trinarkular_prober {

  /** Prober name */
  char *name;

  /** Prober name (timeseries safe) */
  char *name_ts;

  /** Configuration parameters */
  struct params params;

  /** Libtimeseries instance (Per-/24 data) */
  timeseries_t *ts_slash24;

  /** Libtimeseries instance (non-/24 data) */
  timeseries_t *ts_aggr;

  /** Has this prober been started? */
  int started;

  /** Should the prober shut down? */
  int shutdown;

  /** Two probelist states hold probelists and corresponding key packages */
  probelist_state_t pl_states[PROBELIST_STATES_CNT];

  /** Holds index of active probelist state. Either 0 or 1 */
  int pl_state_active_idx;

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

  /** Number of probes queued with the driver(s) */
  uint64_t outstanding_probe_cnt;

  /* ==== Periodic Probing State ==== */

  /** The number of /24s that are in a slice */
  int slice_size;

  /** The current slice (i.e. how many times the slice timer has fired) */
  uint64_t current_slice;

  /** Has probing started yet? */
  int probing_started;

  /** Do we have to reload the probelist? */
  int reload_probelist;

  /** Used by threads to coordinate probelist reload */
  sig_atomic_t reload_probelist_state;

  /** Filename of probelist */
  char *probelist_filename;

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

  // create key packages
  if ((NEXT_KP_SLASH24(prober) =
       timeseries_kp_init(prober->ts_slash24, 0)) == NULL ||
      (NEXT_KP_AGGR(prober) = timeseries_kp_init(prober->ts_aggr, 0)) == NULL) {
    return -1;
  }

  // round id
  snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.meta.round_id",
           prober->name_ts);
  if ((NEXT_PL_STATE(prober).metrics.round_id =
         timeseries_kp_add_key(NEXT_KP_AGGR(prober), buf)) == -1) {
    return -1;
  }

  // round duration
  snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.meta.round_duration",
           prober->name_ts);
  if ((NEXT_PL_STATE(prober).metrics.round_duration =
         timeseries_kp_add_key(NEXT_KP_AGGR(prober), buf)) == -1) {
    return -1;
  }

  for (i = PERIODIC; i <= RECOVERY; i++) {
    // probe cnt
    snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.probing.%s.probe_cnt",
             prober->name_ts, probe_types[i]);
    if ((NEXT_PL_STATE(prober).metrics.round_probe_cnt[i] =
           timeseries_kp_add_key(NEXT_KP_AGGR(prober), buf)) == -1) {
      return -1;
    }

    // probe complete cnt
    snprintf(buf, BUFFER_LEN,
             METRIC_PREFIX_PROBER ".%s.probing.%s.completed_probe_cnt",
             prober->name_ts, probe_types[i]);
    if ((NEXT_PL_STATE(prober).metrics.round_probe_complete_cnt[i] =
           timeseries_kp_add_key(NEXT_KP_AGGR(prober), buf)) == -1) {
      return -1;
    }

    // responsive cnt
    snprintf(buf, BUFFER_LEN,
             METRIC_PREFIX_PROBER ".%s.probing.%s.responsive_probe_cnt",
             prober->name_ts, probe_types[i]);
    if ((NEXT_PL_STATE(prober).metrics.round_responsive_cnt[i] =
           timeseries_kp_add_key(NEXT_KP_AGGR(prober), buf)) == -1) {
      return -1;
    }
  }

  for (i = UNCERTAIN; i < BELIEF_STATE_CNT; i++) {
    snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.states.%s_slash24_cnt",
             prober->name_ts, belief_states[i]);
    if ((NEXT_PL_STATE(prober).metrics.slash24_state_cnts[i] =
           timeseries_kp_add_key(NEXT_KP_AGGR(prober), buf)) == -1) {
      return -1;
    }
  }

  snprintf(buf, BUFFER_LEN, METRIC_PREFIX_PROBER ".%s.slash24_cnt",
           prober->name_ts);
  if ((NEXT_PL_STATE(prober).metrics.slash24_cnt =
         timeseries_kp_add_key(NEXT_KP_AGGR(prober), buf)) == -1) {
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
                                  const char *slash24_string, const char *md,
                                  int per_block_stats)
{
  char buf[BUFFER_LEN];
  int i;
  uint64_t tmp;

  // only include per-block stats if this MD is a leaf (e.g., don't track blocks
  // at continent level)
  if (per_block_stats != 0) {
    // belief
    snprintf(buf, BUFFER_LEN, METRIC_PREFIX_SLASH24
             ".%s.probers.%s.blocks." CH_SLASH24 ".belief",
             md, prober->name_ts, slash24_string);
    if ((metrics->belief =
         timeseries_kp_add_key(NEXT_KP_SLASH24(prober), buf)) == -1) {
      return -1;
    }

    // state
    snprintf(buf, BUFFER_LEN,
             METRIC_PREFIX_SLASH24 ".%s.probers.%s.blocks." CH_SLASH24 ".state",
             md, prober->name_ts, slash24_string);
    if ((metrics->state =
         timeseries_kp_add_key(NEXT_KP_SLASH24(prober), buf)) == -1) {
      return -1;
    }
  } else {
    metrics->belief = -1;
    metrics->state = -1;
  }

  // overall per-state stats
  for (i = UNCERTAIN; i < BELIEF_STATE_CNT; i++) {
    snprintf(buf, BUFFER_LEN,
             METRIC_PREFIX_SLASH24 ".%s.probers.%s.%s_slash24_cnt", md,
             prober->name_ts, belief_states[i]);
    if ((metrics->overall[i] =
           timeseries_kp_get_key(NEXT_KP_AGGR(prober), buf)) == -1 &&
        (metrics->overall[i] =
           timeseries_kp_add_key(NEXT_KP_AGGR(prober), buf)) == -1) {
      return -1;
    }
  }

  // add this /24 to the UP state
  tmp = timeseries_kp_get(NEXT_KP_AGGR(prober), metrics->overall[UP]);
  timeseries_kp_set(NEXT_KP_AGGR(prober), metrics->overall[UP], tmp + 1);

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
                               s24->md[i]+2, // skip the '[LN]:' prefix
                               (s24->md[i][0] == 'L') ? 1 : 0) != 0) {
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

  NEXT_STAT(slash24_state_cnts[UP])++;
  NEXT_STAT(slash24_cnt)++;

  // now, attach it
  if (trinarkular_probelist_save_slash24_state(NEXT_PL(prober),
                                               s24, state) != 0) {
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

  ACTIVE_STAT(start_time) = start_time;

  for (i = UNPROBED; i < PROBE_TYPE_CNT; i++) {
    ACTIVE_STAT(probe_cnt[i]) = 0;
    ACTIVE_STAT(probe_complete_cnt[i]) = 0;
    ACTIVE_STAT(responsive_cnt[i]) = 0;
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
  ACTIVE_STAT(probe_cnt[probe_type])++;

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
  if ((ret = trinarkular_driver_queue_req(dw->driver, &req)) != 0) {
    return -1;
  }
  prober->outstanding_probe_cnt++;

  // move on to the next driver ready for the next probe
  prober->drivers_next = (prober->drivers_next + 1) % prober->drivers_cnt;

  // save the state
  if (trinarkular_probelist_save_slash24_state(ACTIVE_PL(prober),
                                               s24, state) != 0) {
    return -1;
  }

  return 0;
}

static int end_of_round(trinarkular_prober_t *prober, int round_id)
{
  uint64_t now = zclock_time();
  uint64_t aligned_start =
    ((uint64_t)(ACTIVE_STAT(start_time) / PARAM(periodic_round_duration))) *
    PARAM(periodic_round_duration);
  int i;

  timeseries_kp_set(ACTIVE_KP_AGGR(prober),
                    ACTIVE_METRICS(prober).round_id, round_id);
  timeseries_kp_set(ACTIVE_KP_AGGR(prober),
                    ACTIVE_METRICS(prober).round_duration,
                    now - ACTIVE_STAT(start_time));

  for (i = PERIODIC; i < PROBE_TYPE_CNT; i++) {
    timeseries_kp_set(ACTIVE_KP_AGGR(prober),
                      ACTIVE_METRICS(prober).round_probe_cnt[i],
                      ACTIVE_STAT(probe_cnt[i]));
    timeseries_kp_set(ACTIVE_KP_AGGR(prober),
                      ACTIVE_METRICS(prober).round_probe_complete_cnt[i],
                      ACTIVE_STAT(probe_complete_cnt[i]));
    timeseries_kp_set(ACTIVE_KP_AGGR(prober),
                      ACTIVE_METRICS(prober).round_responsive_cnt[i],
                      ACTIVE_STAT(responsive_cnt[i]));
  }

  for (i = UNCERTAIN; i < BELIEF_STATE_CNT; i++) {
    timeseries_kp_set(ACTIVE_KP_AGGR(prober),
                      ACTIVE_METRICS(prober).slash24_state_cnts[i],
                      ACTIVE_STAT(slash24_state_cnts[i]));
  }

  timeseries_kp_set(ACTIVE_KP_AGGR(prober), ACTIVE_METRICS(prober).slash24_cnt,
                    ACTIVE_STAT(slash24_cnt));

  trinarkular_log("round %d completed in %" PRIu64 "ms (ideal: %" PRIu64 "ms)",
                  round_id, now - ACTIVE_STAT(start_time),
                  PARAM(periodic_round_duration));
  trinarkular_log("round periodic response rate: %d/%d (%0.0f%%)",
                  ACTIVE_STAT(responsive_cnt[PERIODIC]),
                  ACTIVE_STAT(probe_cnt[PERIODIC]),
                  ACTIVE_STAT(responsive_cnt[PERIODIC]) * 100.0 /
                    ACTIVE_STAT(probe_cnt[PERIODIC]));

  // flush the kps
  if (timeseries_kp_flush(ACTIVE_KP_AGGR(prober), aligned_start / 1000) != 0 ||
      timeseries_kp_flush(ACTIVE_KP_SLASH24(prober),
                          aligned_start / 1000) != 0) {
    return -1;
  }
  return 0;
}

int probelist_state_destroy(probelist_state_t *pl_state)
{
  trinarkular_probelist_destroy(pl_state->pl);
  pl_state->pl = NULL;

  // destroy the key packages
  timeseries_kp_free(&pl_state->kp_slash24);
  pl_state->kp_slash24 = NULL;
  timeseries_kp_free(&pl_state->kp_aggr);
  pl_state->kp_aggr = NULL;

  return 0;
}

static int trinarkular_prober_prepare_probelist(trinarkular_prober_t *prober)
{
  int i = 0;
  trinarkular_slash24_t *s24 = NULL;

  trinarkular_log("Preparing probelist to be assigned");

  assert(NEXT_PL(prober) == NULL);

  // create new probelist
  if ((NEXT_PL(prober) =
       trinarkular_probelist_create(prober->probelist_filename)) == NULL) {
    trinarkular_log("ERROR: Could not create probelist file");
    goto err;
  }

  // re-initialize key packages
  if (init_kp(prober) != 0) {
    trinarkular_log("ERROR: Could not create timeseries key package");
    goto err;
  }

  // re-initialize statistics
  for (i = UNCERTAIN; i < BELIEF_STATE_CNT; i++) {
      NEXT_PL_STATE(prober).stats.slash24_state_cnts[i] = 0;
  }
  NEXT_PL_STATE(prober).stats.slash24_cnt = 0;

  // and create all the state (including timeseries metrics)
  trinarkular_probelist_reset_slash24_iter(NEXT_PL(prober));

  // iterates over the entire probelist.
  while ((s24 =
          trinarkular_probelist_get_next_slash24(NEXT_PL(prober))) != NULL) {
    if (slash24_state_create(prober, s24) == NULL) {
      trinarkular_log("ERROR: Could not create /24 state");
      goto err;
    }
  }

  // force libtimeseries to resolve all keys
  trinarkular_log("Resolving %d timeseries keys (Per-/24 KP)",
                  timeseries_kp_size(NEXT_KP_SLASH24(prober)));
  while (timeseries_kp_resolve(NEXT_KP_SLASH24(prober)) != 0) {
    trinarkular_log("WARN: Could not resolve timeseries keys. Retrying");
    if (sleep(10) != 0) {
      trinarkular_log("WARN: Sleep interrupted, exiting");
      return -1;
    }
  }
  trinarkular_log("Resolving %d timeseries keys (Aggregate KP)",
                  timeseries_kp_size(NEXT_KP_AGGR(prober)));
  while (timeseries_kp_resolve(NEXT_KP_AGGR(prober)) != 0) {
    trinarkular_log("WARN: Could not resolve timeseries keys. Retrying");
    if (sleep(10) != 0) {
      trinarkular_log("WARN: Sleep interrupted, exiting");
      return -1;
    }
  }

  return 0;

err:
  probelist_state_destroy(&NEXT_PL_STATE(prober));
  prober->reload_probelist_state = PROBELIST_RELOAD_NONE;
  return -1;
}

static void trinarkular_prober_update_probelist(trinarkular_prober_t *prober)
{
  trinarkular_log("Updating probelist");

  // destroy active state if there is any (there may not be if we are loading
  // the probelist for the first time)
  probelist_state_destroy(&ACTIVE_PL_STATE(prober));

  // make index point to the other probelist state now
  prober->pl_state_active_idx = !prober->pl_state_active_idx;

  // compute the slice size
  prober->slice_size = trinarkular_probelist_get_slash24_cnt(ACTIVE_PL(prober))/
                       PARAM(periodic_round_slices);
  if (prober->slice_size * PARAM(periodic_round_slices) <
      trinarkular_probelist_get_slash24_cnt(ACTIVE_PL(prober))) {
    // round up to ensure we cover everything in the interval
    prober->slice_size++;
  }
  trinarkular_log("Periodic Probing Slice Size: %d", prober->slice_size);

  trinarkular_log("Probelist size: %d /24s, version: %s",
                  trinarkular_probelist_get_slash24_cnt(ACTIVE_PL(prober)),
                  trinarkular_probelist_get_version(ACTIVE_PL(prober)));
}

static void *reload_probelist(void *arg)
{
  trinarkular_log("I'm the thread that reloads the probelist");

  trinarkular_prober_t *prober = (trinarkular_prober_t *) arg;

  if (trinarkular_prober_prepare_probelist(prober) != 0) {
    trinarkular_log("ERROR: trinarkular_prober_prepare_probelist() failed");
    return NULL;
  }

  prober->reload_probelist_state = PROBELIST_RELOAD_DONE;
  trinarkular_log("Probelist successfully reloaded in separate thread");

  return NULL;
}

static void schedule_probelist_reload(trinarkular_prober_t *prober)
{
  pthread_t handle = 0;
  int ret = 0;

  trinarkular_log("A probelist reload is scheduled");

  // run probelist reload code in separate thread so we don't stall probing
  trinarkular_log("Creating thread to reload probelist");
  if ((ret = pthread_create(&handle, NULL, reload_probelist,
                            (void *) prober)) == 0) {
    // detach thread, so its resources are automatically freed, without having
    // to call pthread_join()
    if ((ret = pthread_detach(handle)) != 0) {
      trinarkular_log("ERROR: pthread_detach() returned %d", ret);
    }
  } else {
    trinarkular_log("ERROR: pthread_create() returned %d", ret);
    prober->reload_probelist_state = PROBELIST_RELOAD_NONE;
    return;
  }
  prober->reload_probelist_state = PROBELIST_RELOAD_RUNNING;
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

  // do we have to reload the probelist?
  if (prober->reload_probelist_state == PROBELIST_RELOAD_SCHEDULED) {
    schedule_probelist_reload(prober);
  }

  // have we reached the end of the probelist and need to start over?
  if (prober->probing_started == 0 ||
      trinarkular_probelist_has_more_slash24(ACTIVE_PL(prober)) == 0) {
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

      // check if we're done reloading the probelist
      if (prober->reload_probelist_state == PROBELIST_RELOAD_RUNNING) {
        trinarkular_log("Probelist reload still in progress.  "
                        "Waiting until next round.");
      } else if (prober->reload_probelist_state == PROBELIST_RELOAD_DONE) {
        trinarkular_log("Probelist reload done.  Now updating probelist.");
        trinarkular_prober_update_probelist(prober);
        prober->reload_probelist_state = PROBELIST_RELOAD_NONE;
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
    trinarkular_probelist_reset_slash24_iter(ACTIVE_PL(prober));
    // reset round stats
    reset_round_stats(prober, now);

    prober->probing_started = 1;
  }

  // check if there are still a few slices of probes outstanding.  this is a
  // good indication that scamper is not keeping up with the probing rate
  // (probably due to our timers firing close together).  in this case we skip
  // the entire slice.
  if (prober->outstanding_probe_cnt > prober->slice_size * 5) {
    trinarkular_log(
      "WARN: %d outstanding requests (slice size is %d), skipping slice.",
      prober->outstanding_probe_cnt, prober->slice_size);
    goto done;
  }

  trinarkular_log("INFO: %" PRIu64 " outstanding requests (slice size is %d)",
                  prober->outstanding_probe_cnt, prober->slice_size);

  for (slice_cnt = 0; slice_cnt < prober->slice_size; slice_cnt++) {
    // get a slash24 to probe
    if ((s24 =
         trinarkular_probelist_get_next_slash24(ACTIVE_PL(prober))) == NULL) {
      // end of /24s
      break;
    }
    // get the state for this /24
    if ((state = trinarkular_probelist_get_slash24_state(ACTIVE_PL(prober),
                                                         s24)) == NULL) {
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
  // B(U)
  float BU = state->current_belief;

  // B(~U)
  float BD = 1.0 - BU;

  // P(p|~U)
  float PpD = (1.0 - PACKET_LOSS_FREQUENCY) / TRINARKULAR_SLASH24_HOST_CNT;

  // P(p|U)
  float PpU = s24->aeb;

  // P(n|U)
  float PnU = 1.0 - PpU;

  // P(n|~U)
  float PnD = 1.0 - PpD;

  float new_belief_down;

  // Positive response
  if (probe_response != 0) {
    new_belief_down = (PpD * BD) / ((PpD*BD) + (PpU*BU));
  } else { // Negative, or no response
    new_belief_down = (PnD*BD) / ((PnD*BD) + (PnU*BU));
  }

  // capping as per sec 4.2 of paper
  if (new_belief_down > 0.99) {
    new_belief_down = 0.99;
  } else if (new_belief_down < 0.01) {
    new_belief_down = 0.01;
  }

  // convert B(~U) to B(U) and return
  return 1 - new_belief_down;
}

#define BECOMING_UNCERTAIN(old, new)                                           \
  ((BELIEF_STATE(new) == UNCERTAIN) ||                                         \
   (BELIEF_STATE(old) == UP && (old > new)) ||                                 \
   (BELIEF_STATE(old) == DOWN && (new > old)))

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
         ACTIVE_PL(prober), (ntohl(resp.target_ip) &
                             TRINARKULAR_SLASH24_NETMASK))) == NULL) {
    trinarkular_log("WARN: Missing /24 for %x", ntohl(resp.target_ip));
    return 0;
  }
  prober->outstanding_probe_cnt--;

  // grab the state for this /24
  if ((state = trinarkular_probelist_get_slash24_state(ACTIVE_PL(prober),
                                                       s24)) == NULL) {
    trinarkular_log("ERROR: Missing state for %x", ntohl(resp.target_ip));
    goto err;
  }

  // since periodic probing will reset adaptive probing state, this can cause
  // responses to become out of sync. simply ignore a response that we don't
  // care about. (NB: in practice this should rarely, if ever, happen since a
  // round is much longer than the timeout)
  if (state->last_probe_type == UNPROBED) {
    return 0;
  }

  // update the overall per-round statistics
  ACTIVE_STAT(probe_complete_cnt[state->last_probe_type])++;
  ACTIVE_STAT(responsive_cnt[state->last_probe_type]) += resp.verdict;

  new_belief_up = update_bayesian_belief(s24, state, resp.verdict);

#ifdef DEBUG_PROBING
  fprintf(stdout, "%f (%s) -> %f (%s)", state->current_belief,
          belief_icons[BELIEF_STATE(state->current_belief)], new_belief_up,
          belief_icons[BELIEF_STATE(new_belief_up)]);
#endif

  // if new state is uncertain, or we are moving toward uncertainty, send more
  // probes
  if (BECOMING_UNCERTAIN(state->current_belief, new_belief_up)) {
    // we'd like to send an adaptive probe, but do we have any left in the
    // budget?
    if (ADAPTIVE_BUDGET(state) > 0) {
      if (queue_slash24_probe(prober, s24, state, ADAPTIVE) != 0) {
        return -1;
      }
#ifdef DEBUG_PROBING
      fprintf(stdout, " ADAPTIVE");
#endif

      // AK removes this urgent recovery to save some probes. consider enabling
      // if probe usage is not a problem as it might help some unstable blocks
#if 0
      // no more adaptive probes left, but if we are changing state away from
      // UP, we'd better be darn sure, so lets switch to recovery probing right
      // now (unless this is the first round...)
    } else if (state->current_state == UP && RECOVERY_BUDGET(state) > 0 &&
               prober->current_slice != 0) {
      // queue a recovery probe
      if (queue_slash24_probe(prober, s24, state, RECOVERY) != 0) {
        return -1;
      }
#ifdef DEBUG_PROBING
      fprintf(stdout, " ADAPTIVE-RECOVERY");
#endif
#endif

      // we ideally would have liked to send an adaptive probe since belief has
      // changed, but we're out of probes, so we give up and if the block is not
      // already uncertain, we move belief to 0.5 (uncertain)
    } else {
      if (BELIEF_STATE(new_belief_up) != UNCERTAIN) {
        new_belief_up = 0.5; // => UNCERTAIN
      }
      state->last_probe_type = UNPROBED;
#ifdef DEBUG_PROBING
      fprintf(stdout, " DONE-ADAPTIVE");
#endif
    }

    // otherwise, if we are down and staying down, send recovery probes to try
    // and get back up.
  } else if (BELIEF_STATE(state->current_belief) == DOWN &&
             BELIEF_STATE(new_belief_up) == DOWN &&
             RECOVERY_ELIGIBLE(state) != 0 &&
             RECOVERY_BUDGET(state) > 0) {
    // queue a recovery probe
    if (queue_slash24_probe(prober, s24, state, RECOVERY) != 0) {
      return -1;
    }
#ifdef DEBUG_PROBING
    fprintf(stdout, " RECOVERY");
#endif

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
    ACTIVE_STAT(slash24_state_cnts[state->current_state])--;
    // increment new state
    ACTIVE_STAT(slash24_state_cnts[BELIEF_STATE(new_belief_up)])++;

    // update the timeseries
    for (i = 0; i < state->metrics_cnt; i++) {
      if (state->metrics[i].belief != -1) {
        timeseries_kp_set(ACTIVE_KP_SLASH24(prober), state->metrics[i].belief,
                          new_belief_up * 100);
      }
      if (state->metrics[i].state != -1) {
        timeseries_kp_set(ACTIVE_KP_SLASH24(prober), state->metrics[i].state,
                          BELIEF_STATE(new_belief_up));
      }

      // update the overall stats for this metric
      // decrement the old state
      key = state->metrics[i].overall[state->current_state];
      tmp = timeseries_kp_get(ACTIVE_KP_AGGR(prober), key);
      assert(tmp > 0);
      timeseries_kp_set(ACTIVE_KP_AGGR(prober), key, tmp - 1);
      // increment the new state
      key = state->metrics[i].overall[BELIEF_STATE(new_belief_up)];
      tmp = timeseries_kp_get(ACTIVE_KP_AGGR(prober), key);
      timeseries_kp_set(ACTIVE_KP_AGGR(prober), key, tmp + 1);
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
                                                const char *probelist,
                                                timeseries_t *ts_slash24,
                                                timeseries_t *ts_aggr)
{
  trinarkular_prober_t *prober;

  if ((prober = malloc_zero(sizeof(trinarkular_prober_t))) == NULL) {
    trinarkular_log("ERROR: Could not create prober object");
    return NULL;
  }

  // initialize default params
  set_default_params(&prober->params);

  prober->ts_slash24 = ts_slash24;
  prober->ts_aggr = ts_aggr;

  prober->name = strdup(name);
  assert(prober->name != NULL);
  prober->name_ts = strdup(name);
  assert(prober->name_ts != NULL);
  prober->probelist_filename = strdup(probelist);
  assert(prober->probelist_filename != NULL);

  // prepare and assign probelist
  if (trinarkular_prober_prepare_probelist(prober) != 0) {
    goto err;
  }
  trinarkular_prober_update_probelist(prober);

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
  free(prober->probelist_filename);
  prober->probelist_filename = NULL;

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

  probelist_state_destroy(&ACTIVE_PL_STATE(prober));
  probelist_state_destroy(&NEXT_PL_STATE(prober));

  // timeseries instances don't belong to us

  free(prober);
}

int trinarkular_prober_start(trinarkular_prober_t *prober)
{
  uint32_t periodic_timeout;
  uint64_t now;
  uint64_t aligned_start;

  assert(prober != NULL);
  assert(prober->started == 0);

  if (ACTIVE_PL(prober) == NULL ||
      trinarkular_probelist_get_slash24_cnt(ACTIVE_PL(prober)) == 0) {
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

  // start the default driver if needed
  if (prober->drivers_cnt == 0 &&
      trinarkular_prober_add_driver(prober, TRINARKULAR_PROBER_DRIVER_DEFAULT,
                                    TRINARKULAR_PROBER_DRIVER_ARGS_DEFAULT) !=
        0) {
    return -1;
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

  while (zloop_start(prober->loop) == 0) {
    // Only reenter zloop_start() if we got a SIGHUP.
    if (errno == EINTR && sighup_received) {
      sighup_received = 0;
    } else {
      break;
    }
  }

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
  assert(timeout < UINT8_MAX);

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

void trinarkular_prober_reload_probelist(trinarkular_prober_t *prober)
{
  assert(prober != NULL);

  if (prober->reload_probelist_state != PROBELIST_RELOAD_NONE) {
    trinarkular_log("Probelist reload still in progress.  Ignoring signal.");
  } else {
    prober->reload_probelist_state = PROBELIST_RELOAD_SCHEDULED;
    trinarkular_log("Probelist reload scheduled");
  }
}
