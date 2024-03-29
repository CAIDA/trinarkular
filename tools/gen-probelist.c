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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wandio.h>

#include <libipmeta.h>

#include "khash.h"
#include "utils.h"
#include "wandio_utils.h"

#include "trinarkular.h"

// the min number of ips that must respond before a /24 is considered
#define MIN_SLASH24_RESP_CNT 15

// the min avg response rate for a /24
#define MIN_SLASH24_AVG_RESP_RATE 0.1

#define UNSET 255

#define NETACQ_METRIC_PREFIX "geo.netacuity"
#define PFX2AS_METRIC_PREFIX "asn"

#define VERSION_PATTERN 'V'
#define VERSION_PATTERN_STR "%V"
#define PROBER_PATTERN 'P'
#define PROBER_PATTERN_STR "%P"

#define DEFAULT_COMPRESS_LEVEL 6

KHASH_SET_INIT_STR(strset);
static khash_t(strset) *keyset = NULL;

// output file pattern
static char *outfile_pattern = NULL;

// probers to split probelist amongst
static char **prober_names = NULL;
static int prober_names_cnt = 0;
static char *probers_file = NULL;

static int prober_cnt = 0;

// list of output files
static iow_t **outfiles = NULL;
static int outfiles_cnt = 0; // at most one per prober
static int outfiles_idx = 0; // round robin
static int objects_written = 0;

// ipmeta instance
#define NETACQ_EDGE_POLYS_TBL_CNT 2
static ipmeta_t *ipmeta = NULL;
static ipmeta_record_set_t *records = NULL;
static ipmeta_provider_t *netacq_provider;
static ipmeta_provider_t *pfx2as_provider;
static ipmeta_polygon_table_t **poly_tbls = NULL;
static int poly_tbls_cnt = 0;
static uint32_t *poly_id_to_tbl_idx[NETACQ_EDGE_POLYS_TBL_CNT];
static uint32_t poly_id_to_tbl_idx_cnts[NETACQ_EDGE_POLYS_TBL_CNT];

static char *netacq_config_str = NULL;
static char *pfx2as_file = NULL;

// probelist version (date)
static char *version = NULL;

// metadata filter strings
static khash_t(strset) *meta_filters = NULL;
// should the current /24 be skipped
static int skip = 0;

/* should only summary stats be dumped? */
static int summary_only = 0;
static char *history_file = NULL;
static io_t *infile = NULL;

KHASH_INIT(u32, uint32_t, char, 0, kh_int_hash_func, kh_int_hash_equal);
static khash_t(u32) *blacklist_set = NULL;

static uint32_t last_slash24 = 0;
static uint8_t e_b[256]; // response rate of each ip in /24
static int e_b_cnt = 0;
static int e_b_sum = 0; // total response rate (for avg)

// overall stats:
static int slash24_cnt = 0;
static int resp_slash24_cnt = 0;
static int usable_slash24_cnt = 0;

// limit the number of /24s we output?
static int max_slash24_cnt = 0;

static int add_blacklist(const char *slash24_str)
{
  uint32_t slash24;
  // convert string to integer
  if (inet_pton(AF_INET, slash24_str, &slash24) != 1) {
    return -1;
  }
  slash24 = ntohl(slash24);
  int khret;
  kh_put(u32, blacklist_set, slash24, &khret);
  if (khret < 0) {
    return -1;
  }
  fprintf(stderr, "INFO: added %s (%x) to the blacklist\n",
          slash24_str, slash24);
  return 0;
}

static int is_blacklisted(uint32_t slash24)
{
  if (kh_get(u32, blacklist_set, slash24) != kh_end(blacklist_set)) {
    return 1;
  }
  return 0;
}

static int add_prober(const char *prober)
{
  // realloc the array
  if ((prober_names = realloc(
         prober_names, sizeof(char *) * (prober_names_cnt + 1))) == NULL) {
    return -1;
  }

  if ((prober_names[prober_names_cnt++] = strdup(prober)) == NULL) {
    return -1;
  }

  return 0;
}

