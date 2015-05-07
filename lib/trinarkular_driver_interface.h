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

#ifndef __TRINARKULAR_DRIVER_INTERFACE_H
#define __TRINARKULAR_DRIVER_INTERFACE_H

#include "trinarkular_driver_factory.h"

/** @file
 *
 * @brief Header file that exposes the public interface of a trinarkular probe
 * driver
 *
 * @author Alistair King
 *
 */


/** Convenience macro that defines all the driver function prototypes */
#define TRINARKULAR_DRIVER_GENERATE_PROTOS(drvname)                     \
  trinarkular_driver_t *trinarkular_driver_##drvname##_alloc();         \
  int trinarkular_driver_##drvname##_init(trinarkular_driver_t *drv,    \
                                          int argc, char **argv);       \
  void trinarkular_driver_##drvname##_destroy(trinarkular_driver_t *drv);

/** Convenience macro that defines all the driver function pointers */
#define TRINARKULAR_DRIVER_GENERATE_PTRS(drvname)       \
  trinarkular_driver_##drvname##_init,                  \
    trinarkular_driver_##drvname##_destroy,

#define TRINARKULAR_DRIVER_HEAD_DECLARE                               \
  trinarkular_driver_id_t id;                                         \
  char *name;                                                         \
  int (*init)(struct trinarkular_driver *drv, int argc, char **argv); \
  void (*destroy)(struct trinarkular_driver *drv);

#define TRINARKULAR_DRIVER_HEAD_INIT(drv_id, drv_strname, drvname)  \
  drv_id,                                                           \
    drv_strname,                                                    \
    TRINARKULAR_DRIVER_GENERATE_PTRS(drvname)

/** Structure that represents the trinarkular driver interface.
 *
 * Implementors must provide exactly these fields in the structure returned by
 * _alloc. The easiest way to do this is to use the
 * TRINARKULAR_DRIVER_HEAD_INIT macro as the first item in the structure
*/
struct trinarkular_driver {

  /** The ID of the driver */
  trinarkular_driver_id_t id;

  /** The name of the driver */
  char *name;

  /** Initialize and enable this driver
   *
   * @param drv         The driver object to allocate
   * @param argc        The number of tokens in argv
   * @param argv        An array of strings parsed from the command line
   * @return 0 if the driver is successfully initialized, -1 otherwise
   *
   * @note the most common reason for returning -1 will likely be incorrect
   * command line arguments.
   *
   * @warning the strings contained in argv will be free'd once this function
   * returns. Ensure you make appropriate copies as needed.
   */
  int (*init)(struct trinarkular_driver *drv, int argc, char **argv);

  /** Shutdown and free driver-specific state for this driver
   *
   * @param drv    The driver object to free
   */
  void (*destroy)(struct trinarkular_driver *drv);

};


#endif /* __TRINARKULAR_DRIVER_INTERFACE_H */
