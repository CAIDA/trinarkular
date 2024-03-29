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

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "parse_cmd.h"

#include "trinarkular_driver_interface.h"
#include "trinarkular_driver.h"
#include "trinarkular_log.h"
#include "trinarkular_probe_io.h"
#include "trinarkular_signal.h"

#include "trinarkular_driver_test.h"

#ifdef WITH_SCAMPER
#include "trinarkular_driver_scamper.h"
#endif

#define MAXOPTS 1024

/** To be run within a zloop handler */
#define CHECK_SHUTDOWN(err_handle)                                             \
  do {                                                                         \
    if (zctx_interrupted != 0 || TRINARKULAR_DRIVER_DEAD(drv) != 0) {          \
      trinarkular_log("Interrupted, shutting down");                           \
      err_handle;                                                              \
    }                                                                          \
  } while (0)

typedef trinarkular_driver_t *(*alloc_func_t)();

/** Array of driver allocation functions.
 *
 * @note the indexes of these functions must exactly match the ID in
 * trinarkular_driver_id_t.
 */
static const alloc_func_t alloc_funcs[] = {
  trinarkular_driver_test_alloc,
#ifdef WITH_SCAMPER
  trinarkular_driver_scamper_alloc,
#else
  NULL,
#endif
};

/** Array of driver names. Not the most elegant solution, but it will do for
    now */
static const char *driver_names[] = {
  "test",
#ifdef WITH_SCAMPER
  "scamper",
#else
  NULL,
#endif
};

static int handle_req(zloop_t *loop, zsock_t *reader, void *arg)
{
  trinarkular_driver_t *drv = (trinarkular_driver_t *)arg;

  char *command = NULL;

  trinarkular_probe_req_t req;

  if ((command = trinarkular_probe_recv_str(TRINARKULAR_DRIVER_DRIVER_PIPE(drv),
                                            0)) == NULL) {
    goto shutdown;
  }
  CHECK_SHUTDOWN(goto shutdown);

  if (strcmp("$TERM", command) == 0) {
    goto shutdown;
  } else if (strcmp("REQ", command) == 0) {
    if (trinarkular_probe_req_recv(TRINARKULAR_DRIVER_DRIVER_PIPE(drv), &req) !=
        0) {
      goto shutdown;
    }

    if (drv->handle_req(drv, &req) == -1) {
      goto shutdown;
    }
  } else {
    trinarkular_log("WARN: Unknown command received (%s)", command);
  }

  free(command);
  return 0;

shutdown:
  free(command);
  TRINARKULAR_DRIVER_DEAD(drv) = 1;
  return -1;
}

static void drv_run(zsock_t *pipe, void *args)
{
  trinarkular_driver_t *drv = (trinarkular_driver_t *)args;

  TRINARKULAR_DRIVER_DRIVER_PIPE(drv) = zsock_resolve(pipe);

  if ((TRINARKULAR_DRIVER_ZLOOP(drv) = zloop_new()) == NULL) {
    trinarkular_log("ERROR: Could not create loop");
    goto shutdown;
  }

  // poll the parent socket for commands
  if (zloop_reader(TRINARKULAR_DRIVER_ZLOOP(drv),
                   TRINARKULAR_DRIVER_DRIVER_PIPE(drv), handle_req, drv) != 0) {
    trinarkular_log("ERROR: Could not add reader to loop");
    goto shutdown;
  }

  // allow the subclass to create thread-specific state
  if (drv->init_thr(drv) != 0) {
    goto shutdown;
  }

  // signal that we are ready for messages
  if (zsock_signal(pipe, 0) != 0) {
    trinarkular_log("ERROR: Could not send ready signal to user thread");
    goto shutdown;
  }

  while (zloop_start(TRINARKULAR_DRIVER_ZLOOP(drv)) == 0) {
    // Only reenter zloop_start() if we got a SIGHUP.
    if (errno == EINTR && sighup_received) {
      sighup_received = 0;
    } else {
      break;
    }
  }

  trinarkular_log("driver thread shutting down");

shutdown:
  TRINARKULAR_DRIVER_DEAD(drv) = 1;
  zloop_destroy(&TRINARKULAR_DRIVER_ZLOOP(drv));
  return;
}

