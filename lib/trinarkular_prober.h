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


#endif /* __TRINARKULAR_PROBER_H */