static char *stradd(const char *str, char *bufp, char *buflim)
{
  while (bufp < buflim && (*bufp = *str++) != '\0') {
    ++bufp;
  }
  return bufp;
}

static char *generate_file_name(char *buf, size_t buflen, const char *template,
                                const char *version, const char *prober)
{
  /* some of the structure of this code is borrowed from the
     FreeBSD implementation of strftime */

  char *bufp = buf;
  char *buflim = buf + buflen;

  char *tmpl = (char *)template;

  for (; *tmpl; ++tmpl) {
    if (*tmpl == '%') {
      switch (*++tmpl) {
      case '\0':
        --tmpl;
        break;

      case VERSION_PATTERN:
        bufp = stradd(version, bufp, buflim);
        continue;

      case PROBER_PATTERN:
        bufp = stradd(prober, bufp, buflim);
        continue;

      default:
        /* we want to be generous and leave non-recognized formats
           intact - especially for strftime to use */
        --tmpl;
      }
    }
    if (bufp == buflim)
      break;
    *bufp++ = *tmpl;
  }

  *bufp = '\0';

  return 0;
  ;
}

static void usage(char *name)
{
  fprintf(
    stderr,
    "Usage: %s [-s] -bdflp\n"
    "       -b <blacklist>   file with /24s to blacklist\n"
    "       -c <count>       max number of /24s to output\n"
    "       -d <SERIAL>      version of the probelist (required)\n"
    "       -f <file>        history file (required)\n"
    "       -g <file>        net acuity config string (required)\n"
    "       -x <file>        prefix2as file (required)\n"
    "       -m <meta>        output only /24s with given meta *\n"
    "       -o <pattern>     output file pattern. supports the following:\n"
    "                          '%" PROBER_PATTERN_STR "' => prober name\n"
    "                          '%" VERSION_PATTERN_STR
    "' => probelist version\n"
    "       -p <prober>      prober to assign /24s to *\n"
    "       -P <file>        list of probers to assign /24s to\n"
    "       -n <prober-cnt>  number of probers to assign /24s to\n"
    "                          if this is larger than the number of prober "
    "names,\n"
    "                          unnamed probers will be numbered\n"
    "       -s               only dump summary stats\n"
    " (* denotes an option that may be given multiple times)\n",
    name);
}

static void cleanup()
{
  int i;

  free(version);
  version = NULL;

  free(history_file);
  history_file = NULL;

  free(netacq_config_str);
  netacq_config_str = NULL;

  free(pfx2as_file);
  pfx2as_file = NULL;

  free(outfile_pattern);
  outfile_pattern = NULL;

  if (keyset != NULL) {
    kh_free(strset, keyset, (void (*)(kh_cstr_t))free);
    kh_destroy(strset, keyset);
    keyset = NULL;
  }

  if (blacklist_set != NULL) {
    kh_destroy(u32, blacklist_set);
    blacklist_set = NULL;
  }

  ipmeta_record_set_free(&records);
  ipmeta_free(ipmeta);
  ipmeta = NULL;

  for (i = 0; i < poly_tbls_cnt; i++) {
    free(poly_id_to_tbl_idx[i]);
    poly_id_to_tbl_idx[i] = NULL;
  }
  poly_id_to_tbl_idx_cnts[i] = 0;

  if (meta_filters != NULL) {
    kh_free(strset, meta_filters, (void (*)(kh_cstr_t))free);
    kh_destroy(strset, meta_filters);
    meta_filters = NULL;
  }

  for (i = 0; i < prober_names_cnt; i++) {
    free(prober_names[i]);
    prober_names[i] = NULL;
  }
  free(prober_names);
  prober_names = NULL;
  prober_names_cnt = 0;

  free(probers_file);
  probers_file = NULL;

  for (i = 0; i < outfiles_cnt; i++) {
    if (outfiles[i] != NULL) {
      wandio_wdestroy(outfiles[i]);
      outfiles[i] = NULL;
    }
  }
  free(outfiles);
  outfiles = NULL;
  outfiles_cnt = 0;
}

