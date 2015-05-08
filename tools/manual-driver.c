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

#include "utils.h"

#include "trinarkular.h"
#include "trinarkular_log.h"
#include "trinarkular_driver_factory.h" // not included in trinarkular.h

static trinarkular_driver_t *driver = NULL;

/** Indicates that we are waiting to shutdown */
volatile sig_atomic_t shutdown = 0;

/** The number of SIGINTs to catch before aborting */
#define HARD_SHUTDOWN 3

/** Handles SIGINT gracefully and shuts down */
static void catch_sigint(int sig)
{
  shutdown++;
  if(shutdown == HARD_SHUTDOWN)
    {
      fprintf(stderr, "caught %d SIGINT's. shutting down NOW\n",
	      HARD_SHUTDOWN);
      exit(-1);
    }

  fprintf(stderr, "caught SIGINT, shutting down at the next opportunity\n");

  if(driver != NULL)
    {
      driver->destroy(driver);
    }

  signal(sig, catch_sigint);
}

static void usage(char *name)
{
  const char **driver_names = trinarkular_driver_factory_get_driver_names();
  int i;
  assert(driver_names != NULL);

  fprintf(stderr,
          "Usage: %s [options]\n"
          "       -d <driver>      driver to use for probes\n"
          "                        options are:\n",
          name);

  for (i=0; i <= TRINARKULAR_DRIVER_ID_MAX; i++) {
    fprintf(stderr, "                          - %s\n", driver_names[i]);
  }
}

static void cleanup()
{
  if (driver != NULL) {
    driver->destroy(driver);
    driver = NULL;
  }
}

int main(int argc, char **argv)
{
  int opt;
  int prevoptind;

  char *driver_name = NULL;

  trinarkular_probe_req_t req;
  uint64_t seq_num;
  int req_cnt;

  trinarkular_probe_resp_t resp;
  int resp_cnt = 0;

  signal(SIGINT, catch_sigint);

  while(prevoptind = optind,
	(opt = getopt(argc, argv, ":d:v?")) >= 0)
    {
      if (optind == prevoptind + 2 &&
          optarg && *optarg == '-' && *(optarg+1) != '\0') {
        opt = ':';
        -- optind;
      }
      switch(opt)
	{
	case 'd':
          driver_name = strdup(optarg);
          assert(driver_name != NULL);
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

  if (driver_name == NULL) {
    fprintf(stderr, "ERROR: Driver name must be specifed using -d\n");
    usage(argv[0]);
    goto err;
  }

  if ((driver =
       trinarkular_driver_factory_alloc_driver_by_name(driver_name)) == NULL) {
    goto err;
  }

  if (driver->init(driver, 0, NULL) != 0) {
    // probably just an invalid argument
    goto err;
  }

  // queue a bunch of measurements
  for (req_cnt=0; req_cnt<10; req_cnt++) {
    req.target_ip = rand() % (((uint64_t)1<<32)-1);
    if ((seq_num = driver->queue(driver, req)) == 0) {
      trinarkular_log("ERROR: Could not queue probe request");
      goto err;
    }
    trinarkular_probe_req_fprint(stdout, &req, seq_num);
  }

  // do blocking recv's until all replies are received
  for (resp_cnt = 0; resp_cnt < req_cnt; resp_cnt++) {
    if (driver->recv(driver, &resp, 1) != 1) {
      trinarkular_log("Could not receive response");
      goto err;
    }

    trinarkular_probe_resp_fprint(stdout, &resp);
  }

  assert(resp_cnt == req_cnt);

  trinarkular_log("done");

  cleanup();
  return 0;

 err:
  cleanup();
  return -1;
}
