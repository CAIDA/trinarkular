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
}

int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "ERROR: Probelist file must be specifed\n");
    usage(argv[0]);
    cleanup();
    return -1;
  }

  if ((pl = trinarkular_probelist_create_from_file(argv[1])) == NULL) {
    goto err;
  }

  // TODO: do things here

  cleanup();
  return 0;

 err:
  cleanup();
  return -1;
}
