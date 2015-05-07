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
#include "trinarkular_driver_interface.h"

#include "trinarkular_driver_test.h"

/** Our 'subclass' of the generic driver */
typedef struct test_driver {
  TRINARKULAR_DRIVER_HEAD_DECLARE

  // add our fields here

  int a_test_field;

} test_driver_t;

/** Our class instance */
static test_driver_t clz = {
  TRINARKULAR_DRIVER_HEAD_INIT(TRINARKULAR_DRIVER_ID_TEST, "test", test)
  0, // a_test_field
};

trinarkular_driver_t *
trinarkular_driver_test_alloc()
{
  test_driver_t *drv = NULL;

  if ((drv = malloc(sizeof(test_driver_t))) == NULL) {
    trinarkular_log("ERROR: failed");
    return NULL;
  }

  memcpy(drv, &clz, sizeof(test_driver_t));

  return (trinarkular_driver_t *)drv;
}

int trinarkular_driver_test_init(trinarkular_driver_t *drv,
                                 int argc, char **argv)
{
  test_driver_t *this = (test_driver_t *)drv;

  this->a_test_field = 11;

  return 0;
}

void trinarkular_driver_test_destroy(trinarkular_driver_t *drv)
{
  free(drv);
}
