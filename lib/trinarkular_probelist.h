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

#ifndef __TRINARKULAR_PROBELIST_H
#define __TRINARKULAR_PROBELIST_H

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

/** Structure representing a trinarkular probe list */
typedef struct trinarkular_probelist trinarkular_probelist_t;

/** @} */

/**
 * @name Public Data Structures
 *
 * @{ */

/** @} */

/** Create a new Trinarkular Probelist object
 *
 * @return pointer to a probelist object if successful, NULL otherwise
 */
trinarkular_probelist_t *
trinarkular_probelist_create();

/** Destroy the given Trinarkular Probelist
 *
 * @param               pointer to the probelist to destroy
 */
void
trinarkular_probelist_destroy(trinarkular_probelist_t *pl);

/** Add a /24 to the probelist
 *
 * @param pl            probelist to add /24 to
 * @param network_ip    network address of the /24 to add (host byte order)
 * @param avg_resp_rate average response rate of hosts in this /24 [A(E(b))]
 * @return an ID (>=0) to be used when adding hosts to this /24 if successful,
 * -1 otherwise.
 *
 * If this /24 is already present in the probelist, the avg_resp_rate will be
 * updated and the original ID will be returned.
 */
int
trinarkular_probelist_add_slash24(trinarkular_probelist_t *pl,
                                  uint32_t network_ip,
                                  double avg_resp_rate);

/** Add a host to the given /24 in the probelist
 *
 * @param pl            probelist to add to
 * @param slash24_id    ID returned by trinarkular_probelist_add_slash24
 * @param host_ip       IP address of the host to add (host byte order)
 * @param resp_rate     The (recent) historical response rate for this host
 * @return 0 if the host was added successfully, -1 otherwise
 */
int trinarkular_probelist_slash24_add_host(trinarkular_probelist_t *pl,
                                           int slash24_id,
                                           uint32_t host_ip,
                                           double resp_rate);

/** How many /24s are in this probelist?
 *
 * @param pl            probelist to get /24 count for
 * @return the number of /24s in the probelist
 */
int
trinarkular_probelist_get_slash24_cnt(trinarkular_probelist_t *pl);

/** How many hosts are in this probelist?
 *
 * @param pl            probelist to get host count for
 * @return the number of hosts in the probelist
 */
int
trinarkular_probelist_get_host_cnt(trinarkular_probelist_t *pl);


#endif /* __TRINARKULAR_PROBELIST_H */
