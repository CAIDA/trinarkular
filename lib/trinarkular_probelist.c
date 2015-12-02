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
#include "utils.h"
#include "wandio_utils.h"
#include "jsmn.h"

#include "trinarkular.h"
#include "trinarkular_log.h"
#include "trinarkular_probelist.h"

/* -------------------- TARGET HOST -------------------- */

/** Represents a host within a /24 that should be probed */
struct trinarkular_probelist_host {

  /** The host portion of the IP address to probe (to be added to
      slash24.network_ip) */
  uint8_t host;

  /** The (recent) historical response rate for this host */
  float resp_rate; // may not need this?

  /** User pointer for this host */
  void *user;

} __attribute__((packed));

/** Hash a target host */
#define target_host_hash(a) ((a).host)

/** Compare two target hosts for equality */
#define target_host_eq(a, b) ((a).host == (b).host)

#define HOST_IDX(slash24)                             \
  (slash24->hosts_order ?                             \
   slash24->hosts_order[slash24->host_iter] :         \
   slash24->host_iter)

#define GET_HOST(slash24)                       \
  (&(kh_key(slash24->hosts, HOST_IDX(slash24))))

KHASH_INIT(target_host_set, trinarkular_probelist_host_t, char, 0,
           target_host_hash, target_host_eq);

static void
target_host_destroy(trinarkular_probelist_host_t h)
{
  // nothing to do
}



/* -------------------- TARGET SLASH24 -------------------- */

/** Represents a single target /24 that should be probed */
struct trinarkular_probelist_slash24 {

  /** The network IP (first IP) of this /24 (in host byte order) */
  uint32_t network_ip;

  /** Set of target hosts to probe for this /24 */
  khash_t(target_host_set) *hosts;

  /** Array of iterator values (for random walks) */
  khiter_t *hosts_order;

  /** The current host */
  khiter_t host_iter;

  /** The average response rate of recently responding hosts in this /24
   * (I.e. the A(E(b)) value from the paper) */
  float avg_host_resp_rate;

  /** User pointer for this /24 */
  void *user;

  /** List of metadata */
  char **md;

  /** Number of items in metadata list */
  int md_cnt;

} __attribute__((packed));

/** Hash a target /24 */
#define target_slash24_hash(a) ((a).network_ip)

/** Compare two target hosts for equality */
#define target_slash24_eq(a, b) ((a).network_ip == (b).network_ip)

#define SLASH24_IDX(pl)                             \
  (pl->slash24s_order ?                             \
   pl->slash24s_order[pl->slash24_iter] :           \
   pl->slash24_iter)

#define GET_SLASH24(pl)                       \
  (&(kh_key(pl->slash24s, SLASH24_IDX(pl))))

KHASH_INIT(target_slash24_set, trinarkular_probelist_slash24_t, char, 0,
           target_slash24_hash, target_slash24_eq);

static trinarkular_probelist_host_t*
get_host(trinarkular_probelist_slash24_t *s, uint32_t host_ip)
{
  int k;
  trinarkular_probelist_host_t findme;
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

  /** Array of iterator values (for random walks) */
  khiter_t *slash24s_order;

  /** The total number of hosts in this probelist */
  uint64_t host_cnt;

  /** The current /24 */
  khiter_t slash24_iter;

  /** Destructor function for /24 user data */
  trinarkular_probelist_user_destructor_t *slash24_user_destructor;

    /** Destructor function for host user data */
  trinarkular_probelist_user_destructor_t *host_user_destructor;

};

static void
target_slash24_destroy(trinarkular_probelist_t *pl,
                       trinarkular_probelist_slash24_t *t)
{
  kh_free(target_host_set, t->hosts, target_host_destroy);
  kh_destroy(target_host_set, t->hosts);
  t->hosts = NULL;

  free(t->hosts_order);
  t->hosts_order = NULL;

  if (pl->slash24_user_destructor != NULL && t->user != NULL) {
    pl->slash24_user_destructor(t->user);
  }

  free(t->md);
  t->md = NULL;
  t->md_cnt = 0;
}