static void dump_slash24_info()
{
  khiter_t k;
  int comma = 0;
  char ip_str[INET_ADDRSTRLEN];
  uint32_t tmp;
  iow_t *outfile = outfiles[outfiles_idx];

  slash24_cnt++;

  if ((slash24_cnt % 100000) == 0) {
    fprintf(stderr, "INFO: %d /24s processed\n", slash24_cnt);
  }

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

  // move on to the next outfile (for the next dump, not this one)
  outfiles_idx = (outfiles_idx + 1) % outfiles_cnt;

  // add a comma for the previous JSON object if this is not the first
  if (objects_written >= outfiles_cnt) {
    wandio_printf(outfile, ",\n");
  }
  objects_written++;

  // convert the /24 to a string
  tmp = htonl(last_slash24);
  inet_ntop(AF_INET, &tmp, ip_str, INET_ADDRSTRLEN);

  // start the JSON object
  // and print the /24 stats:
  wandio_printf(outfile, "  \"%s/24\": {\n"
                         "    \"version\": \"%s\",\n"
                         "    \"host_cnt\": %d,\n"
                         "    \"avg_resp_rate\": %f,\n",
                ip_str, version, e_b_cnt, avg);

  wandio_printf(outfile, "    \"meta\": [\n");
  for (k = kh_begin(keyset); k < kh_end(keyset); k++) {
    if (!kh_exist(keyset, k)) {
      continue;
    }
    if (comma != 0) {
      wandio_printf(outfile, ",\n");
    } else {
      comma = 1;
    }
    wandio_printf(outfile, "      \"%s\"", kh_key(keyset, k));
  }
  wandio_printf(outfile,
                "\n" // end the last meta line
                "    ],\n"
                "    \"hosts\": [\n");
  // print each ip address
  comma = 0;
  int last_octet;
  for (last_octet = 0; last_octet < 256; last_octet++) {
    if (e_b[last_octet] == UNSET) {
      // this ip was never responsive
      continue;
    }

    // convert the host to a string
    tmp = htonl((last_slash24 | last_octet));
    inet_ntop(AF_INET, &tmp, ip_str, INET_ADDRSTRLEN);

    if (comma != 0) {
      wandio_printf(outfile, ",\n");
    } else {
      comma = 1;
    }
    wandio_printf(outfile, "      {"
                           " \"host_ip\": \"%s\","
                           " \"e_b\": %f"
                           " }",
                  ip_str, e_b[last_octet] / 4.0);
  }
  wandio_printf(outfile,
                "\n" // end the last host line
                "    ]\n"
                "  }"); // end the JSON object
}

#define METRIC_CREATE(format, leafpfx, pfx, metric...)                  \
  do {                                                                  \
    snprintf(buf, 1024, "%s:"pfx"."format, leafpfx, metric); \
    cpy = strdup(buf);                                                  \
    assert(cpy != NULL);                                                \
                                                                        \
    if (kh_size(meta_filters) == 0 ||                                   \
        kh_get(strset, meta_filters, cpy) != kh_end(meta_filters)) {    \
      matches_filter = 1;                                               \
    }                                                                   \
                                                                        \
    kh_put(strset, keyset, cpy, &khret);                                \
    if (khret == 0) {                                                   \
      /* key was already present */                                     \
      free(cpy);                                                        \
      cpy = NULL;                                                       \
    }                                                                   \
  } while (0)

