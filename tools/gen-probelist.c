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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wandio.h>

#include "wandio_utils.h"
#include "utils.h"

// the min number of ips that must respond before a /24 is considered
#define MIN_SLASH24_RESP_CNT 15

// the min avg response rate for a /24
#define MIN_SLASH24_AVG_RESP_RATE 0.1

#define UNSET 255

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
          "Usage: %s [-s] -f history-file\n"
          "       -s          only dump summary stats\n",
          name);
}

static void cleanup()
{
  free(history_file);
}

static void dump_slash24_info()
{
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
  fprintf(stdout, "## %x %d %f\n", last_slash24, e_b_cnt, avg);

  // print each ip address
  int last_octet;
  for (last_octet = 0; last_octet < 256; last_octet++) {
    if (e_b[last_octet] == UNSET) {
      // this ip was never responsive
      continue;
    }
    fprintf(stdout, "%x %f\n",
            (last_slash24 << 8) | last_octet,
            e_b[last_octet] / 4.0);
  }
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
  slash24 = ip >> 8;

  if (last_slash24 == 0) {
    // init
    last_slash24 = slash24;
  }

  if (slash24 < last_slash24) {
    fprintf(stderr, "ERROR: History file must be sorted by IP\n");
    return -1;
  }
  if (slash24 > last_slash24) {
    // dump info
    dump_slash24_info();
    last_slash24 = slash24;
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

  /* init all to unused */
  memset(e_b, UNSET, sizeof(uint8_t) * 256);

  while (prevoptind = optind,
         (opt = getopt(argc, argv, ":f:s?")) >= 0) {
    if (optind == prevoptind + 2 &&
        optarg && *optarg == '-' && *(optarg+1) != '\0') {
      opt = ':';
      -- optind;
    }
    switch (opt) {
    case 'f':
      history_file = strdup(optarg);
      if (history_file == NULL) {
        fprintf(stderr, "ERROR: Could not duplicate history filename\n");
        cleanup();
        return -1;
      }
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

  if ((infile = wandio_create(history_file)) == NULL) {
    fprintf(stderr, "ERROR: Could not open %s for reading\n", history_file);
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
