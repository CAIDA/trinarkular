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

#ifndef __TRINARKULAR_DRIVER_H
#define __TRINARKULAR_DRIVER_H

#include "trinarkular_probe.h"

/** @file
 *
 * @brief Header file that exposes a trinarkular driver
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

/* factory methods */

/** Allocate the driver with the given ID
 *
 * @param drv_id        ID of the driver to allocate
 * @param args          Config string for the driver
 * @return pointer to the allocated driver if successful, NULL otherwise
 *
 * @note the returned driver **must** be destroyed by calling the destroy method
 */
trinarkular_driver_t *
trinarkular_driver_create(trinarkular_driver_id_t drv_id, char *args);

/** Allocate the driver with the given name
 *
 * @param drv_name      name of the driver to allocate
 * @param args          Config string for the driver
 * @return pointer to the allocated driver if successful, NULL otherwise
 *
 * @note the returned driver **must** be destroyed by calling the destroy method
 */
trinarkular_driver_t *
trinarkular_driver_create_by_name(const char *drv_name, char *args);

/** Get an array of driver names
 *
 * @return borrowed pointer to an array of driver names with exactly
 * TRINARKULAR_DRIVER_ID_MAX+1 elements
 *
 * There may be NULL elements in the returned array. These correspond to drivers
 * that have not been compiled.
 */
const char **
trinarkular_driver_get_driver_names();


/* instance methods */

/** Shutdown and free state for this driver
 *
 * @param drv    The driver object to free
 */
void
trinarkular_driver_destroy(trinarkular_driver_t *drv);

/** Queue the given probe request
 *
 * @param drv         The driver object
 * @oaram req         Pointer to the probe request
 * @return sequence number (>0) for matching replies to requests, 0 if an
 * error occurred.
 */
seq_num_t
trinarkular_driver_queue_req(trinarkular_driver_t *drv,
                             trinarkular_probe_req_t *req);

/** Get an opaque socket to use when using zmq_poll or zloop in an event loop
 *
 * @param drv         The driver object to get socket from
 * @return pointer to a ZMQ socket to poll
 */
void *
trinarkular_driver_get_recv_socket(trinarkular_driver_t *drv);

/** Poll for a probe response
 *
 * @param drv         The driver object
 * @param resp        Pointer to a response object to fill
 * @param blocking    If non-zero, the recv will block until a response is ready
 * @return 1 if a response was received, 0 if non-blocking and no response was
 * ready, -1 if an error occurred
 *
 * If using a ZMQ poller (or zloop), use trinarkular_driver_get_recv_socket to
 * get a socket to poll for responses, then use this function to receive a
 * response.
 */
int
trinarkular_driver_recv_resp(trinarkular_driver_t *drv,
                             trinarkular_probe_resp_t *resp,
                             int blocking);

#endif /* __TRINARKULAR_DRIVER_H */
