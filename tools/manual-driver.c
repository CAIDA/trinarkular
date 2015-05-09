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
#include "trinarkular_driver.h" // not included in trinarkular.h

static trinarkular_driver_t *driver = NULL;

/** Indicates that we are waiting to shutdown */
volatile sig_atomic_t shutdown = 0;

/** The number of SIGINTs to catch before aborting */
#define HARD_SHUTDOWN 3

#define PROBE_CNT 3
#define WAIT 3000
#define TARGET_CNT 10

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
      trinarkular_driver_destroy(driver);
    }

  signal(sig, catch_sigint);
}

static void usage(char *name)
{
  const char **driver_names = trinarkular_driver_get_driver_names();
  int i;
  assert(driver_names != NULL);

  fprintf(stderr,
          "Usage: %s [options] -d driver\n"
          "       -c <probecount>  max number of probes to send (default: %d)\n"
          "       -d <driver>      driver to use for probes\n"
          "                        options are:\n",
          name,
          PROBE_CNT);

  for (i=0; i <= TRINARKULAR_DRIVER_ID_MAX; i++) {
    fprintf(stderr, "                          - %s\n", driver_names[i]);
  }

  fprintf(stderr,
          "       -i <wait>        msec to wait between probes (default: %d)\n"
          "       -t <targets>     number of targets to probe (default: %d)\n",
          WAIT,
          TARGET_CNT);

}

static void cleanup()
{
  trinarkular_driver_destroy(driver);
  driver = NULL;
}

int main(int argc, char **argv)
{
  int opt, prevoptind, lastopt;

  char *driver_name = NULL;
  char *driver_arg_ptr = NULL;

  trinarkular_probe_req_t req;
  seq_num_t seq_num;
  int req_cnt;
  int target_cnt = TARGET_CNT;

  trinarkular_probe_resp_t resp;
  int resp_cnt = 0;

  int responsive_count = 0;
  int probe_count = 0;

  signal(SIGINT, catch_sigint);

  // set defaults for the request
  req.probecount = PROBE_CNT;
  req.wait = WAIT;

  while(prevoptind = optind,
	(opt = getopt(argc, argv, ":c:d:i:t:v?")) >= 0)
    {
      if (optind == prevoptind + 2 &&
          optarg && *optarg == '-' && *(optarg+1) != '\0') {
        opt = ':';
        -- optind;
      }
      switch(opt)
	{
        case 'c':
          req.probecount = atoi(optarg);
          break;

	case 'd':
          driver_name = strdup(optarg);
          assert(driver_name != NULL);
          break;

        case 'i':
          req.wait = atoi(optarg);
          break;

        case 't':
          target_cnt = atoi(optarg);
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

  /* store the value of the last index*/
  lastopt = optind;
  /* reset getopt for drivers to use */
  optind = 1;

  if (driver_name == NULL) {
    fprintf(stderr, "ERROR: Driver name must be specifed using -d\n");
    usage(argv[0]);
    goto err;
  }

  /* the driver_name string will contain the name of the driver, optionally
     followed by a space and then the arguments to pass to the driver */
  if ((driver_arg_ptr = strchr(driver_name, ' ')) != NULL) {
    /* set the space to a nul, which allows driver_name to be used for the
       provider name, and then increment driver_arg_ptr to point to the next
       character, which will be the start of the arg string (or at worst case,
       the terminating \0 */
    *driver_arg_ptr = '\0';
    driver_arg_ptr++;
  }

  if ((driver =
       trinarkular_driver_create_by_name(driver_name, driver_arg_ptr)) == NULL) {
    usage(argv[0]);
    goto err;
  }

  // queue a bunch of measurements
  for (req_cnt=0; req_cnt<target_cnt; req_cnt++) {
    req.target_ip = rand() % (((uint64_t)1<<32)-1);
    if ((seq_num = trinarkular_driver_queue_req(driver, &req)) == 0) {
      trinarkular_log("ERROR: Could not queue probe request");
      goto err;
    }
    trinarkular_probe_req_fprint(stdout, &req, seq_num);
  }

  // do blocking recv's until all replies are received
  for (resp_cnt = 0; resp_cnt < req_cnt; resp_cnt++) {
    if (trinarkular_driver_recv_resp(driver, &resp, 1) != 1) {
      trinarkular_log("Could not receive response");
      goto err;
    }

    trinarkular_probe_resp_fprint(stdout, &resp);

    responsive_count += resp.verdict;
    probe_count +=  resp.probes_sent;
  }

  assert(resp_cnt == req_cnt);

  trinarkular_log("done");

  fprintf(stdout,
          "\n----- SUMMARY -----\n"
          "Responsive Targets: %d/%d (%0.0f%%)\n"
          "Responsive Probes: %d/%d (%0.0f%%)\n"
          "-------------------\n",
          responsive_count,
          target_cnt,
          responsive_count * 100.0 / target_cnt,
          responsive_count,
          probe_count,
          responsive_count * 100.0 / probe_count);

  cleanup();
  return 0;

 err:
  cleanup();
  return -1;
}
