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

#ifndef __TRINARKULAR_PROBER_H
#define __TRINARKULAR_PROBER_H

#include "trinarkular_driver.h"
#include "trinarkular_probelist.h"
#include <timeseries.h>

/** @file
 *
 * @brief Header file that exposes the public interface of trinarkular
 *
 * @author Alistair King
 *
 */

/**
 * @name Public Constants
 *
 * @{ */

/** Default number of msec in which the prober should complete one round of
    periodic probing (default: 10min) */
#define TRINARKULAR_PROBER_PERIODIC_ROUND_DURATION_DEFAULT 600000

/** Default number of periodic "slices" that the round is divided into. The
    probelist is divided into slices, and probes for all targets in a slice are
    queued simultaneously. */
#define TRINARKULAR_PROBER_PERIODIC_ROUND_SLICES_DEFAULT 60

/** Default maximum number of adaptive probes that can be sent to a single /24
    in one round */
#define TRINARKULAR_PROBER_ROUND_PROBE_BUDGET 14

/** Default timeout for periodic probes (default: 3 seconds) */
#define TRINARKULAR_PROBER_PERIODIC_PROBE_TIMEOUT_DEFAULT 3

/** Default probe driver to use (scamper) */
#define TRINARKULAR_PROBER_DRIVER_DEFAULT "test"

/** Default probe driver arguments */
#define TRINARKULAR_PROBER_DRIVER_ARGS_DEFAULT ""

/** Maximum number of probers that can be used */
#define TRINARKULAR_PROBER_DRIVER_MAX_CNT 100

/** @} */

/**
 * @name Public Enums
 *
 * @{ */

/** @} */

/**
 * @name Public Opaque Data Structures
 *
 * @{ */

/** Structure representing a trinarkular prober instance */
typedef struct trinarkular_prober trinarkular_prober_t;

/** @} */

/**
 * @name Public Data Structures
 *
 * @{ */

/** @} */

/** Create a new Trinarkular Prober
 *
 * @param name          name of the prober (used in timeseries paths)
 * @param probelist     filename of the probelist
 * @param ts_slash24    libtimeseries instance to use for per-/24 data
 * @param ts_aggr       libtimeseries instance to use for aggregated data
 * @return pointer to a prober object if successful, NULL otherwise
 */
trinarkular_prober_t *trinarkular_prober_create(const char *name,
                                                const char *probelist,
                                                timeseries_t *ts_slash24,
                                                timeseries_t *ts_aggr);

/** Destroy the given Trinarkular Prober
 *
 * @param prober        pointer to the prober to destroy
 */
void trinarkular_prober_destroy(trinarkular_prober_t *prober);

/** Start the given prober
 *
 * @param prober        pointer to the prober to start
 * @return 0 if the prober ran successfully, -1 if an error occurred
 *
 * @note this function will block until the prober is stopped, either by calling
 * trinarkular_prober_stop, or by receiving an interrupt, or by encountering an
 * error.
 */
int trinarkular_prober_start(trinarkular_prober_t *prober);

/** Stop the given prober at the next opportunity
 *
 * @param prober        pointer to the prober to stop
 *
 */
void trinarkular_prober_stop(trinarkular_prober_t *prober);

/** Set the periodic round duration
 *
 * @param prober        pointer to the prober to set parameter for
 * @param duration      periodic round duration to set
 *
 */
void trinarkular_prober_set_periodic_round_duration(
  trinarkular_prober_t *prober, uint64_t duration);

/** Set the periodic slice count
 *
 * @param prober        pointer to the prober to set parameter for
 * @param slices        number of slices to use for periodic probing
 *
 */
void trinarkular_prober_set_periodic_round_slices(trinarkular_prober_t *prober,
                                                  int slices);

/** Set the number of periodic rounds to complete
 *
 * @param prober        pointer to the prober to set parameter for
 * @param round         number of rounds to complete before shutting down
 *
 * If this is not set, the prober will operate indefinitely
 */
void trinarkular_prober_set_periodic_round_limit(trinarkular_prober_t *prober,
                                                 int rounds);

/** Set the timeout for periodic probes
 *
 * @param prober        pointer to the prober to set parameter for
 * @param timeout       probe timeout (in msec)
 */
void trinarkular_prober_set_periodic_probe_timeout(trinarkular_prober_t *prober,
                                                   uint32_t timeout);

/** Disable sleeping at startup to align with interval boundary.
 *
 * @param prober        pointer to the prober to set parameter for
 *
 */
void trinarkular_prober_disable_sleep_align_start(trinarkular_prober_t *prober);

/** Add an instance of the given driver to the prober
 *
 * @param prober        pointer to the prober to set parameter for
 * @param driver_name   name of the driver to use
 * @param driver_args   driver configuration string
 * @return 0 if successful, -1 otherwise
 *
 * The first time this is called, the default prober is replaced, successive
 * calls will add addition drivers that will be used in a round-robin fashion.
 */
int trinarkular_prober_add_driver(trinarkular_prober_t *prober,
                                  char *driver_name, char *driver_args);

/** Tell prober to reload probelist
 *
 * @param prober        pointer to the prober to set parameter for
 */
void trinarkular_prober_reload_probelist(trinarkular_prober_t *prober);

#endif /* __TRINARKULAR_PROBER_H */
