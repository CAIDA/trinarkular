/*
 * This file is part of trinarkular
 *
 * Copyright (C) 2015 The Regents of the University of California.
 * Authors: Alistair King
 *
 * This software is Copyright (c) 2015 The Regents of the University of
 * California. All Rights Reserved. Permission to copy, modify, and distribute this
 * software and its documentation for academic research and education purposes,
 * without fee, and without a written agreement is hereby granted, provided that
 * the above copyright notice, this paragraph and the following three paragraphs
 * appear in all copies. Permission to make use of this software for other than
 * academic research and education purposes may be obtained by contacting:
 *
 * Office of Innovation and Commercialization
 * 9500 Gilman Drive, Mail Code 0910
 * University of California
 * La Jolla, CA 92093-0910
 * (858) 534-5815
 * invent@ucsd.edu
 *
 * This software program and documentation are copyrighted by The Regents of the
 * University of California. The software program and documentation are supplied
 * "as is", without any accompanying services from The Regents. The Regents does
 * not warrant that the operation of the program will be uninterrupted or
 * error-free. The end-user understands that the program was developed for research
 * purposes and is advised not to rely exclusively on the program for any reason.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST
 * PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
 * THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE. THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS
 * IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE
 * MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Report any bugs, questions or comments to alistair@caida.org
 *
 */

#ifndef __TRINARKULAR_DRIVER_INTERFACE_H
#define __TRINARKULAR_DRIVER_INTERFACE_H

#include <czmq.h>

#include "trinarkular_driver.h"
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
#define TRINARKULAR_DRIVER_GENERATE_PROTOS(drvname)                            \
  trinarkular_driver_t *trinarkular_driver_##drvname##_alloc();                \
  int trinarkular_driver_##drvname##_init(trinarkular_driver_t *drv, int argc, \
                                          char **argv);                        \
  void trinarkular_driver_##drvname##_destroy(trinarkular_driver_t *drv);      \
  int trinarkular_driver_##drvname##_init_thr(trinarkular_driver_t *drv);      \
  int trinarkular_driver_##drvname##_handle_req(trinarkular_driver_t *drv,     \
                                                trinarkular_probe_req_t *req);

/** Convenience macro that defines all the driver function pointers */
#define TRINARKULAR_DRIVER_GENERATE_PTRS(drvname)                              \
  trinarkular_driver_##drvname##_init, trinarkular_driver_##drvname##_destroy, \
    trinarkular_driver_##drvname##_init_thr,                                   \
    trinarkular_driver_##drvname##_handle_req,

#define TRINARKULAR_DRIVER_HEAD_DECLARE                                        \
  trinarkular_driver_id_t id;                                                  \
  char *name;                                                                  \
  zactor_t *driver_actor;                                                      \
  void *user_pipe;                                                             \
  void *driver_pipe;                                                           \
  zloop_t *driver_loop;                                                        \
  int dead;                                                                    \
  int (*init)(struct trinarkular_driver * drv, int argc, char **argv);         \
  void (*destroy)(struct trinarkular_driver * drv);                            \
  int (*init_thr)(struct trinarkular_driver * drv);                            \
  int (*handle_req)(struct trinarkular_driver * drv,                           \
                    trinarkular_probe_req_t * req);

#define TRINARKULAR_DRIVER_HEAD_INIT(drv_id, drv_strname, drvname)             \
  drv_id, drv_strname, NULL, NULL, NULL, NULL, 0,                              \
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

  /* user-thread fields that are common to all drivers. Driver implementors
     should use accessor macros */

  /** The actor that runs the driver thread */
  zactor_t *driver_actor;

  /** The zsocket to poll for a response to be ready (user side of the actor
      pipe) */
  void *user_pipe;

  /** driver-thread fields common to all drivers */

  /** The socket to talk to the user thread */
  void *driver_pipe;

  /** The loop that runs inside the driver thread */
  zloop_t *driver_loop;

  /** Has the driver thread shut down? */
  int dead;

  /* ============================================================ */
  /* Functions that run in the user's thread                      */

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
   *
   * This is guaranteed to be run **before** the driver thread is started
   */
  int (*init)(struct trinarkular_driver *drv, int argc, char **argv);

  /** Shutdown and free driver-specific state for this driver
   *
   * @param drv    The driver object to free
   *
   * This is guaranteed to be called **after** the driver thread has exited
   * This **must not** free the drv structure
   */
  void (*destroy)(struct trinarkular_driver *drv);

  /* ============================================================ */
  /* Functions that run in the driver thread                      */

  /** Initialize any state that requires the event loop or driver pipe
   *
   * @param drv         The driver object
   * @return 0 if the driver thread state was initialized successfully, -1
   * otherwise
   *
   * This function is called from within the driver thread **after** the event
   * loop has been created, but before it is started.  All thread-specific state
   * is valid at this point.
   */
  int (*init_thr)(struct trinarkular_driver *drv);

  /** Handle given probe request (i.e. send a probe!)
   *
   * @param drv         The driver object
   * @oaram req         Pointer to the probe request
   * @return 0 if request was handled successfully, -1 otherwise
   *
   * As this function is called from within the driver thread, be extremely
   * careful to not touch any state from the user thread.
   */
  int (*handle_req)(struct trinarkular_driver *drv,
                    trinarkular_probe_req_t *req);
};

/* user-thread accessors */

/** Get a pointer to the driver actor */
#define TRINARKULAR_DRIVER_ACTOR(drv) (drv->driver_actor)

/** Get a pointer to the recv side of the driver socket */
#define TRINARKULAR_DRIVER_USER_PIPE(drv) (drv->user_pipe)

/* driver-thread accessors */

/** Get the internal zloop that forms the driver's event loop */
#define TRINARKULAR_DRIVER_ZLOOP(drv) (drv->driver_loop)

/** Get the send side of the pipe to the user thread */
#define TRINARKULAR_DRIVER_DRIVER_PIPE(drv) (drv->driver_pipe)

/** Get/set whether the driver thread has shut down */
#define TRINARKULAR_DRIVER_DEAD(drv) (drv->dead)

// implemented in trinarkular_driver.c
/** Yield a probe response to the user thread
 *
 * @param drv         The driver object
 * @oaram req         Pointer to the probe request
 * @return 0 if request was yielded successfully, -1 otherwise
 */
int trinarkular_driver_yield_resp(trinarkular_driver_t *drv,
                                  trinarkular_probe_resp_t *resp);

#endif /* __TRINARKULAR_DRIVER_INTERFACE_H */
