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
#include <stdio.h>
#include <wandio.h>

#include "khash.h"
#include "wandio_utils.h"

#include "trinarkular.h"
#include "trinarkular_log.h"
#include "trinarkular_probelist.h"

/* -------------------- TARGET HOST -------------------- */

/** Represents a host within a /24 that should be probed */
typedef struct target_host {

  /** The host portion of the IP address to probe (to be added to
      slash24.network_ip) */
  uint8_t host;

  /** The (recent) historical response rate for this host */
  float resp_rate; // may not need this?

} __attribute__((packed)) target_host_t;

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
  float avg_host_resp_rate;

} __attribute__((packed)) target_slash24_t;

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

static target_host_t*
get_host(target_slash24_t *s, uint32_t host_ip)
{
  int k;
  target_host_t findme;
  findme.host = host_ip & TRINARKULAR_SLASH24_HOSTMASK;
  if ((k = kh_get(target_host_set, s->hosts, findme))
      == kh_end(s->hosts)) {
    return NULL;
  }
  return &kh_key(s->hosts, k);
}

/* -------------------- PROBELIST -------------------- */

/** Structure representing a Trinarkular probelist */
struct trinarkular_probelist {

  /** Set of target /24s to probe */
  khash_t(target_slash24_set) *slash24s;

  /** The total number of hosts in this probelist */
  uint64_t host_cnt;

};

static target_slash24_t*
get_slash24(trinarkular_probelist_t *pl, uint32_t network_ip)
{
  int k;
  target_slash24_t findme;
  findme.network_ip = network_ip;
  if ((k = kh_get(target_slash24_set, pl->slash24s, findme))
      == kh_end(pl->slash24s)) {
    return NULL;
  }
  return &kh_key(pl->slash24s, k);
}


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

  return pl;
}

trinarkular_probelist_t *
trinarkular_probelist_create_from_file(const char *filename)
{
  trinarkular_probelist_t *pl = NULL;;
  io_t *infile = NULL;

  char buffer[1024];
  char *bufp = NULL;
  char *np = NULL;

  uint32_t slash24_cnt = 0;
  uint64_t host_cnt = 0;

  uint32_t network_ip, host_ip;
  int cnt;
  double resp;

  trinarkular_log("Creating probelist from %s", filename);
  if ((pl = trinarkular_probelist_create()) == NULL) {
    goto err;
  }

  if ((infile = wandio_create(filename)) == NULL) {
    trinarkular_log("ERROR: Could not open %s for reading\n", filename);
    goto err;
  }

  while (wandio_fgets(infile, buffer, 1024, 1) != 0) {
    bufp = buffer;

    if (bufp[0] == '#' && bufp[1] == '#') {
      // this is a slash24
      slash24_cnt++;
      bufp += 3; // skip over "## "

      // parse the network ip
      if ((np = strchr(bufp, ' ')) == NULL) {
        trinarkular_log("ERROR: Malformed /24 line: %d", buffer);
        goto err;
      }
      *np = '\0';
      network_ip = strtoul(bufp, NULL, 16) << 8; // probelist has this shifted

      // parse the host count
      bufp = np+1;
      if ((np = strchr(bufp, ' ')) == NULL) {
        trinarkular_log("ERROR: Malformed /24 line: %d", buffer);
        goto err;
      }
      *np = '\0';
      np++;
      cnt = strtoul(bufp, NULL, 10);

      // parse the response rate
      resp = strtof(np, NULL);

      if (trinarkular_probelist_add_slash24(pl, network_ip, resp) != 0) {
        goto err;
      }

      if ((slash24_cnt % 100000) == 0)  {
        trinarkular_log("Parsed %d /24s from file", slash24_cnt);
      }
    } else {
      // this is a host
      host_cnt++;

      // parse the host ip
      if ((np = strchr(bufp, ' ')) == NULL) {
        trinarkular_log("ERROR: Malformed /24 line: %d", buffer);
        goto err;
      }
      *np = '\0';
      np++;
      host_ip = strtoul(bufp, NULL, 16);

      assert((host_ip & TRINARKULAR_SLASH24_NETMASK) == network_ip);

      // parse the response rate
      resp = strtof(np, NULL);

      if (trinarkular_probelist_slash24_add_host(pl, host_ip, resp) != 0) {
        goto err;
      }
    }
  }

  if (trinarkular_probelist_get_slash24_cnt(pl) != slash24_cnt) {
    trinarkular_log("ERROR: Probelist file has %d /24s, probelist object has %d",
                    slash24_cnt,
                    trinarkular_probelist_get_slash24_cnt(pl));
  }

  if (trinarkular_probelist_get_host_cnt(pl) != host_cnt) {
    trinarkular_log("ERROR: Probelist file has %d hosts, probelist object has %d",
                    host_cnt,
                    trinarkular_probelist_get_host_cnt(pl));
  }

  trinarkular_log("done");
  trinarkular_log("loaded %d /24s",
          trinarkular_probelist_get_slash24_cnt(pl));
  trinarkular_log("loaded %"PRIu64" hosts",
          trinarkular_probelist_get_host_cnt(pl));

  return pl;

 err:
  wandio_destroy(infile);
  trinarkular_probelist_destroy(pl);
  return NULL;
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

  assert(pl != NULL);
  assert((network_ip & TRINARKULAR_SLASH24_NETMASK) == network_ip);

  if ((s = get_slash24(pl, network_ip)) == NULL) {
    // need to insert it
    findme.network_ip = network_ip;
    k = kh_put(target_slash24_set, pl->slash24s, findme, &khret);
    if (khret == -1) {
      trinarkular_log("ERROR: Could not add /24 to probelist");
      return -1;
    }

    s = &kh_key(pl->slash24s, k);
    if ((s->hosts = kh_init(target_host_set)) == NULL) {
      trinarkular_log("ERROR: Could not allocate host set");
    }
  }

  // we promised to always update the avg_resp_rate
  s->avg_host_resp_rate = avg_resp_rate;

  return 0;
}

int trinarkular_probelist_slash24_add_host(trinarkular_probelist_t *pl,
                                           uint32_t host_ip,
                                           double resp_rate)
{
  target_slash24_t *s = NULL;
  target_host_t findme;
  target_host_t *h = NULL;
  khiter_t k;
  int khret;

  assert(pl != NULL);

  if ((s = get_slash24(pl, host_ip & TRINARKULAR_SLASH24_NETMASK)) == NULL) {
    trinarkular_log("ERROR: Missing /24 %x for host %x",
                    host_ip & TRINARKULAR_SLASH24_NETMASK, host_ip);
    return -1;
  }

  assert((host_ip & TRINARKULAR_SLASH24_NETMASK) == s->network_ip);

  if ((h = get_host(s, host_ip)) == NULL) {
    // need to insert it
    findme.host = host_ip & TRINARKULAR_SLASH24_HOSTMASK;
    k = kh_put(target_host_set, s->hosts, findme, &khret);
    if (khret == -1) {
      trinarkular_log("ERROR: Could not add host to /24");
      return -1;
    }
    pl->host_cnt++;
    h = &kh_key(s->hosts, k);
  }

  // we promised to always update the resp_rate
  h->resp_rate = resp_rate;

  return 0;
}

int
trinarkular_probelist_get_slash24_cnt(trinarkular_probelist_t *pl)
{
  assert(pl != NULL);
  return kh_size(pl->slash24s);
}

uint64_t trinarkular_probelist_get_host_cnt(trinarkular_probelist_t *pl)
{
  assert(pl != NULL);
  return pl->host_cnt;
}
