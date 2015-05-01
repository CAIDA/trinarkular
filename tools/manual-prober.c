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
#include <wandio.h>

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
          "Usage: %s probelist\n",
          name);
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
  signal(SIGINT, catch_sigint);

  if (argc != 2) {
    fprintf(stderr, "ERROR: Probelist file must be specifed\n");
    usage(argv[0]);
    cleanup();
    return -1;
  }

  if ((prober = trinarkular_prober_create()) == NULL) {
    goto err;
  }

  if ((pl = trinarkular_probelist_create_from_file(argv[1])) == NULL) {
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