static trinarkular_probelist_slash24_t*
get_slash24(trinarkular_probelist_t *pl, uint32_t network_ip)
{
  int k;
  trinarkular_probelist_slash24_t findme;
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

  if ((pl = malloc_zero(sizeof(trinarkular_probelist_t))) == NULL) {
    trinarkular_log("ERROR: Could not allocate probelist");
    return NULL;
  }

  if ((pl->slash24s = kh_init(target_slash24_set)) == NULL) {
    trinarkular_log("ERROR: Could not allocate /24 set");
  }

  pl->slash24_iter = kh_end(pl->slash24s);

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

  trinarkular_probelist_slash24_t *s24 = NULL;

  uint32_t slash24_cnt = 0;
  uint32_t slash24_host_cnt = 0;
  uint64_t host_cnt = 0;

  uint32_t network_ip = 0, host_ip = 0;
  int cnt = 0;
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

    if (strncmp("SLASH24 ", buffer, 8) == 0) {
      // this is a slash24
      slash24_cnt++;

      // ensure the last /24 had the correct number of hosts
      if (slash24_host_cnt != cnt) {
        trinarkular_log("ERROR: /24 header reported %d hosts, %d found",
                        cnt, slash24_host_cnt);
        goto err;
      }
      slash24_host_cnt = 0;
      bufp += 8; // skip over "SLASH24 "

      // parse the network ip
      if ((np = strchr(bufp, ' ')) == NULL) {
        trinarkular_log("ERROR: Malformed /24 line: %s", buffer);
        goto err;
      }
      *np = '\0';
      network_ip = strtoul(bufp, NULL, 16);

      // parse the host count
      bufp = np+1;
      if ((np = strchr(bufp, ' ')) == NULL) {
        trinarkular_log("ERROR: Malformed /24 line: %s", buffer);
        goto err;
      }
      *np = '\0';
      np++;
      cnt = strtoul(bufp, NULL, 10);

      // parse the response rate
      resp = strtof(np, NULL);

      if ((s24 =
           trinarkular_probelist_add_slash24(pl, network_ip, resp)) == NULL) {
        goto err;
      }

      if ((slash24_cnt % 100000) == 0)  {
        trinarkular_log("Parsed %d /24s from file", slash24_cnt);
      }
    } else if (strncmp("SLASH24_META ", buffer, 13) == 0) {
      bufp += 13; // skip over "SLASH24_META "

      // add metadata to the /24
      if (trinarkular_probelist_slash24_add_metadata(pl, s24, bufp) != 0) {
        goto err;
      }
    } else {
      // this is a host
      host_cnt++;
      slash24_host_cnt++;

      // parse the host ip
      if ((np = strchr(bufp, ' ')) == NULL) {
        trinarkular_log("ERROR: Malformed /24 line: %d", buffer);
        goto err;
      }
      *np = '\0';
      np++;
      host_ip = strtoul(bufp, NULL, 16);

      // if 0/8 starts being used, then this will cause a problem
      assert(network_ip != 0);
      if ((host_ip & TRINARKULAR_SLASH24_NETMASK) != network_ip) {
        trinarkular_log("ERROR: Host/Network mismatch: %x %x",
                        host_ip, network_ip);
        goto err;
      }

      // parse the response rate
      resp = strtof(np, NULL);

      if (trinarkular_probelist_slash24_add_host(pl, s24, host_ip, resp)
          == NULL) {
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

  wandio_destroy(infile);

  return pl;

 err:
  wandio_destroy(infile);
  trinarkular_probelist_destroy(pl);
  return NULL;
}

void
trinarkular_probelist_destroy(trinarkular_probelist_t *pl)
{
  khiter_t k;
  if (pl == NULL) {
    return;
  }

  for (k = kh_begin(pl->slash24s); k < kh_end(pl->slash24s); k++) {
    if (kh_exist(pl->slash24s, k)) {
      target_slash24_destroy(pl, &kh_key(pl->slash24s, k));
    }
  }
  kh_destroy(target_slash24_set, pl->slash24s);
  pl->slash24s = NULL;

  free(pl->slash24s_order);
  pl->slash24s_order = NULL;

  free(pl);
}

trinarkular_probelist_slash24_t *
trinarkular_probelist_add_slash24(trinarkular_probelist_t *pl,
                                  uint32_t network_ip,
                                  double avg_resp_rate)
{
  khiter_t k;
  int khret;
  trinarkular_probelist_slash24_t *s = NULL;
  trinarkular_probelist_slash24_t findme;

  assert(pl != NULL);
  assert((network_ip & TRINARKULAR_SLASH24_NETMASK) == network_ip);

  if ((s = get_slash24(pl, network_ip)) == NULL) {
    // need to insert it
    findme.network_ip = network_ip;
    findme.hosts = NULL;
    findme.hosts_order = NULL;
    findme.host_iter = 0;
    findme.avg_host_resp_rate = 0;
    findme.user = NULL;
    findme.md = NULL;
    findme.md_cnt = 0;
    k = kh_put(target_slash24_set, pl->slash24s, findme, &khret);
    if (khret == -1) {
      trinarkular_log("ERROR: Could not add /24 to probelist");
      return NULL;
    }

    s = &kh_key(pl->slash24s, k);
    if ((s->hosts = kh_init(target_host_set)) == NULL) {
      trinarkular_log("ERROR: Could not allocate host set");
    }
    pl->slash24_iter = kh_end(pl->slash24s);
  }

  // we promised to always update the avg_resp_rate
  s->avg_host_resp_rate = avg_resp_rate;

  return s;
}

trinarkular_probelist_host_t *
trinarkular_probelist_slash24_add_host(trinarkular_probelist_t *pl,
                                           trinarkular_probelist_slash24_t *s24,
                                           uint32_t host_ip,
                                           double resp_rate)
{
  trinarkular_probelist_host_t findme;
  trinarkular_probelist_host_t *h = NULL;
  khiter_t k;
  int khret;

  assert(pl != NULL);
  assert(s24 != NULL);
  assert((host_ip & TRINARKULAR_SLASH24_NETMASK) == s24->network_ip);

  if ((h = get_host(s24, host_ip)) == NULL) {
    // need to insert it
    findme.host = host_ip & TRINARKULAR_SLASH24_HOSTMASK;
    findme.resp_rate = 0;
    findme.user = NULL;
    k = kh_put(target_host_set, s24->hosts, findme, &khret);
    if (khret == -1) {
      trinarkular_log("ERROR: Could not add host to /24");
      return NULL;
    }
    pl->host_cnt++;
    h = &kh_key(s24->hosts, k);

    s24->host_iter = kh_end(s24->hosts);
  }

  // we promised to always update the resp_rate
  h->resp_rate = resp_rate;

  return h;
}

int
trinarkular_probelist_slash24_add_metadata(trinarkular_probelist_t *pl,
                                           trinarkular_probelist_slash24_t *s24,
                                           const char *md)
{
  if ((s24->md = realloc(s24->md, sizeof(char*) * (s24->md_cnt+1))) == NULL) {
    return -1;
  }
  if ((s24->md[s24->md_cnt] = strdup(md)) == NULL) {
    return -1;
  }
  s24->md_cnt++;

  return 0;
}

void
trinarkular_probelist_slash24_remove_metadata(trinarkular_probelist_t *pl,
                                          trinarkular_probelist_slash24_t *s24)
{
  free(s24->md);
  s24->md = NULL;
  s24->md_cnt = 0;
}

char **
trinarkular_probelist_slash24_get_metadata(trinarkular_probelist_t *pl,
                                           trinarkular_probelist_slash24_t *s24,
                                           int *md_cnt)
{
  *md_cnt = s24->md_cnt;
  return s24->md;
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

int
trinarkular_probelist_randomize_slash24s(trinarkular_probelist_t *pl,
                                        int seed)
{
  khiter_t k;
  int next_idx = 0;
  int i, r;

  if (pl->slash24s_order == NULL) {
    // we first need to build an array of iterator values for pl->slash24s
    if ((pl->slash24s_order =
         malloc(sizeof(khiter_t) * kh_size(pl->slash24s))) == NULL) {
      trinarkular_log("ERROR: Could not create ordering array");
      return -1;
    }
    for (k = kh_begin(pl->slash24s); k < kh_end(pl->slash24s); k++) {
      if (!kh_exist(pl->slash24s, k)) {
        continue;
      }
      pl->slash24s_order[next_idx++] = k;
    }
    assert(next_idx == kh_size(pl->slash24s));
  }

  srand(seed);

  // now randomize the ordering
  // http://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
  for (i=next_idx-1; i > 0; i--) {
    r = rand() % (i+1);
    k = pl->slash24s_order[i];
    pl->slash24s_order[i] = pl->slash24s_order[r];
    pl->slash24s_order[r] = k;
  }

  // now reset the iterator
  trinarkular_probelist_reset_slash24_iter(pl);

  trinarkular_log("done");

  return 0;
}

/* ==================== /24 ITERATOR FUNCS ==================== */

void
trinarkular_probelist_reset_slash24_iter(trinarkular_probelist_t *pl)
{
  if (pl->slash24s_order == NULL) {
    pl->slash24_iter = kh_begin(pl->slash24s);
    while (pl->slash24_iter < kh_end(pl->slash24s) &&
           !kh_exist(pl->slash24s, pl->slash24_iter)) {
      pl->slash24_iter++;
    }
  } else {
    pl->slash24_iter = 0;
  }
}

trinarkular_probelist_slash24_t *
trinarkular_probelist_get_next_slash24(trinarkular_probelist_t *pl)
{
  trinarkular_probelist_slash24_t *s24 = NULL;

  if (trinarkular_probelist_has_more_slash24(pl) == 0) {
    return NULL;
  }

  s24 = GET_SLASH24(pl);
  assert(s24 != NULL);

  if (pl->slash24s_order == NULL) {
    do {
      pl->slash24_iter++;
    } while(pl->slash24_iter < kh_end(pl->slash24s) &&
            !kh_exist(pl->slash24s, pl->slash24_iter));
  } else {
    pl->slash24_iter++;
  }

  if (trinarkular_probelist_has_more_slash24(pl) == 0) {
    return NULL;
  }

  return s24;
}

int
trinarkular_probelist_has_more_slash24(trinarkular_probelist_t *pl)
{
  if ((pl->slash24s_order == NULL &&
       pl->slash24_iter >= kh_end(pl->slash24s)) ||
      (pl->slash24_iter >= kh_size(pl->slash24s))) {
    return 0;
  }
  return 1;
}

uint32_t
trinarkular_probelist_get_network_ip(trinarkular_probelist_slash24_t *s24)
{
  return s24->network_ip;
}

float
trinarkular_probelist_get_aeb(trinarkular_probelist_slash24_t *s24)
{
  return s24->avg_host_resp_rate;
}

int
trinarkular_probelist_set_slash24_user(trinarkular_probelist_t *pl,
                            trinarkular_probelist_slash24_t *s24,
                            trinarkular_probelist_user_destructor_t *destructor,
                            void *user)
{
  void *old_user;
  assert(pl != NULL);

  // the iterator must be valid
  if (s24 == NULL) {
    return -1;
  }

  // the destructor must be new, or must match
  if (pl->slash24_user_destructor != NULL &&
      pl->slash24_user_destructor != destructor) {
    return -1;
  }

  pl->slash24_user_destructor = destructor;

  // if the user was set, destroy it
  if ((old_user = trinarkular_probelist_get_slash24_user(s24)) != NULL &&
      pl->slash24_user_destructor != NULL) {
    pl->slash24_user_destructor(old_user);
  }

  // now set the user
  s24->user = user;

  return 0;
}

void *
trinarkular_probelist_get_slash24_user(trinarkular_probelist_slash24_t *s24)
{
  assert(s24 != NULL);

  return s24->user;
}

int
trinarkular_probelist_slash24_randomize_hosts(
                                           trinarkular_probelist_slash24_t *s24,
                                           int seed)
{
  khiter_t k;
  int next_idx = 0;
  int i, r;

  assert(s24 != NULL);

  if (s24->hosts_order == NULL) {
    // we first need to build an array of iterator values for pl->slash24s
    if ((s24->hosts_order =
         malloc(sizeof(khiter_t) * kh_size(s24->hosts))) == NULL) {
      trinarkular_log("ERROR: Could not create ordering array");
      return -1;
    }
    for (k = kh_begin(s24->hosts); k < kh_end(s24->hosts); k++) {
      if (!kh_exist(s24->hosts, k)) {
        continue;
      }
      s24->hosts_order[next_idx++] = k;
    }
    assert(next_idx == kh_size(s24->hosts));
  }

  srand(seed);

  // now randomize the ordering
  // http://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
  for (i=next_idx-1; i > 0; i--) {
    r = rand() % (i+1);
    k = s24->hosts_order[i];
    s24->hosts_order[i] = s24->hosts_order[r];
    s24->hosts_order[r] = k;
  }

  // now reset the host iterator
  trinarkular_probelist_reset_host_iter(s24);

  return 0;
}

/* ==================== HOST ITERATOR FUNCS ==================== */

void
trinarkular_probelist_reset_host_iter(trinarkular_probelist_slash24_t *s24)
{
  assert(s24 != NULL);

  s24->host_iter = 0;

  if (s24->hosts_order == NULL) {
    s24->host_iter = kh_begin(s24->hosts);
    while (s24->host_iter < kh_end(s24->hosts) &&
           !kh_exist(s24->hosts, s24->host_iter)) {
      s24->host_iter++;
    }
  }
}

trinarkular_probelist_host_t *
trinarkular_probelist_get_next_host(trinarkular_probelist_slash24_t *s24)
{
  trinarkular_probelist_host_t *host;
  assert(s24 != NULL);

  if ((s24->hosts_order == NULL && s24->host_iter >= kh_end(s24->hosts)) ||
      (s24->host_iter >= kh_size(s24->hosts))) {
    return NULL;
  }

  host = GET_HOST(s24);

  if (s24->hosts_order == NULL) {
    do {
      s24->host_iter++;
    } while(s24->host_iter < kh_end(s24->hosts) &&
            !kh_exist(s24->hosts, s24->host_iter));
  } else {
    s24->host_iter++;
  }

  if ((s24->hosts_order == NULL && s24->host_iter >= kh_end(s24->hosts)) ||
      (s24->host_iter >= kh_size(s24->hosts))) {
    return NULL;
  }

  return host;
}

uint32_t
trinarkular_probelist_get_host_ip(trinarkular_probelist_slash24_t *s24,
                                  trinarkular_probelist_host_t *host)
{
  assert(s24 != NULL);
  assert(host != NULL);

  return s24->network_ip | host->host;
}

int
trinarkular_probelist_set_host_user(trinarkular_probelist_t *pl,
                            trinarkular_probelist_host_t *host,
                            trinarkular_probelist_user_destructor_t *destructor,
                            void *user)
{
  assert(host != NULL);

  void *old_user;

  // the destructor must be new, or must match
  if (pl->host_user_destructor != NULL &&
      pl->host_user_destructor != destructor) {
    return -1;
  }

  pl->host_user_destructor = destructor;

  // if the user was set, destroy it
  if ((old_user = trinarkular_probelist_get_host_user(host)) != NULL &&
      pl->host_user_destructor != NULL) {
    pl->host_user_destructor(old_user);
  }

  // now set the user
  host->user = user;

  return 0;
}

void *
trinarkular_probelist_get_host_user(trinarkular_probelist_host_t *host)
{
  assert(host != NULL);

  return host->user;
}
