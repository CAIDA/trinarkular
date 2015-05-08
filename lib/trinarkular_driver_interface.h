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
#include "trinarkular_probe.h"

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
  void trinarkular_driver_##drvname##_destroy(trinarkular_driver_t *drv); \
  uint64_t trinarkular_driver_##drvname##_queue(trinarkular_driver_t *drv, \
                                                trinarkular_probe_req_t req); \
  int trinarkular_driver_##drvname##_recv(struct trinarkular_driver *drv, \
                                          trinarkular_probe_resp_t *resp, \
                                          int blocking);

/** Convenience macro that defines all the driver function pointers */
#define TRINARKULAR_DRIVER_GENERATE_PTRS(drvname)       \
  trinarkular_driver_##drvname##_init,                  \
    trinarkular_driver_##drvname##_destroy,             \
    trinarkular_driver_##drvname##_queue,               \
    trinarkular_driver_##drvname##_recv,

#define TRINARKULAR_DRIVER_HEAD_DECLARE                                 \
  trinarkular_driver_id_t id;                                           \
  char *name;                                                           \
  int (*init)(struct trinarkular_driver *drv, int argc, char **argv);   \
  void (*destroy)(struct trinarkular_driver *drv);                      \
  uint64_t (*queue)(struct trinarkular_driver *drv,                     \
                    trinarkular_probe_req_t req);                       \
  int (*recv)(struct trinarkular_driver *drv, trinarkular_probe_resp_t *resp, \
              int blocking);                                            \
  uint64_t next_seq_num;                                                \
  void *resp_socket;

#define TRINARKULAR_DRIVER_HEAD_INIT(drv_id, drv_strname, drvname)  \
  drv_id,                                                           \
    drv_strname,                                                    \
    TRINARKULAR_DRIVER_GENERATE_PTRS(drvname)                       \
    1,                                                              \
    NULL,

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

  /** Queue the given probe request
   *
   * @param drv         The driver object
   * @oaram req         Pointer to the probe request
   * @return sequence number (>0) for matching replies to requests, 0 if an
   * error occurred.
   */
  uint64_t (*queue)(struct trinarkular_driver *drv,
                    trinarkular_probe_req_t req);

  /** Poll for a probe response
   *
   * @param drv         The driver object
   * @param resp        Pointer to a response object to fill
   * @param blocking    If non-zero, the recv will block until a response is ready
   * @return 1 if a response was received, 0 if non-blocking and no response was
   * ready, -1 if an error occurred
   */
  int (*recv)(struct trinarkular_driver *drv, trinarkular_probe_resp_t *resp,
              int blocking);

  /* fields that are common to all drivers. Driver implementors should use
     accessor macros below */

  /** The sequence number to use for the next request */
  uint64_t next_seq_num;

  /** The zsocket to poll for a response to be ready */
  void *resp_socket;

};

/* common accessors */

#define TRINARKULAR_DRIVER_NEXT_SEQ_NUM(drv) (drv->next_seq_num++)

#define TRINARKULAR_DRIVER_RESP_SOCKET(drv) (drv->resp_socket)

#endif /* __TRINARKULAR_DRIVER_INTERFACE_H */
