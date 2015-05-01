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
#include <stdlib.h>

#include "utils.h"

#include "trinarkular.h"
#include "trinarkular_log.h"
#include "trinarkular_probelist.h"
#include "trinarkular_prober.h"

/* Structure representing a prober instance */
struct trinarkular_prober {

  /** Has this prober been started? */
  int started;

  /** Should the prober shut down? */
  int shutdown;

  /** Probelist that this prober is probing */
  trinarkular_probelist_t *pl;

};

trinarkular_prober_t *
trinarkular_prober_create()
{
  trinarkular_prober_t *prober;

  if ((prober = malloc_zero(sizeof(trinarkular_prober_t))) == NULL) {
    trinarkular_log("ERROR: Could not create prober object");
    return NULL;
  }

  trinarkular_log("done");

  return prober;
}

void
trinarkular_prober_destroy(trinarkular_prober_t *prober)
{
  trinarkular_probelist_destroy(prober->pl);
  prober->pl = NULL;

  free(prober);
}

void
trinarkular_prober_assign_probelist(trinarkular_prober_t *prober,
                                    trinarkular_probelist_t *pl)
{
  assert(prober != NULL);
  assert(prober->started == 0);

  prober->pl = pl;
}

int
trinarkular_prober_start(trinarkular_prober_t *prober)
{
  assert(prober != NULL);
  assert(prober->started == 0);

  prober->started = 1;

  trinarkular_log("prober up and running");

  return 0;
}

void
trinarkular_prober_stop(trinarkular_prober_t *prober)
{
  assert(prober != NULL);

  prober->shutdown = 1;

  trinarkular_log("waiting to shut down");
}
