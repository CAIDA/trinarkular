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
#include <string.h>

#include "utils.h"

#include "trinarkular_log.h"
#include "trinarkular_driver_interface.h"

#include "trinarkular_driver_scamper.h"

/** Our 'subclass' of the generic driver */
typedef struct scamper_driver {
  TRINARKULAR_DRIVER_HEAD_DECLARE

  // add our fields here

  int a_scamper_field;

} scamper_driver_t;

/** Our class instance */
static scamper_driver_t clz = {
  TRINARKULAR_DRIVER_HEAD_INIT(TRINARKULAR_DRIVER_ID_SCAMPER, "scamper", scamper)
  0, // a_scamper_field
};

trinarkular_driver_t *
trinarkular_driver_scamper_alloc()
{
  scamper_driver_t *drv = NULL;

  if ((drv = malloc(sizeof(scamper_driver_t))) == NULL) {
    trinarkular_log("ERROR: failed");
    return NULL;
  }

  memcpy(drv, &clz, sizeof(scamper_driver_t));

  return (trinarkular_driver_t *)drv;
}

int trinarkular_driver_scamper_init(trinarkular_driver_t *drv,
                                 int argc, char **argv)
{
  scamper_driver_t *this = (scamper_driver_t *)drv;

  this->a_scamper_field = 11;

  return 0;
}

void trinarkular_driver_scamper_destroy(trinarkular_driver_t *drv)
{
  free(drv);
}