trinarkular_driver_t *trinarkular_driver_create(trinarkular_driver_id_t drv_id,
                                                char *args)
{
  trinarkular_driver_t *drv;

  char *local_args = NULL;
  char *process_argv[MAXOPTS];
  int len;
  int process_argc = 0;

  if (drv_id > TRINARKULAR_DRIVER_ID_MAX || alloc_funcs[drv_id] == NULL) {
    trinarkular_log("ERROR: Invalid driver ID %d", drv_id);
    return NULL;
  }

  if ((drv = alloc_funcs[drv_id]()) == NULL) {
    return NULL;
  }

  /* now parse the options */
  if (args != NULL && (len = strlen(args)) > 0) {
    local_args = strndup(args, len);
    parse_cmd(local_args, &process_argc, process_argv, MAXOPTS, drv->name);
  }

  if (drv->init(drv, process_argc, process_argv) != 0) {
    goto err;
  }

  // unlimited buffers between prober and drivers
  zsys_set_pipehwm(0);

  // start the actor
  trinarkular_log("starting driver thread");
  if ((TRINARKULAR_DRIVER_ACTOR(drv) = zactor_new(drv_run, drv)) == NULL) {
    trinarkular_log("ERROR: Could not start driver thread");
    goto err;
  }

  /* by the time the zactor_new function returns, the simulator has been
   initialized, so lets check for any error messages that it has signaled */
  if (TRINARKULAR_DRIVER_DEAD(drv) != 0) {
    trinarkular_log("ERROR: Driver thread has already shut down");
    goto err;
  }

  // store the actor's socket for users to poll looking for responses
  TRINARKULAR_DRIVER_USER_PIPE(drv) =
    zactor_resolve(TRINARKULAR_DRIVER_ACTOR(drv));
  assert(TRINARKULAR_DRIVER_USER_PIPE(drv) != NULL);

  free(local_args);
  return drv;

err:
  if (drv != NULL) {
    drv->destroy(drv);
  }
  free(local_args);
  return NULL;
}

trinarkular_driver_t *trinarkular_driver_create_by_name(const char *drv_name,
                                                        char *args)
{
  int id;

  if (drv_name == NULL) {
    return NULL;
  }

  for (id = 0; id <= TRINARKULAR_DRIVER_ID_MAX; id++) {
    if (strcmp(driver_names[id], drv_name) == 0) {
      return trinarkular_driver_create(id, args);
    }
  }
  trinarkular_log("ERROR: No driver named '%s' found", drv_name);
  return NULL;
}

const char **trinarkular_driver_get_driver_names()
{
  return driver_names;
}

void trinarkular_driver_destroy(trinarkular_driver_t *drv)
{
  if (drv == NULL) {
    return;
  }

  /* shut the driver thread down */
  zactor_destroy(&TRINARKULAR_DRIVER_ACTOR(drv));

  drv->destroy(drv);

  free(drv);
}

void *trinarkular_driver_get_recv_socket(trinarkular_driver_t *drv)
{
  assert(drv != NULL);

  return TRINARKULAR_DRIVER_USER_PIPE(drv);
}

int trinarkular_driver_queue_req(trinarkular_driver_t *drv,
                                 trinarkular_probe_req_t *req)
{
  return trinarkular_probe_req_send(TRINARKULAR_DRIVER_USER_PIPE(drv), req);
}

int trinarkular_driver_recv_resp(trinarkular_driver_t *drv,
                                 trinarkular_probe_resp_t *resp, int blocking)
{
  char *command;

  // check for a RESP command
  if ((command = trinarkular_probe_recv_str(TRINARKULAR_DRIVER_USER_PIPE(drv),
                                            blocking ? 0 : ZMQ_DONTWAIT)) ==
      NULL) {
    goto err;
  }
  if (blocking == 0 && command == NULL) {
    return 0;
  }
  CHECK_SHUTDOWN(goto err);
  if (strcmp("RESP", command) != 0) {
    trinarkular_log("ERROR: Invalid command (%s) received", command);
    goto err;
  }

  if (trinarkular_probe_resp_recv(TRINARKULAR_DRIVER_USER_PIPE(drv), resp) !=
      0) {
    goto err;
  }

  free(command);
  return 1;

err:
  free(command);
  return -1;
}

// defined in trinarkular_driver_interface.h
int trinarkular_driver_yield_resp(trinarkular_driver_t *drv,
                                  trinarkular_probe_resp_t *resp)
{
  // send the response to our parent thread
  return trinarkular_probe_resp_send(TRINARKULAR_DRIVER_DRIVER_PIPE(drv), resp);
}
