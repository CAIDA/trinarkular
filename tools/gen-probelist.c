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

#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wandio.h>

#include <libipmeta.h>

#include "wandio_utils.h"
#include "utils.h"
#include "khash.h"

#include "trinarkular.h"

// the min number of ips that must respond before a /24 is considered
#define MIN_SLASH24_RESP_CNT 15

// the min avg response rate for a /24
#define MIN_SLASH24_AVG_RESP_RATE 0.1

#define UNSET 255

#define NETACQ_METRIC_PREFIX "geo.netacuity"
#define PFX2AS_METRIC_PREFIX "asn"
#define BLOCKS_METRIC_PREFIX "blocks"

KHASH_SET_INIT_STR(strset);
static khash_t(strset) *keyset = NULL;

// ipmeta instance
static ipmeta_t *ipmeta = NULL;
static ipmeta_record_set_t *records = NULL;
static ipmeta_provider_t *netacq_provider;
static ipmeta_provider_t *pfx2as_provider;

static char *netacq_loc_file = NULL;
static char *netacq_blocks_file = NULL;
static char *pfx2as_file = NULL;

/* should only summary stats be dumped? */
static int summary_only = 0;
static char *history_file = NULL;
static io_t *infile = NULL;

static uint32_t last_slash24 = 0;
static uint8_t e_b[256]; // response rate of each ip in /24
static int e_b_cnt = 0;
static int e_b_sum = 0; // total response rate (for avg)

// overall stats:
static int slash24_cnt = 0;
static int resp_slash24_cnt = 0;
static int usable_slash24_cnt = 0;

static void usage(char *name)
{
  fprintf(stderr,
          "Usage: %s [-s] -f history-file -l netacq-locations-file -b netacq-blocks-file -p pfx2as-file\n"
          "       -s          only dump summary stats\n",
          name);
}

static void cleanup()
{
  free(history_file);
  free(netacq_loc_file);
  free(netacq_blocks_file);
  free(pfx2as_file);

  kh_destroy(strset, keyset);

  ipmeta_record_set_free(&records);
  ipmeta_free(ipmeta);
}

static void dump_slash24_info()
{
  khiter_t k;

  slash24_cnt++;

  // does this /24 have |E(b)| >= 15?
  if (e_b_cnt < MIN_SLASH24_RESP_CNT) {
    return;
  }
  resp_slash24_cnt++;

  // compute average response rate for the /24
  double avg = e_b_sum / 4.0 / e_b_cnt;

  // does this /24 have A(E(b)) >= 0.1?
  if (avg < MIN_SLASH24_AVG_RESP_RATE) {
    return;
  }
  usable_slash24_cnt++;

  if (summary_only != 0) {
    return;
  }

  // print the /24 stats:
  // ## <slash24> <resp_ip_cnt> <avg resp rate>
  fprintf(stdout, "SLASH24 %x %d %f\n", last_slash24, e_b_cnt, avg);
  for (k=kh_begin(keyset); k < kh_end(keyset); k++) {
    if (kh_exist(keyset, k)) {
      fprintf(stdout, "SLASH24_META %s\n", kh_key(keyset, k));
    }
  }

  // print each ip address
  int last_octet;
  for (last_octet = 0; last_octet < 256; last_octet++) {
    if (e_b[last_octet] == UNSET) {
      // this ip was never responsive
      continue;
    }
    fprintf(stdout, "%x %f\n",
            last_slash24 | last_octet,
            e_b[last_octet] / 4.0);
  }
}