static int lookup_metadata()
{
  char buf[1024];
  char *cpy;
  ipmeta_record_t *rec;
  uint32_t num_ips;
  int khret;

  int matches_filter = 0;
  int poly_table = 0;

  // clear the metadata set
  kh_free(strset, keyset, (void (*)(kh_cstr_t))free);
  kh_clear(strset, keyset);

  // lookup the netacq records, and add each continent, country, and polygons to
  // the set of metadata for this /24
  ipmeta_lookup(netacq_provider, htonl(last_slash24), 24, records);
  ipmeta_record_set_rewind(records);
  while ((rec = ipmeta_record_set_next(records, &num_ips)) != NULL) {
    // build continent metric (not a leaf)
    METRIC_CREATE("%s", "N", NETACQ_METRIC_PREFIX, rec->continent_code);

    // build country metric (not a leaf)
    METRIC_CREATE("%s.%s", "N", NETACQ_METRIC_PREFIX,
                  rec->continent_code, rec->country_code);

    // build polygon metrics (could be a leaf if non-US or county)
    for (poly_table = 0; poly_table < rec->polygon_ids_cnt; poly_table++) {
      uint32_t poly_id = rec->polygon_ids[poly_table];
      uint32_t tbl_idx = poly_id_to_tbl_idx[poly_table][poly_id];
      assert(poly_id < poly_id_to_tbl_idx_cnts[poly_table]);

      METRIC_CREATE("%s", "N", NETACQ_METRIC_PREFIX,
                    poly_tbls[poly_table]->polygons[tbl_idx]->fqid);
    }
  }

  // now perform ASN lookups
  ipmeta_lookup(pfx2as_provider, htonl(last_slash24), 24, records);
  ipmeta_record_set_rewind(records);
  while ((rec = ipmeta_record_set_next(records, &num_ips)) != NULL) {
    if (rec->asn_cnt != 1) {
      continue;
    }

    // build asn metric
    METRIC_CREATE("%"PRIu32, "L", PFX2AS_METRIC_PREFIX, rec->asn[0]);
  }

  return !matches_filter;
}

