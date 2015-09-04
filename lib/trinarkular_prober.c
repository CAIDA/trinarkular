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

#include "khash.h"
#include "utils.h"

#include "trinarkular.h"
#include "trinarkular_log.h"
#include "trinarkular_probelist.h"
#include "trinarkular_driver.h"
#include "trinarkular_prober.h"

/** To be run within a zloop handler */
#define CHECK_SHUTDOWN                                          \
  do {                                                          \
    if (zctx_interrupted != 0 || prober->shutdown != 0) {       \
      trinarkular_log("Interrupted, shutting down");            \
      return -1;                                                \
    }                                                           \
  } while(0)

#define METRIC_PREFIX "active.trinarkular.probers"

// set of ids that correspond to metrics in the kp
struct metrics {

  /** Value is the current round ID */
  int round_id;

  /** Value is NOW - round_start */
  int round_duration;

  /** Value is the number of /24s probed in the round */
  int round_probe_cnt;

  /** Value is the number of /24s that we got responses for in the round */
  int round_probe_complete_cnt;

  /** Value is the number of /24s that appeared alive this round */
  int round_responsive_cnt;

};

/** Max number of rounds that are currently being tracked. Most of the time this
    will just be 2 */
#define MAX_IN_PROGRESS_ROUNDS 100

typedef struct round_info {

  /** Is this round info in use? */
  uint64_t in_use;

  /** The round ID */
  uint32_t id;

  /** The walltime that the round started at */
  uint64_t start_time;

  /** Have all periodic probes been sent for this round? */
  uint8_t periodic_all_probes_sent;

  /** The number of probed /24s this round */
  uint32_t periodic_probe_cnt;

  /** The number of responses received this round */
  uint32_t periodic_probe_complete_cnt;

  /** The number of responsive /24s this round */
  uint32_t periodic_responsive_cnt;

} round_info_t;

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

  /** Defaults to 1 */
  uint8_t periodic_max_probecnt;

  /** Defaults to 3000 */
  uint32_t periodic_probe_timeout;

  /** Defaults to walltime at initialization */
  int random_seed;

};

#define PARAM(pname)  (prober->params.pname)

/** Possible bayesian inference states for a /24 */
enum {UNCERTAIN = 0, DOWN = 1, UP = 2};

/** How often do we expect packet loss?
    (Taken from the paper) */
#define PACKET_LOSS_FREQUENCY 0.01

#define BELIEF_UP_FRAC   0.9
#define BELIEF_DOWN_FRAC 0.1
#define BELIEF_STATE(s)                         \
  (((s) < BELIEF_DOWN_FRAC) ? DOWN : ((s) > BELIEF_UP_FRAC) ? UP : UNCERTAIN)

/** Structure to be attached to a /24 in the probelist */
typedef struct prober_slash24_state {

  /** What round is this /24 currently being probed in? */
  uint16_t round_id;

  /** The number of additional probes that can be sent this round */
  uint8_t probe_budget;

  /** The current belief value for this /24 */
  float current_belief;

  /** Pointer to the /24 in the probelist */
  trinarkular_probelist_slash24_t *s24;

} __attribute__((packed)) prober_slash24_state_t;

KHASH_INIT(seq_state, uint64_t, prober_slash24_state_t*, 1,
           kh_int64_hash_func, kh_int64_hash_equal);

/* Structure representing a prober instance */
struct trinarkular_prober {

  /** Prober name */
  char *name;

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

  /** Outstanding request state */
  khash_t(seq_state) *probe_state;

  /** Information about in-progress rounds */
  round_info_t round_info[MAX_IN_PROGRESS_ROUNDS];

  /** Number of in-use round_infos */
  int round_info_cnt;


  /* ==== Periodic Probing State ==== */

  /** The number of /24s that are in a slice */
  int slice_size;

  /** The current slice (i.e. how many times the slice timer has fired) */
  uint64_t current_slice;

  /** Has probing started yet? */
  int probing_started;

};

