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
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wandio_utils.h"
#include "utils.h"

#include "trinarkular.h"
#include "trinarkular_log.h"

static trinarkular_probelist_t *pl = NULL;
static trinarkular_prober_t *prober = NULL;

/** Indicates that the prober is waiting to shutdown */
volatile sig_atomic_t prober_shutdown = 0;

/** The number of SIGINTs to catch before aborting */
#define HARD_SHUTDOWN 3

/** Handles SIGINT gracefully and shuts down */
static void catch_sigint(int sig)
{
  prober_shutdown++;
  if(prober_shutdown == HARD_SHUTDOWN)
    {
      fprintf(stderr, "caught %d SIGINT's. shutting down NOW\n",
	      HARD_SHUTDOWN);
      exit(-1);
    }

  fprintf(stderr, "caught SIGINT, shutting down at the next opportunity\n");

  if(prober != NULL)
    {
      trinarkular_prober_stop(prober);
    }

  signal(sig, catch_sigint);
}

static void usage(char *name)
{
  fprintf(stderr,
          "Usage: %s [options] probelist\n"
          "       -c <probecount>  periodic max number of probes to send per /24 (default: %d)\n"
          "       -d <duration>    periodic probing round duration in msec (default: %d)\n"
          "       -i <timeout>     periodic probing probe timeout in msec (default: %d)\n"
          "       -l <rounds>      periodic probing round limit (default: unlimited)\n"
          "       -p <driver>      probe driver to use (default: %s %s)\n"
          "       -r <seed>        random number generator seed (default: NOW)\n"
          "       -s <slices>      periodic probing round slices (default: %d)\n",
          name,
          TRINARKULAR_PROBER_PERIODIC_MAX_PROBECOUNT_DEFAULT,
          TRINARKULAR_PROBER_PERIODIC_ROUND_DURATION_DEFAULT,
          TRINARKULAR_PROBER_PERIODIC_PROBE_TIMEOUT_DEFAULT,
          TRINARKULAR_PROBER_DRIVER_DEFAULT,
          TRINARKULAR_PROBER_DRIVER_ARGS_DEFAULT,
          TRINARKULAR_PROBER_PERIODIC_ROUND_SLICES_DEFAULT);
}

static void cleanup()
{
  trinarkular_probelist_destroy(pl);
  pl = NULL;

  trinarkular_prober_destroy(prober);
  prober = NULL;
}

int main(int argc, char **argv)
{
  int opt, prevoptind;

  char *probelist_file;

  int probecount;
  int probecount_set = 0;

  uint64_t duration;
  int duration_set = 0;

  uint32_t wait;
  int wait_set = 0;

  int round_limit;
  int round_limit_set = 0;

  int slices;
  int slices_set = 0;

  int random_seed;
  int random_seed_set = 0;

  signal(SIGINT, catch_sigint);

  while(prevoptind = optind,
	(opt = getopt(argc, argv, ":c:d:i:l:r:s:v?")) >= 0)
    {
      if (optind == prevoptind + 2 &&
          optarg && *optarg == '-' && *(optarg+1) != '\0') {
        opt = ':';
        -- optind;
      }
      switch(opt)
	{
        case 'c':
          probecount = strtoul(optarg, NULL, 10);
          if (probecount > UINT8_MAX) {
            fprintf(stderr, "ERROR: max probe count muse be < 256\n");
            usage(argv[0]);
            return -1;
          }
          probecount_set = 1;
          break;

	case 'd':
          duration = strtoull(optarg, NULL, 10);
          duration_set = 1;
          break;

        case 'i':
          wait = strtoul(optarg, NULL, 10);
          wait_set = 1;
          break;

        case 'l':
          round_limit = strtol(optarg, NULL, 10);
          round_limit_set = 1;
          break;

        case 's':
          slices = strtol(optarg, NULL, 10);
          slices_set = 1;
          break;

        case 'r':
          random_seed = strtol(optarg, NULL, 10);
          random_seed_set = 1;
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
	  goto err;
	  break;

	default:
	  usage(argv[0]);
	  goto err;
	}
    }

  if (optind >= argc) {
    fprintf(stderr, "ERROR: Probelist file must be specifed\n");
    usage(argv[0]);
    goto err;
  }

  probelist_file = argv[optind];

  /* reset getopt for drivers to use */
  optind = 1;


  if ((prober = trinarkular_prober_create()) == NULL) {
    goto err;
  }

  if (probecount_set != 0) {
    trinarkular_prober_set_periodic_max_probecount(prober, probecount);
  }

  if (duration_set != 0) {
    trinarkular_prober_set_periodic_round_duration(prober, duration);
  }

  if (wait_set != 0) {
    trinarkular_prober_set_periodic_probe_timeout(prober, wait);
  }

  if (round_limit_set != 0) {
    trinarkular_prober_set_periodic_round_limit(prober, round_limit);
  }

  if (slices_set != 0) {
    trinarkular_prober_set_periodic_round_slices(prober, slices);
  }

  if (random_seed_set != 0) {
    trinarkular_prober_set_random_seed(prober, random_seed);
  }

  if ((pl = trinarkular_probelist_create_from_file(probelist_file)) == NULL) {
    goto err;
  }

  trinarkular_prober_assign_probelist(prober, pl);
  // prober now owns pl
  pl = NULL;

  // this will block indefinitely
  if (trinarkular_prober_start(prober) != 0) {
    goto err;
  }

  cleanup();
  return 0;

 err:
  cleanup();
  return -1;
}