static int process_history_line(char *line)
{
  char *linep = line;
  char *cell;
  int col = 0;

  uint32_t ip = 0; // in host byte order

  // only consider most recent 16 measurements as per sec 4.4:
  uint16_t history = 0;

  uint32_t slash24 = 0;

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
    if ((skip = lookup_metadata()) < 0) {
      return -1;
    }
    if (skip == 0 && is_blacklisted(slash24) != 0) {
      fprintf(stderr, "INFO: Skipping %x (blacklisted)\n", slash24);
      skip = 1;
    }
  }

  if (slash24 < last_slash24) {
    fprintf(stderr, "ERROR: History file must be sorted by IP\n");
    return -1;
  }
  if (slash24 > last_slash24) {
    // dump info
    if (skip == 0) {
      dump_slash24_info();
    }
    last_slash24 = slash24;
    if ((skip = lookup_metadata()) < 0) {
      return -1;
    }
    if (skip == 0 && is_blacklisted(slash24) != 0) {
      fprintf(stderr, "INFO: Skipping %x (blacklisted)\n", slash24);
      skip = 1;
    }
    memset(e_b, UNSET, sizeof(uint8_t) * 256);
    e_b_cnt = 0;
    e_b_sum = 0;
  }

  // we only care about addresses that have ever responded
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
  char buffer2[1024];
  char *prober_name = NULL;
  int i, j;
  int outfile_idx;

  char *blacklist_file = NULL;

  // init sets
  keyset = kh_init(strset);
  assert(keyset != NULL);
  meta_filters = kh_init(strset);
  assert(meta_filters != NULL);
  blacklist_set = kh_init(u32);
  assert(blacklist_set != NULL);

  // init ipmeta
  ipmeta = ipmeta_init();
  assert(ipmeta != NULL);
  records = ipmeta_record_set_init();
  assert(records != NULL);

  /* init all to unused */
  memset(e_b, UNSET, sizeof(uint8_t) * 256);

  while (prevoptind = optind,
         (opt = getopt(argc, argv, ":b:c:d:f:g:m:n:o:p:P:s:x:v?")) >= 0) {
    if (optind == prevoptind + 2 && optarg && *optarg == '-' &&
        *(optarg + 1) != '\0') {
      opt = ':';
      --optind;
    }
    switch (opt) {
    case 'b':
      blacklist_file = optarg;
      break;

    case 'c':
      max_slash24_cnt = atoi(optarg);
      break;

    case 'd':
      version = strdup(optarg);
      assert(version != NULL);
      break;

    case 'f':
      history_file = strdup(optarg);
      assert(history_file != NULL);
      break;

    case 'g':
      netacq_config_str = strdup(optarg);
      assert(netacq_config_str != NULL);
      break;

    case 'm':
      kh_put(strset, meta_filters, strdup(optarg), &i);
      break;

    case 'n':
      prober_cnt = atoi(optarg);
      break;

    case 'o':
      outfile_pattern = strdup(optarg);
      assert(outfile_pattern != NULL);
      break;

    case 'p':
      if (add_prober(optarg) != 0) {
        fprintf(stderr, "ERROR: Could not add prober %s\n", optarg);
        usage(argv[0]);
        goto err;
      }
      break;

    case 'P':
      probers_file = strdup(optarg);
      assert(probers_file != NULL);
      break;

    case 's':
      summary_only = 1;
      break;

    case 'x':
      pfx2as_file = strdup(optarg);
      assert(pfx2as_file != NULL);
      break;

    case ':':
      fprintf(stderr, "ERROR: Missing option argument for -%c\n", optopt);
      usage(argv[0]);
      return -1;
      break;

    case '?':
    case 'v':
      fprintf(stderr, "trinarkular version %d.%d.%d\n",
              TRINARKULAR_MAJOR_VERSION, TRINARKULAR_MID_VERSION,
              TRINARKULAR_MINOR_VERSION);
      usage(argv[0]);
      goto err;
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
    goto err;
  }

  if (version == NULL) {
    fprintf(stderr, "ERROR: Version must be specified using -d\n");
    usage(argv[0]);
    goto err;
  }

  if (netacq_config_str == NULL) {
    fprintf(stderr, "ERROR: Netacq config string must be specified using -g\n");
    usage(argv[0]);
    goto err;
  }

  if (pfx2as_file == NULL) {
    fprintf(stderr, "ERROR: Pfx2AS file must be specified using -x\n");
    usage(argv[0]);
    goto err;
  }

  // read the blacklist file if there is one
  if (blacklist_file != NULL) {
    assert(infile == NULL);
    if ((infile = wandio_create(blacklist_file)) == NULL) {
      fprintf(stderr, "ERROR: Could not open %s for reading\n", blacklist_file);
      goto err;
    }

    while (wandio_fgets(infile, buffer, 1024, 1) != 0) {
      if (buffer[0] == '#') {
        continue;
      }
      if (add_blacklist(buffer) != 0) {
        fprintf(stderr, "ERROR: Failed to add /24 to blacklist '%s'\n", buffer);
        goto err;
      }
    }

    wandio_destroy(infile);
    infile = NULL;
  }

  // read the probers file if there is one
  if (probers_file != NULL) {
    assert(infile == NULL);
    if ((infile = wandio_create(probers_file)) == NULL) {
      fprintf(stderr, "ERROR: Could not open %s for reading\n", probers_file);
      goto err;
    }

    while (wandio_fgets(infile, buffer, 1024, 1) != 0) {
      if (buffer[0] == '#') {
        continue;
      }
      if (add_prober(buffer) != 0) {
        fprintf(stderr, "ERROR: Failed to add prober '%s'\n", buffer);
        goto err;
      }
    }

    wandio_destroy(infile);
    infile = NULL;
  }

  if (prober_cnt > 0 && prober_names_cnt > prober_cnt) {
    fprintf(stderr, "WARN: %d probers requested, but %d names given. "
                    "Splitting across %d probers\n",
            prober_cnt, prober_names_cnt, prober_names_cnt);
  }

  // default to a single prober
  if (prober_cnt == 0) {
    prober_cnt = 1;
  }

  // if they didnt specify a number of probers, then we default to the number of
  // names they asked for
  if (prober_names_cnt > prober_cnt) {
    prober_cnt = prober_names_cnt;
  }

  if (prober_cnt > prober_names_cnt) {
    fprintf(stderr, "WARN: %d probers requested but %d names given. "
                    "Some output files will be numbered\n",
            prober_cnt, prober_names_cnt);
  }

  // if they asked for multiple probers, we cant send that to stdout!
  if (prober_cnt > 1 && outfile_pattern == NULL) {
    fprintf(stderr, "ERROR: Cannot output multiple probers to stdout. "
                    "Use -o instead\n");
    goto err;
  }

  // malloc the outfile array (one for each prober. some may be number not
  // named)
  if ((outfiles = malloc(sizeof(iow_t *) * prober_cnt)) == NULL) {
    fprintf(stderr, "ERROR: Could not malloc outfile array\n");
    goto err;
  }
  outfiles_cnt = prober_cnt;

  if (outfile_pattern == NULL) {
    assert(prober_cnt == 1);
    // open a single stdout file
    if ((outfiles[0] = wandio_wcreate("-", WANDIO_COMPRESS_NONE, 0, 0)) ==
        NULL) {
      fprintf(stderr, "ERROR: Could not open stdout for writing\n");
      goto err;
    }

    // start the JSON object
    wandio_printf(outfiles[0], "{\n");
  } else {
    // validate the outfile pattern

    // if there are multiple probers, there must be %P in the string
    if (prober_cnt > 1 &&
        (strstr(outfile_pattern, PROBER_PATTERN_STR) == NULL)) {
      fprintf(stderr, "ERROR: %d probers requested, but outfile pattern is "
                      "missing %" PROBER_PATTERN_STR "\n",
              prober_cnt);
      usage(argv[0]);
      goto err;
    }

    // open the output files
    for (i = 0; i < outfiles_cnt; i++) {

      if (i >= prober_names_cnt) {
        // this is an unnamed prober
        snprintf(buffer2, 1024, "%d", i + 1);
        prober_name = buffer2;
      } else {
        prober_name = prober_names[i];
      }

      if (generate_file_name(buffer, 1024, outfile_pattern, version,
                             prober_name) != 0) {
        fprintf(stderr, "ERROR: Could not generate output filename\n");
        goto err;
      }

      fprintf(stderr, "INFO: Opening output file %s\n", buffer);

      // open file
      if ((outfiles[i] =
             wandio_wcreate(buffer, wandio_detect_compression_type(buffer),
                            DEFAULT_COMPRESS_LEVEL, O_CREAT)) == NULL) {
        fprintf(stderr, "ERROR: Could not open %s for writing\n", buffer);
        goto err;
      }

      // start the JSON object
      wandio_printf(outfiles[i], "{\n");
    }
  }

  assert(infile == NULL);
  if ((infile = wandio_create(history_file)) == NULL) {
    fprintf(stderr, "ERROR: Could not open %s for reading\n", history_file);
    goto err;
  }

  // init the netacq provider
  netacq_provider =
    ipmeta_get_provider_by_id(ipmeta, IPMETA_PROVIDER_NETACQ_EDGE);
  if (netacq_provider == NULL) {
    fprintf(stderr, "ERROR: Could not find net acuity provider. "
                    "Is libipmeta built with net acuity support?\n");
    goto err;
  }
  if (ipmeta_enable_provider(ipmeta, netacq_provider, netacq_config_str,
                             IPMETA_PROVIDER_DEFAULT_NO) != 0) {
    fprintf(stderr, "ERROR: Could not enable net acuity provider\n");
    usage(argv[0]);
    goto err;
  }
  // grab a copy of the polygon tables
  poly_tbls_cnt =
    ipmeta_provider_netacq_edge_get_polygon_tables(netacq_provider, &poly_tbls);
  assert(poly_tbls_cnt == NETACQ_EDGE_POLYS_TBL_CNT);
  if (poly_tbls == NULL || poly_tbls_cnt == 0) {
    fprintf(stderr, "ERROR: Net Acuity Edge provider must be used with "
                    "the -p and -t options to load polygon information\n");
    usage(argv[0]);
    goto err;
  }
  // create mapping from poly ID to table index
  ipmeta_polygon_table_t *table = NULL;
  for (i=0; i<poly_tbls_cnt; i++) {
    table = poly_tbls[i];

    poly_id_to_tbl_idx_cnts[i] = 0;

    // sneaky check to find the largest polygon id
    for (j = 0; j < table->polygons_cnt; j++) {
      if (table->polygons[j]->id + 1 > poly_id_to_tbl_idx_cnts[i]) {
        poly_id_to_tbl_idx_cnts[i] = table->polygons[j]->id + 1;
      }
    }

    // allocate a mapping array long enough to hold all polygon IDs
    if ((poly_id_to_tbl_idx[i] =
           malloc_zero(sizeof(uint32_t) * poly_id_to_tbl_idx_cnts[i])) == NULL) {
      goto err;
    }

    for (j = 0; j < table->polygons_cnt; j++) {
      assert(table->polygons[j] != NULL);
      poly_id_to_tbl_idx[i][table->polygons[j]->id] = j;
    }
  }

  // init the pfx2as provider
  pfx2as_provider = ipmeta_get_provider_by_id(ipmeta, IPMETA_PROVIDER_PFX2AS);
  if (pfx2as_provider == NULL) {
    fprintf(stderr, "ERROR: Could not find pfx2as provider. "
                    "Is libipmeta built with pfx2as support?\n");
    goto err;
  }
  snprintf(buffer, 1024, "-f %s -D intervaltree", pfx2as_file);
  if (ipmeta_enable_provider(ipmeta, pfx2as_provider, buffer,
                             IPMETA_PROVIDER_DEFAULT_NO) != 0) {
    fprintf(stderr, "ERROR: Could not enable pfx2as provider\n");
    usage(argv[0]);
    goto err;
  }

  fprintf(stderr, "INFO: Processing /24s...\n");

  // process the history file, round-robining between outfiles
  outfile_idx = 0;
  while (wandio_fgets(infile, buffer, 1024, 1) != 0) {
    if (buffer[0] == '#') {
      continue;
    }
    if (process_history_line(buffer) != 0) {
      fprintf(stderr, "ERROR: Failed to process history line '%s'\n", buffer);
      goto err;
    }
    if (max_slash24_cnt > 0 && resp_slash24_cnt == max_slash24_cnt) {
      fprintf(stderr, "INFO: %d /24s processed, stopping\n", resp_slash24_cnt);
      skip = 1;
      break;
    }
    outfile_idx = (outfile_idx + 1) % outfiles_cnt;
  }

  // dump the final /24
  if (skip == 0) {
    dump_slash24_info();
  }

  // end the JSON objects
  for (i = 0; i < outfiles_cnt; i++) {
    wandio_printf(outfiles[i], "\n}\n");
  }

  fprintf(stderr, "Overall Stats:\n");
  fprintf(stderr, "\t# /24s:\t%d\n", slash24_cnt);
  fprintf(stderr, "\t# Responsive /24s:\t%d\n", resp_slash24_cnt);
  fprintf(stderr, "\t# Usable /24s:\t%d\n", usable_slash24_cnt);

  cleanup();
  return 0;

err:
  cleanup();
  return -1;
}