static int lookup_metadata()
{
  char buf[1024];
  char *cpy;
  ipmeta_record_t *rec;
  uint32_t num_ips;
  int khret;

  // clear the metadata set
  kh_free(strset, keyset, (void (*)(kh_cstr_t))free);
  kh_clear(strset, keyset);

  // lookup the netacq records, and add each continent and country to the set of
  // metadata for this /24
  ipmeta_lookup(netacq_provider, htonl(last_slash24), 24, records);
  ipmeta_record_set_rewind(records);
  while ((rec = ipmeta_record_set_next(records, &num_ips)) != NULL) {
    // build continent metric
    snprintf(buf, 1024, NETACQ_METRIC_PREFIX".%s", rec->continent_code);
    cpy = strdup(buf);
    assert(cpy != NULL);
    kh_put(strset, keyset, cpy, &khret);

    // build country metric
    snprintf(buf, 1024, NETACQ_METRIC_PREFIX".%s.%s",
             rec->continent_code, rec->country_code);
    cpy = strdup(buf);
    assert(cpy != NULL);
    kh_put(strset, keyset, cpy, &khret);
  }

  // now perform ASN lookups
  ipmeta_lookup(pfx2as_provider, htonl(last_slash24), 24, records);
  ipmeta_record_set_rewind(records);
  while ((rec = ipmeta_record_set_next(records, &num_ips)) != NULL) {
    if (rec->asn_cnt != 1) {
      continue;
    }

    // build asn metric
    snprintf(buf, 1024, PFX2AS_METRIC_PREFIX".%"PRIu32, rec->asn[0]);
    cpy = strdup(buf);
    assert(cpy != NULL);
    kh_put(strset, keyset, cpy, &khret);
  }

  return 0;
}

static int process_history_line(char *line)
{
  char *linep = line;
  char *cell;
  int col = 0;

  uint32_t ip; // in host byte order
  uint32_t history;

  uint32_t slash24;

  while ((cell = strsep(&linep, "\t")) != NULL) {
    switch (col) {
    case 0:
      // hex IP address
      ip = strtoul(cell, NULL, 16);
      break;

    case 1:
      // string IP (ignored)
      break;

    case 2:
      // hex history
      history = strtoul(cell, NULL, 16);
      break;

    case 3:
      // score (ignored)
      break;

    default:
      fprintf(stderr, "ERROR: Unknown column in history file: %s\n", line);
      return -1;
    }
    col++;
  }
  if (col != 4) {
    fprintf(stderr, "ERROR: Too few columns in history file: %s\n", line);
    return -1;
  }

  // process info here
  slash24 = ip & TRINARKULAR_SLASH24_NETMASK;

  if (last_slash24 == 0) {
    // init
    last_slash24 = slash24;
    if (lookup_metadata() != 0) {
      return -1;
    }
  }

  if (slash24 < last_slash24) {
    fprintf(stderr, "ERROR: History file must be sorted by IP\n");
    return -1;
  }
  if (slash24 > last_slash24) {
    // dump info
    dump_slash24_info();
    last_slash24 = slash24;
    if (lookup_metadata() != 0) {
      return -1;
    }
    memset(e_b, UNSET, sizeof(uint8_t) * 256);
    e_b_cnt = 0;
    e_b_sum = 0;
  }

  // we only care about addresses that have ever responded
  // ... probably redundant
  if (history == 0) {
    return 0;
  }

  uint8_t resp_cnt = __builtin_popcount(history & 0xf);
  assert(e_b[ip & 0xff] == UNSET);
  e_b[ip & 0xff] = resp_cnt;
  e_b_sum += resp_cnt;
  e_b_cnt++;

  return 0;
}

