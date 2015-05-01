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

#include "khash.h"

#include "trinarkular_log.h"
#include "trinarkular_probelist.h"

/** The total number of /24s in IPv4 */
#define SLASH24_CNT 0x1000000

/** Netmask for a /24 network */
#define SLASH24_MASK 0xffffff00

/* -------------------- TARGET HOST -------------------- */

/** Represents a host within a /24 that should be probed */
typedef struct target_host {

  /** The host portion of the IP address to probe (to be added to
      slash24.network_ip) */
  uint8_t host;

  /** The (recent) historical response rate for this host */
  double resp_rate; // may not need this?

} target_host_t;

/** Hash a target host */
#define target_host_hash(a) ((a).host)

/** Compare two target hosts for equality */
#define target_host_eq(a, b) ((a).host == (b).host)

KHASH_INIT(target_host_set, target_host_t, char, 0,
           target_host_hash, target_host_eq);

static void
target_host_destroy(target_host_t h)
{
  // nothing to do
}



/* -------------------- TARGET SLASH24 -------------------- */

/** Represents a single target /24 that should be probed */
typedef struct target_slash24 {

  /** The network IP (first IP) of this /24 (in host byte order) */
  uint32_t network_ip;

  /** Set of target hosts to probe for this /24 */
  khash_t(target_host_set) *hosts;

  /** The average response rate of recently responding hosts in this /24
   * (I.e. the A(E(b)) value from the paper) */
  double avg_host_resp_rate;

} target_slash24_t;

/** Hash a target /24 */
#define target_slash24_hash(a) ((a).network_ip)

/** Compare two target hosts for equality */
#define target_slash24_eq(a, b) ((a).network_ip == (b).network_ip)

KHASH_INIT(target_slash24_set, target_slash24_t, char, 0,
           target_slash24_hash, target_slash24_eq);

static void
target_slash24_destroy(target_slash24_t t)
{
  kh_free(target_host_set, t.hosts, target_host_destroy);
  kh_destroy(target_host_set, t.hosts);
  t.hosts = NULL;
}


/* -------------------- PROBELIST -------------------- */

/** Structure representing a Trinarkular probelist */
struct trinarkular_probelist {

  /** Set of target /24s to probe */
  khash_t(target_slash24_set) *slash24s;

  /** The total number of hosts in this probelist */
  uint64_t host_cnt;

};




/* ==================== PUBLIC FUNCTIONS ==================== */

trinarkular_probelist_t *
trinarkular_probelist_create()
{
  trinarkular_probelist_t *pl;

  if ((pl = malloc(sizeof(trinarkular_probelist_t))) == NULL) {
    trinarkular_log("ERROR: Could not allocate probelist");
    return NULL;
  }

  if ((pl->slash24s = kh_init(target_slash24_set)) == NULL) {
    trinarkular_log("ERROR: Could not allocate /24 set");
  }

  trinarkular_log("INFO: Successfully created probelist\n");

  return pl;
}

void
trinarkular_probelist_destroy(trinarkular_probelist_t *pl)
{
  if (pl == NULL) {
    return;
  }

  kh_free(target_slash24_set, pl->slash24s, target_slash24_destroy);
  kh_destroy(target_slash24_set, pl->slash24s);
  pl->slash24s = NULL;

  free(pl);
}

int
trinarkular_probelist_add_slash24(trinarkular_probelist_t *pl,
                                  uint32_t network_ip,
                                  double avg_resp_rate)
{
  khiter_t k;
  int khret;
  target_slash24_t *s = NULL;
  target_slash24_t findme;
  findme.network_ip = network_ip;

  assert(pl != NULL);
  assert((network_ip & SLASH24_NETMASK) == network_ip);

  if ((k = kh_get(target_slash24_set, pl->slash24s, findme))
      == kh_end(pl->slash24s)) {
    // need to insert it
    k = kh_put(target_slash24_set, pl->slash24s, findme, &khret);
    if (khret == -1) {
      trinarkular_log("ERROR: Could not add /24 to probelist");
      return -1;
    }

    s = &kh_key(pl->slash24s, k);
    if ((s->hosts = kh_init(target_host_set)) == NULL) {
      trinarkular_log("ERROR: Could not allocate host set\n");
    }
  }

  // we promised to always update the avg_resp_rate
  s = &kh_key(pl->slash24s, k);
  s->avg_host_resp_rate = avg_resp_rate;

  return k;
}

int trinarkular_probelist_slash24_add_host(trinarkular_probelist_t *pl,
                                           int slash24_id,
                                           uint32_t host_ip,
                                           double resp_rate)
{
  target_slash24_t *s = NULL;
  target_host_t findme;
  target_host_t *h = NULL;
  khiter_t k;
  int khret;

  assert(pl != NULL);
  assert(slash24_id >= 0);

  s = &kh_key(pl->slash24s, slash24_id);
  assert(s != NULL);

  assert((host_ip & SLASH24_MASK) == s->network_ip);

  findme.host = host_ip & 0xff;
  if ((k = kh_get(target_host_set, s->hosts, findme))
      == kh_end(s->hosts)) {
    // need to insert it
    k = kh_put(target_host_set, s->hosts, findme, &khret);
    if (khret == -1) {
      trinarkular_log("ERROR: Could not add host to /24");
      return -1;
    }
    pl->host_cnt++;
  }

  // we promised to always update the resp_rate
  h = &kh_key(s->hosts, k);
  h->resp_rate = resp_rate;

  return 0;
}

int
trinarkular_probelist_get_slash24_cnt(trinarkular_probelist_t *pl)
{
  assert(pl != NULL);
  return kh_size(pl->slash24s);
}

int trinarkular_probelist_get_host_cnt(trinarkular_probelist_t *pl)
{
  assert(pl != NULL);
  return pl->host_cnt;
}
