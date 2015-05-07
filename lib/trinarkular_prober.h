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

#ifndef __TRINARKULAR_PROBER_H
#define __TRINARKULAR_PROBER_H

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
 * @return pointer to a prober object if successful, NULL otherwise
 */
trinarkular_prober_t *
trinarkular_prober_create();

/** Destroy the given Trinarkular Prober
 *
 * @param prober        pointer to the prober to destroy
 */
void
trinarkular_prober_destroy(trinarkular_prober_t *prober);

/** Assign a probelist to the given prober
 *
 * @param prober        pointer to the prober to assign the list to
 * @param pl            pointer to the probelist to assign to the prober
 *
 * @note the prober takes ownership of the probelist.
 * This function must be called before calling trinarkular_prober_start.
 * To change probe lists, a new prober should be created.
 */
void
trinarkular_prober_assign_probelist(trinarkular_prober_t *prober,
                                    trinarkular_probelist_t *pl);

/** TODO: set paramters! */

/** Start the given prober
 *
 * @param prober        pointer to the prober to start
 * @return 0 if the prober ran successfully, -1 if an error occurred
 *
 * @note this function will block until the prober is stopped, either by calling
 * trinarkular_prober_stop, or by receiving an interrupt, or by encountering an
 * error.
 */
int
trinarkular_prober_start(trinarkular_prober_t *prober);

/** Stop the given prober at the next opportunity
 *
 * @param prober        pointer to the prober to stop
 *
 */
void
trinarkular_prober_stop(trinarkular_prober_t *prober);

/** Set the periodic round duration
 *
 * @param prober        pointer to the prober to set parameter for
 * @param duration      periodic round duration to set
 *
 */
void
trinarkular_prober_set_periodic_round_duration(trinarkular_prober_t *prober,
                                               uint64_t duration);

/** Set the periodic slice count
 *
 * @param prober        pointer to the prober to set parameter for
 * @param slices        number of slices to use for periodic probing
 *
 */
void
trinarkular_prober_set_periodic_round_slices(trinarkular_prober_t *prober,
                                             int slices);

/** Set the random number generator seed
 *
 * @param prober        pointer to the prober to set parameter for
 * @param seed          seed for the random number generator
 *
 */
void
trinarkular_prober_set_random_seed(trinarkular_prober_t *prober,
                                   int seed);


#endif /* __TRINARKULAR_PROBER_H */