int main(int argc, char **argv)
{
  int opt;
  int prevoptind;

  char buffer[1024];

  // init set
  keyset = kh_init(strset);
  assert(keyset != NULL);

  // init ipmeta
  ipmeta = ipmeta_init();
  assert(ipmeta != NULL);
  records = ipmeta_record_set_init();
  assert(records != NULL);

  /* init all to unused */
  memset(e_b, UNSET, sizeof(uint8_t) * 256);

  while (prevoptind = optind,
         (opt = getopt(argc, argv, ":b:f:l:p:s?")) >= 0) {
    if (optind == prevoptind + 2 &&
        optarg && *optarg == '-' && *(optarg+1) != '\0') {
      opt = ':';
      -- optind;
    }
    switch (opt) {
    case 'b':
      netacq_blocks_file = strdup(optarg);
      assert(netacq_blocks_file != NULL);
      break;

    case 'f':
      history_file = strdup(optarg);
      assert(history_file != NULL);
      break;

    case 'l':
      netacq_loc_file = strdup(optarg);
      assert(netacq_loc_file != NULL);
      break;

    case 'p':
      pfx2as_file = strdup(optarg);
      assert(pfx2as_file != NULL);
      break;

    case 's':
      summary_only = 1;
      break;

    case ':':
      fprintf(stderr, "ERROR: Missing option argument for -%c\n", optopt);
      usage(argv[0]);
      return -1;
      break;

    case '?':
    case 'v':
      fprintf(stderr, "trinarkular version %d.%d.%d\n",
              TRINARKULAR_MAJOR_VERSION,
              TRINARKULAR_MID_VERSION,
              TRINARKULAR_MINOR_VERSION);
      usage(argv[0]);
      cleanup();
      return -1;
      break;

    default:
      usage(argv[0]);
      cleanup();
      return -1;
    }
  }

  if (history_file == NULL) {
    fprintf(stderr, "ERROR: History file must be specified using -f\n");
    usage(argv[0]);
    cleanup();
    return -1;
  }

  if (netacq_blocks_file == NULL) {
    fprintf(stderr, "ERROR: Netacq blocks file must be specified using -b\n");
    usage(argv[0]);
    cleanup();
    return -1;
  }

  if (netacq_loc_file == NULL) {
    fprintf(stderr, "ERROR: Netacq locations file must be specified using -l\n");
    usage(argv[0]);
    cleanup();
    return -1;
  }

  if (pfx2as_file == NULL) {
    fprintf(stderr, "ERROR: Pfx2AS file must be specified using -p\n");
    usage(argv[0]);
    cleanup();
    return -1;
  }

  if ((infile = wandio_create(history_file)) == NULL) {
    fprintf(stderr, "ERROR: Could not open %s for reading\n", history_file);
    cleanup();
    return -1;
  }

  // init the netacq provider
  netacq_provider =
    ipmeta_get_provider_by_id(ipmeta, IPMETA_PROVIDER_NETACQ_EDGE);
  if (netacq_provider == NULL) {
    fprintf(stderr, "ERROR: Could not find net acuity provider. "
            "Is libipmeta built with net acuity support?\n");
    cleanup();
    return -1;
  }
  snprintf(buffer, 1024, "-l %s -b %s -D intervaltree", netacq_loc_file, netacq_blocks_file);
  if(ipmeta_enable_provider(ipmeta, netacq_provider,
                            buffer, IPMETA_PROVIDER_DEFAULT_NO) != 0) {
    fprintf(stderr, "ERROR: Could not enable net acuity provider\n");
    usage(argv[0]);
    cleanup();
    return -1;
  }

  // init the pfx2as provider
  pfx2as_provider =
    ipmeta_get_provider_by_id(ipmeta, IPMETA_PROVIDER_PFX2AS);
  if (pfx2as_provider == NULL) {
    fprintf(stderr, "ERROR: Could not find pfx2as provider. "
            "Is libipmeta built with pfx2as support?\n");
    cleanup();
    return -1;
  }
  snprintf(buffer, 1024, "-f %s -D intervaltree", pfx2as_file);
  if(ipmeta_enable_provider(ipmeta, pfx2as_provider,
                            buffer, IPMETA_PROVIDER_DEFAULT_NO) != 0) {
    fprintf(stderr, "ERROR: Could not enable pfx2as provider\n");
    usage(argv[0]);
    cleanup();
    return -1;
  }

  while (wandio_fgets(infile, buffer, 1024, 1) != 0) {
    if (buffer[0] == '#') {
      continue;
    }
    if (process_history_line(buffer) != 0) {
      cleanup();
      return -1;
    }
  }

  // dump the final /24
  dump_slash24_info();

  fprintf(stderr, "Overall Stats:\n");
  fprintf(stderr, "\t# /24s:\t%d\n", slash24_cnt);
  fprintf(stderr, "\t# Responsive /24s:\t%d\n", resp_slash24_cnt);
  fprintf(stderr, "\t# Usable /24s:\t%d\n", usable_slash24_cnt);

  cleanup();
  return 0;
}
