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

#ifndef __TRINARKULAR_DRIVER_FACTORY_H
#define __TRINARKULAR_DRIVER_FACTORY_H

/** @file
 *
 * @brief Header file that exposes the trinarkular driver factory
 * driver
 *
 * @author Alistair King
 *
 */

/** Forward-declaration of driver structure (defined in
 * trinarkular_driver_interface.h)
 */
typedef struct trinarkular_driver trinarkular_driver_t;

typedef enum trinarkular_driver_id {

  /** Test driver */
  TRINARKULAR_DRIVER_ID_TEST = 0,

  /** Scamper driver */
  TRINARKULAR_DRIVER_ID_SCAMPER = 1,

} trinarkular_driver_id_t;

/** Must always be defined to the highest ID in use */
#define TRINARKULAR_DRIVER_ID_MAX TRINARKULAR_DRIVER_ID_SCAMPER

/** Allocate the driver with the given ID
 *
 * @param drv_id        ID of the driver to allocate
 * @return pointer to the allocated driver if successful, NULL otherwise
 *
 * @note the returned driver **must** be destroyed by calling the destroy method
 */
trinarkular_driver_t *
trinarkular_driver_factory_alloc_driver(trinarkular_driver_id_t drv_id);

/** Allocate the driver with the given name
 *
 * @param drv_name      name of the driver to allocate
 * @return pointer to the allocated driver if successful, NULL otherwise
 *
 * @note the returned driver **must** be destroyed by calling the destroy method
 */
trinarkular_driver_t *
trinarkular_driver_factory_alloc_driver_by_name(const char *drv_name);

/** Get an array of driver names
 *
 * @return borrowed pointer to an array of driver names with exactly
 * TRINARKULAR_DRIVER_ID_MAX+1 elements
 */
const char **
trinarkular_driver_factory_get_driver_names();

// allow this header to be included by itself
#include "trinarkular_driver_interface.h"

#endif /* __TRINARKULAR_DRIVER_FACTORY_H */
