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

#include "trinarkular_log.h"
#include "trinarkular_driver_factory.h"

#include "trinarkular_driver_test.h"
#include "trinarkular_driver_scamper.h"

typedef trinarkular_driver_t * (*alloc_func_t)();

/** Array of driver allocation functions.
 *
 * @note the indexes of these functions must exactly match the ID in
 * trinarkular_driver_id_t.
 */
static const alloc_func_t alloc_funcs[] = {
  trinarkular_driver_test_alloc,
  trinarkular_driver_scamper_alloc,
};

/** Array of driver names. Not the most elegant solution, but it will do for
    now */
static const char *driver_names[] = {
  "test",
  "scamper",
};

trinarkular_driver_t *
trinarkular_driver_factory_alloc_driver(trinarkular_driver_id_t drv_id)
{
  if (drv_id > TRINARKULAR_DRIVER_ID_MAX) {
    trinarkular_log("ERROR: Invalid driver ID");
    return NULL;
  }

  return alloc_funcs[drv_id]();
}

trinarkular_driver_t *
trinarkular_driver_factory_alloc_driver_by_name(const char *drv_name)
{
  int id;

  if (drv_name == NULL) {
    return NULL;
  }

  for (id = 0; id <= TRINARKULAR_DRIVER_ID_MAX; id++) {
    if (strcmp(driver_names[id], drv_name) == 0) {
      return trinarkular_driver_factory_alloc_driver(id);
    }
  }
  trinarkular_log("ERROR: No driver named '%s' found", drv_name);
  return NULL;
}

const char **
trinarkular_driver_factory_get_driver_names()
{
  return driver_names;
}