static char *
graphite_safe(char *p)
{
  if(p == NULL)
    {
      return p;
    }

  char *r = p;
  while(*p != '\0')
    {
      if(*p == '.')
	{
	  *p = '-';
	}
      if(*p == '*')
	{
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

  // round id
  snprintf(buf, BUFFER_LEN, METRIC_PREFIX".%s.meta.round_id", prober->name);
  if ((prober->metrics.round_id =
       timeseries_kp_add_key(prober->kp, buf)) == -1) {
    return -1;
  }

  // round duration
  snprintf(buf, BUFFER_LEN, METRIC_PREFIX".%s.meta.round_duration",
           prober->name);
  if ((prober->metrics.round_duration =
       timeseries_kp_add_key(prober->kp, buf)) == -1) {
    return -1;
  }

  // probe cnt
  snprintf(buf, BUFFER_LEN, METRIC_PREFIX".%s.periodic.probed_slash24_cnt",
           prober->name);
  if ((prober->metrics.round_probe_cnt =
       timeseries_kp_add_key(prober->kp, buf))
      == -1) {
    return -1;
  }

  // probe complete cnt
  snprintf(buf, BUFFER_LEN, METRIC_PREFIX".%s.periodic.completed_slash24_cnt",
           prober->name);
  if ((prober->metrics.round_probe_complete_cnt =
       timeseries_kp_add_key(prober->kp, buf))
      == -1) {
    return -1;
  }

  // responsive cnt
  snprintf(buf, BUFFER_LEN, METRIC_PREFIX".%s.periodic.responsive_slash24_cnt",
           prober->name);
  if ((prober->metrics.round_responsive_cnt =
       timeseries_kp_add_key(prober->kp, buf))
      == -1) {
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

  // max probecount
  params->periodic_max_probecnt =
    TRINARKULAR_PROBER_PERIODIC_MAX_PROBECOUNT_DEFAULT;

  // probe timeout
  params->periodic_probe_timeout =
    TRINARKULAR_PROBER_PERIODIC_PROBE_TIMEOUT_DEFAULT;

  // random seed
  params->random_seed = zclock_time();
}

static void prober_slash24_state_destroy(void *user)
{
  prober_slash24_state_t *state = (prober_slash24_state_t*)user;

  free(state);
}

static prober_slash24_state_t *
prober_slash24_state_create(trinarkular_prober_t *prober,
                            trinarkular_probelist_slash24_t *s24)
{
  prober_slash24_state_t *slash24_state = NULL;

  // need to first create the state
  if ((slash24_state = malloc(sizeof(prober_slash24_state_t))) == NULL) {
    trinarkular_log("ERROR: Could not create slash24 state");
    return NULL;
  }

  // initialize things
  slash24_state->round_id = 0;
  slash24_state->probe_budget = TRINARKULAR_PROBER_ROUND_PROBE_BUDGET;
  slash24_state->current_belief = 0.99; // as per paper
  slash24_state->s24 = s24;

  // now, attach it
  if (trinarkular_probelist_set_slash24_user(prober->pl,
                                             s24,
                                             prober_slash24_state_destroy,
                                             slash24_state) != 0) {
    goto err;
  }

  // and ask for the hosts to be randomized
  if (trinarkular_probelist_slash24_randomize_hosts(s24, PARAM(random_seed))
      != 0) {
    goto err;
  }

  return slash24_state;

 err:
  prober_slash24_state_destroy(slash24_state);
  return NULL;
}

static int add_round(trinarkular_prober_t *prober,
                     uint32_t round_id, uint64_t start_time) {
  int i;

  round_info_t *ri;

  // look for an empty round
  for (i=0; i<MAX_IN_PROGRESS_ROUNDS; i++) {
    if (prober->round_info[i].in_use == 0) {
      ri = &prober->round_info[i];
      ri->in_use = 1;
      ri->id = round_id;
      ri->start_time = start_time;
      ri->periodic_all_probes_sent = 0;
      ri->periodic_probe_cnt = 0;
      ri->periodic_probe_complete_cnt = 0;
      ri->periodic_responsive_cnt = 0;
      prober->round_info_cnt++;
      return 0;
    }
  }

  return -1;
}

static round_info_t * get_round(trinarkular_prober_t *prober,
                                uint32_t round_id)
{
  int i;

  for (i=0; i<MAX_IN_PROGRESS_ROUNDS; i++) {
    if (prober->round_info[i].in_use != 0 &&
        prober->round_info[i].id == round_id) {
      return &prober->round_info[i];
    }
  }

  return NULL;
}

static int add_probe_state(struct driver_wrap *dw,
                           seq_num_t seq_num,
                           prober_slash24_state_t *state)
{
  khiter_t k;
  int khret;

  uint64_t probe_id = ((uint64_t)dw->id << 32) | seq_num;

  k = kh_put(seq_state, dw->prober->probe_state, probe_id, &khret);
  if (khret == -1) {
    trinarkular_log("ERROR: Could not add probe state to hash");
    return -1;
  }
  kh_val(dw->prober->probe_state, k) = state;

  return 0;
}

static prober_slash24_state_t *find_probe_state(struct driver_wrap *dw,
                                                seq_num_t seq_num)
{
  khiter_t k;
  prober_slash24_state_t *state;

  uint64_t probe_id = ((uint64_t)dw->id << 32) | seq_num;

  if ((k = kh_get(seq_state, dw->prober->probe_state, probe_id))
      == kh_end(dw->prober->probe_state)) {
    return NULL;
  }

  state = kh_val(dw->prober->probe_state, k);

  kh_del(seq_state, dw->prober->probe_state, k);

  return state;
}

static uint32_t get_next_host(trinarkular_probelist_slash24_t *s24)
{
  trinarkular_probelist_host_t *host =
    trinarkular_probelist_get_next_host(s24);

  if (host == NULL) {
    trinarkular_probelist_reset_host_iter(s24);
    host = trinarkular_probelist_get_next_host(s24);
    assert(host != NULL);
  }

  return trinarkular_probelist_get_host_ip(s24, host);
}

/** Queue a probe for the given /24 */
static int queue_slash24_probe(trinarkular_prober_t *prober,
                               prober_slash24_state_t *slash24_state,
                               round_info_t *ri)
{
  uint32_t network_ip;
  uint32_t host_ip;
  uint32_t tmp;
  char netbuf[INET_ADDRSTRLEN];
  char ipbuf[INET_ADDRSTRLEN];

  trinarkular_probe_req_t req = {
    0,
    PARAM(periodic_max_probecnt),
    PARAM(periodic_probe_timeout),
  };
  seq_num_t seq_num;
  struct driver_wrap *dw;

  assert(slash24_state != NULL);
  // slash24_state is valid here

  // its up to the caller to ensure that we have enough probes in the budget
  assert(slash24_state->probe_budget > 0);

  network_ip =
    trinarkular_probelist_get_network_ip(slash24_state->s24);
  tmp = htonl(network_ip);
  inet_ntop(AF_INET, &tmp, netbuf, INET_ADDRSTRLEN);

  // identify the appropriate host to probe
  host_ip = get_next_host(slash24_state->s24);
  req.target_ip = htonl(host_ip);
  inet_ntop(AF_INET, &req.target_ip, ipbuf, INET_ADDRSTRLEN);

  // indicate that we are waiting for a response
  slash24_state->round_id = ri->id;
  // decrement the probe budget
  slash24_state->probe_budget--;

  dw = &prober->drivers[prober->drivers_next];
  if ((seq_num =
       trinarkular_driver_queue_req(dw->driver,
                                    &req)) == 0) {
    return -1;
  }
  if (add_probe_state(dw, seq_num, slash24_state) != 0) {
    return -1;
  }

  prober->drivers_next = (prober->drivers_next + 1) % prober->drivers_cnt;

  return 0;
}

/** Queue the next set of periodic probes */
static int handle_timer(zloop_t *loop, int timer_id, void *arg)
{
  trinarkular_prober_t *prober = (trinarkular_prober_t *)arg;
  int slice_cnt = 0;
  int queued_cnt = 0;

  uint64_t probing_round =
    prober->current_slice / PARAM(periodic_round_slices);

  uint64_t now = zclock_time();

  trinarkular_probelist_slash24_t *s24 = NULL;
  prober_slash24_state_t *slash24_state = NULL;

  round_info_t *ri;

  CHECK_SHUTDOWN;

  // have we reached the end of the probelist and need to start over?
  if (prober->probing_started == 0 ||
      trinarkular_probelist_has_more_slash24(prober->pl) == 0) {
    // only reset if the round has ended (should only happen when probelist is
    // smaller than slice count
    if (prober->current_slice % PARAM(periodic_round_slices) != 0) {
      trinarkular_log("No /24s left to probe in round %"PRIu64, probing_round);
      goto done;
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
    if (add_round(prober, probing_round, now) != 0) {
      trinarkular_log("ERROR: %d in-progress rounds, max is %d",
                      prober->round_info_cnt, MAX_IN_PROGRESS_ROUNDS);
      return -1;
    }

    prober->probing_started = 1;
  }

  // get the round-info for this round
  if ((ri = get_round(prober, probing_round)) == NULL) {
    trinarkular_log("ERROR: No state for round %d", probing_round);
    return -1;
  }

  // check if there is still an entire slice worth of requests outstanding. this
  // is a good indication that we are not keeping up with the probing rate.
  if (kh_size(prober->probe_state) > prober->slice_size) {
    trinarkular_log("ERROR: %d outstanding requests (slice size is %d)",
                    kh_size(prober->probe_state), prober->slice_size);
    return -1;
  }

  trinarkular_log("INFO: %d outstanding requests (slice size is %d)",
                  kh_size(prober->probe_state), prober->slice_size);

  for (slice_cnt=0; slice_cnt < prober->slice_size; slice_cnt++) {
    // get a slash24 to probe
    if ((s24 = trinarkular_probelist_get_next_slash24(prober->pl)) == NULL) {
      // end of /24s
      break;
    }
    // get or create the user state for this /24
    if ((slash24_state =
         trinarkular_probelist_get_slash24_user(s24)) == NULL &&
        (slash24_state = prober_slash24_state_create(prober, s24)) == NULL) {
      return -1;
    }
    assert(slash24_state != NULL);
    // reset the probe budget
    slash24_state->probe_budget = TRINARKULAR_PROBER_ROUND_PROBE_BUDGET;
    // queue a probe to a random host
    if (queue_slash24_probe(prober, slash24_state, ri) != 0) {
      return -1;
    }
    queued_cnt++;
    ri->periodic_probe_cnt++; // we ALWAYS probe all /24s in a slice
  }

  trinarkular_log("Queued %d /24s in slice %"PRIu64" (round: %"PRIu64")",
                  queued_cnt,
                  prober->current_slice,
                  probing_round);

 done:
  prober->current_slice++;
  CHECK_SHUTDOWN;
  return 0;
}

static int end_of_round(trinarkular_prober_t *prober, round_info_t *ri)
{
  uint64_t now = zclock_time();
  uint64_t aligned_start =
    ((uint64_t)(ri->start_time / PARAM(periodic_round_duration))) *
    PARAM(periodic_round_duration);

  timeseries_kp_set(prober->kp, prober->metrics.round_id, ri->id);
  timeseries_kp_set(prober->kp, prober->metrics.round_duration,
                    now - ri->start_time);
  timeseries_kp_set(prober->kp, prober->metrics.round_probe_cnt,
                    ri->periodic_probe_cnt);
  timeseries_kp_set(prober->kp, prober->metrics.round_probe_complete_cnt,
                    ri->periodic_probe_complete_cnt);
  timeseries_kp_set(prober->kp, prober->metrics.round_responsive_cnt,
                    ri->periodic_responsive_cnt);

  trinarkular_log("round %d completed in %"PRIu64"ms (ideal: %"PRIu64"ms)",
                  ri->id, now - ri->start_time,
                  PARAM(periodic_round_duration));
  trinarkular_log("round response rate: %d/%d (%0.0f%%)",
                  ri->periodic_responsive_cnt,
                  ri->periodic_probe_cnt,
                  ri->periodic_responsive_cnt * 100.0
                  / ri->periodic_probe_cnt);

  // flush the kp
  return timeseries_kp_flush(prober->kp, aligned_start/1000);
}

// update the bayesian model given a positive (1) or negative (0) probe response
static float update_bayesian_belief(prober_slash24_state_t *slash24,
                                    int probe_response)
{
  float Ppd;
  float new_belief_down;

  float aeb = trinarkular_probelist_get_aeb(slash24->s24);

  // P(p|~U)
  Ppd = (((1.0 - PACKET_LOSS_FREQUENCY) / TRINARKULAR_SLASH24_HOST_CNT) *
       (1.0 - slash24->current_belief));

  if (probe_response) {
    new_belief_down = Ppd / (Ppd + (aeb * slash24->current_belief));
  } else {
    new_belief_down = (1.0-Ppd) /
      ((1.0-Ppd) + ((1-aeb) * slash24->current_belief));
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
  prober_slash24_state_t *slash24_state = NULL;
  round_info_t *ri;
  float new_belief_up;

  CHECK_SHUTDOWN;

  if (trinarkular_driver_recv_resp(dw->driver, &resp, 0) != 1) {
    trinarkular_log("Could not receive response");
    goto err;
  }

  // TARGET IP IS IN NETWORK BYTE ORDER

  // find the state for this /24
  if ((slash24_state = find_probe_state(dw, resp.seq_num)) == NULL) {
    trinarkular_log("ERROR: Missing state for %x", ntohl(resp.target_ip));
    goto err;
  }

  // NB: we are treating adaptive responses the same as periodic responses

  // get the round-info for this /24
  if ((ri = get_round(prober, slash24_state->round_id)) == NULL) {
    trinarkular_log("ERROR: No state for round %d", slash24_state->round_id);
    return -1;
  }

  new_belief_up =
    update_bayesian_belief(slash24_state, resp.verdict);

  // are we moving toward uncertainty?

  // if we are currently up, then has belief dropped?
  if (BELIEF_STATE(slash24_state->current_belief) == UP &&
      (slash24_state->current_belief - new_belief_up) > 0) {
    // this is a move toward uncertainty, trigger an adaptive probe
    trinarkular_log("UP(%f) towards ?(%f), triggering adaptive probe",
                    slash24_state->current_belief, new_belief_up);
  }

  // if we are currently down, then has the belief increased?
  if (BELIEF_STATE(slash24_state->current_belief) == DOWN &&
      (slash24_state->current_belief - new_belief_up) < 0) {
    // this is a move toward uncertainty, trigger an adaptive probe
    trinarkular_log("DOWN(%f) towards ?(%f), triggering adaptive probe",
                    slash24_state->current_belief, new_belief_up);
  }

  // TODO: if the belief is uncertain, AND we have adaptive probes left in the
  // budget for this /24, then fire one off
  if (BELIEF_STATE(slash24_state->current_belief) == UNCERTAIN &&
      BELIEF_STATE(new_belief_up) == UNCERTAIN ) {
      // && have probes available
    trinarkular_log("UNCERTAIN(%f) -> UNCERTAIN(%f), trigger adaptive probe",
                    slash24_state->current_belief, new_belief_up);
  }

  // update the belief
  slash24_state->current_belief = new_belief_up;

  // update the overall per-round statistics
  ri->periodic_probe_complete_cnt++;
  ri->periodic_responsive_cnt += resp.verdict;

  // check if this is the last response for this round
  if (ri->periodic_probe_complete_cnt ==
      trinarkular_probelist_get_slash24_cnt(prober->pl)) {
    // end-of-round statistics
    if (end_of_round(prober, ri) != 0) {
      goto err;
    }

    ri->in_use = 0;
    prober->round_info_cnt--;
  }

  return 0;

 err:
  return -1;
}

static int
start_driver(struct driver_wrap *dw,
             char *driver_name, char *driver_config)
{
  // start user-specified driver
  if ((dw->driver =
       trinarkular_driver_create_by_name(driver_name,
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

trinarkular_prober_t *
trinarkular_prober_create(const char *name, timeseries_t *timeseries)
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
  graphite_safe(prober->name);

  // create a key package and init metrics
  if ((prober->kp = timeseries_kp_init(prober->ts, 1)) == NULL ||
      init_kp(prober) != 0) {
    trinarkular_log("ERROR: Could not create timeseries key package");
    goto err;
  }

  // create the reactor
  if ((prober->loop = zloop_new()) == NULL) {
    trinarkular_log("ERROR: Could not initialize reactor");
    goto err;
  }

  // create the probe state hash
  if ((prober->probe_state = kh_init(seq_state)) == NULL) {
    trinarkular_log("ERROR: Could not initialize probe state hash");
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
  int i;

  if (prober == NULL) {
    return;
  }

  zloop_destroy(&prober->loop);

  if (kh_size(prober->probe_state) != 0) {
    trinarkular_log("WARN: %d outstanding probes at shutdown",
                    kh_size(prober->probe_state));
  }
  kh_destroy(seq_state, prober->probe_state);
  prober->probe_state = NULL;

  // shut down the probe driver(s)
  for (i=0; i<prober->drivers_cnt; i++) {
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

void
trinarkular_prober_assign_probelist(trinarkular_prober_t *prober,
                                    trinarkular_probelist_t *pl)
{
  assert(prober != NULL);
  assert(prober->started == 0);

  prober->pl = pl;

  // we now randomize the /24 ordering
  trinarkular_probelist_randomize_slash24s(pl, PARAM(random_seed));

  trinarkular_log("Probelist size: %d /24s",
                  trinarkular_probelist_get_slash24_cnt(pl));
}

int
trinarkular_prober_start(trinarkular_prober_t *prober)
{
  uint32_t periodic_timeout;
  uint64_t now = zclock_time();
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
  if ((prober->periodic_timer_id = zloop_timer(prober->loop,
                                               periodic_timeout, 0,
                                               handle_timer, prober)) < 0) {
    trinarkular_log("ERROR: Could not create periodic timer");
    return -1;
  }

  // compute the slice size
  prober->slice_size =
    trinarkular_probelist_get_slash24_cnt(prober->pl) /
    PARAM(periodic_round_slices);
  if (prober->slice_size * PARAM(periodic_round_slices) <
      trinarkular_probelist_get_slash24_cnt(prober->pl)) {
    // round up to ensure we cover everything in the interval
    prober->slice_size++;
  }
  trinarkular_log("Periodic Probing Slice Size: %d", prober->slice_size);

  // start the default driver if needed
  if (prober->drivers_cnt == 0 &&
      trinarkular_prober_add_driver(prober,
                                    TRINARKULAR_PROBER_DRIVER_DEFAULT,
                                    TRINARKULAR_PROBER_DRIVER_ARGS_DEFAULT)
      != 0) {
    return -1;
  }

  prober->started = 1;

  // wait so that our round starts at a nice time
  aligned_start =
    (((uint64_t)(now / PARAM(periodic_round_duration))) *
     PARAM(periodic_round_duration)) + PARAM(periodic_round_duration);

  trinarkular_log("sleeping for %d seconds to align with round duration",
                  (aligned_start/1000) - (now/1000));
  sleep((aligned_start/1000) - (now/1000));

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

void
trinarkular_prober_set_periodic_round_duration(trinarkular_prober_t *prober,
                                               uint64_t duration)
{
  assert(prober != NULL);

  trinarkular_log("%"PRIu64, duration);
  PARAM(periodic_round_duration) = duration;
}

void
trinarkular_prober_set_periodic_round_slices(trinarkular_prober_t *prober,
                                             int slices)
{
  assert(prober != NULL);

  trinarkular_log("%d", slices);
  PARAM(periodic_round_slices) = slices;
}

void
trinarkular_prober_set_periodic_round_limit(trinarkular_prober_t *prober,
                                            int rounds)
{
  assert(prober != NULL);

  trinarkular_log("%d", rounds);
  PARAM(periodic_round_limit) = rounds;
}

void
trinarkular_prober_set_periodic_max_probecount(trinarkular_prober_t *prober,
                                               uint8_t probecount)
{
  assert(prober != NULL);

  trinarkular_log("%"PRIu8, probecount);
  PARAM(periodic_max_probecnt) = probecount;
}

void
trinarkular_prober_set_periodic_probe_timeout(trinarkular_prober_t *prober,
                                              uint32_t timeout)
{
  assert(prober != NULL);

  trinarkular_log("%"PRIu32, timeout);
  PARAM(periodic_probe_timeout) = timeout;
}

void
trinarkular_prober_set_random_seed(trinarkular_prober_t *prober,
                                   int seed)
{
  assert(prober != NULL);

  trinarkular_log("%d", seed);
  PARAM(random_seed) = seed;
}

#include "trinarkular_driver_interface.h"

int
trinarkular_prober_add_driver(trinarkular_prober_t *prober,
                              char *driver_name, char *driver_args)
{
  assert(prober != NULL);

  trinarkular_log("%s %s", driver_name, driver_args);

  prober->drivers[prober->drivers_cnt].id = prober->drivers_cnt;
  prober->drivers[prober->drivers_cnt].prober = prober;

  // initialize the driver
  if (start_driver(&prober->drivers[prober->drivers_cnt],
                   driver_name, driver_args) != 0) {
    return -1;
  }

  prober->drivers_cnt++;

  trinarkular_log("%d drivers", prober->drivers_cnt);

  return 0;
}
