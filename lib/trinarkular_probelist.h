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

#include <stdint.h>

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

/** Structure representing a target /24 within a probe list */
typedef struct trinarkular_probelist_slash24 trinarkular_probelist_slash24_t;

/** Structure representing a target host within a target /24 in the probe
    list */
typedef struct trinarkular_probelist_host trinarkular_probelist_host_t;

/** User data destructor */
typedef void (trinarkular_probelist_user_destructor_t)(void *user);

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

/** Create a Trinarkular Probelist object from a file
 *
 * @param filename      path of the probelist file to read
 * @return pointer to a probelist object if successful, NULL otherwise
 */
trinarkular_probelist_t *
trinarkular_probelist_create_from_file(const char *filename);

/** Destroy the given Trinarkular Probelist
 *
 * @param pl            pointer to the probelist to destroy
 */
void
trinarkular_probelist_destroy(trinarkular_probelist_t *pl);

/** Add a /24 to the probelist
 *
 * @param pl            probelist to add /24 to
 * @param network_ip    network address of the /24 to add (host byte order)
 * @param avg_resp_rate average response rate of hosts in this /24 [A(E(b))]
 * @return borrowed reference to the /24 added if successful, NULL otherwise
 *
 * If this /24 is already present in the probelist, the avg_resp_rate will be
 * updated.
 */
trinarkular_probelist_slash24_t *
trinarkular_probelist_add_slash24(trinarkular_probelist_t *pl,
                                  uint32_t network_ip,
                                  double avg_resp_rate);

/** Add a host to the given /24 in the probelist
 *
 * @param pl            probelist to add to
 * @param s24           pointer to the /24 to add the host to
 * @param host_ip       IP address of the host to add (host byte order)
 * @param resp_rate     The (recent) historical response rate for this host
 * @return borrowed reference to the host added if successful, NULL otherwise
 */
trinarkular_probelist_host_t *
trinarkular_probelist_slash24_add_host(trinarkular_probelist_t *pl,
                                       trinarkular_probelist_slash24_t *s24,
                                       uint32_t host_ip,
                                       double resp_rate);

/** Add string metadata to the given slash24
 *
 * @param pl            probelist to add to
 * @param s24           pointer to the /24 to add metadata to
 * @param md            metadata string to add
 * @return 0 if the metadata was added successfully, -1 otherwise
 */
int
trinarkular_probelist_slash24_add_metadata(trinarkular_probelist_t *pl,
                                           trinarkular_probelist_slash24_t *s24,
                                           const char *md);

/** Remove all metadata from the given /24
 *
 * @param pl            probelist to add to
 * @param s24           pointer to the /24 to remove metadata from
 */
void
trinarkular_probelist_slash24_remove_metadata(trinarkular_probelist_t *pl,
                                          trinarkular_probelist_slash24_t *s24);

/** Get the list of metadata associated with the given /24
 *
 * @param pl            probelist to add to
 * @param s24           pointer to the /24 to get metadata for
 * @param[out]          will be set to the number of metadata items returned
 * @return borrowed pointer to the list of metadata for the given /24
 */
char **
trinarkular_probelist_slash24_get_metadata(trinarkular_probelist_t *pl,
                                           trinarkular_probelist_slash24_t *s24,
                                           int *md_cnt);

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
uint64_t
trinarkular_probelist_get_host_cnt(trinarkular_probelist_t *pl);

/** Randomize the ordering of the /24s in the probelist
 *
 * @param pl            probelist to randomize
 * @param seed          randomizer seed
 * @return 0 if the /24s were randomized successfully, -1 otherwise
 *
 * If this function is not run, the order of /24s is undefined.
 *
 * @note if a /24 is added to the probelist, this function **must** be re-run
 */
int
trinarkular_probelist_randomize_slash24s(trinarkular_probelist_t *pl,
                                         int seed);

/** Reset the iterator to the first /24 in the probelist
 *
 * @param pl            pointer to the probelist to reset
 */
void
trinarkular_probelist_reset_slash24_iter(trinarkular_probelist_t *pl);

/** Get the next /24 from the probelist
 *
 * @param pl            pointer to the probelist
 * @return borrowed reference to the next /24 in the probelist or NULL
 * if no /24s remain
 */
trinarkular_probelist_slash24_t *
trinarkular_probelist_get_next_slash24(trinarkular_probelist_t *pl);

/** Are there more /24s left to iterate over?
 *
 * @param pl            pointer to the probelist
 * @return 1 if there are more /24s remaining, 0 otherwise
 */
int
trinarkular_probelist_has_more_slash24(trinarkular_probelist_t *pl);

/** Get the network IP of the current /24
 *
 * @param s24           pointer to the /24 to get the network IP from
 * @return network address (host byte order)
 */
uint32_t
trinarkular_probelist_get_network_ip(trinarkular_probelist_slash24_t *s24);

/** Get the average host response rate (aka. A(E(b)))
 *
 * @param s24           pointer to the /24 to get A(E(b)) for 
 * @return average host response rate of the /24
 */
float
trinarkular_probelist_get_aeb(trinarkular_probelist_slash24_t *s24);

/** Attach user data to the a /24
 *
 * @param pl            pointer to a probelist
 * @param s24           pointer to the /24 to attach user data to
 * @param destructor    pointer to the destructor function to use
 * @param user          pointer to user data to attach to the /24
 * @return 0 if the user data was attached successfully, -1 otherwise
 *
 * Although the destructor is passed with every call to set_user, the same
 * destructor function **must** be used for all user data. It is an error to set
 * different destructors
 */
int
trinarkular_probelist_set_slash24_user(trinarkular_probelist_t *pl,
                            trinarkular_probelist_slash24_t *s24,
                            trinarkular_probelist_user_destructor_t *destructor,
                            void *user);

/** Get the user data associated with the given /24
 *
 * @param s24           pointer to a /24
 * @return pointer to the user data if set, NULL otherwise
 */
void *
trinarkular_probelist_get_slash24_user(trinarkular_probelist_slash24_t *s24);

/** Randomize the ordering of the hosts in the given /24
 *
 * @param s24           pointer to the /24 to randomize hosts for
 * @param seed          randomizer seed
 * @return 0 if the hosts were randomized successfully, -1 otherwise
 *
 * If this function is not run, the order of hosts is undefined.
 *
 * @note if a host is added to the probelist, this function **must** be re-run
 */
int
trinarkular_probelist_slash24_randomize_hosts(
                                           trinarkular_probelist_slash24_t *s24,
                                           int seed);

/** Reset the iterator to the first host in the given /24
 *
 * @param s24           pointer to the /24 to reset the host iterator for
 */
void
trinarkular_probelist_reset_host_iter(trinarkular_probelist_slash24_t *s24);

/** Advance the iterator to the next host in the /24
 *
 * @param s24           pointer to the /24 to get the next host from
 * @return borrowed reference to the next host, NULL if no more hosts remain
 */
trinarkular_probelist_host_t *
trinarkular_probelist_get_next_host(trinarkular_probelist_slash24_t *s24);

/** Get the IP of the current host
 *
 * @param host          pointer to the host to get IP info from
 * @return IP address (host byte order) of the host
 */
uint32_t
trinarkular_probelist_get_host_ip(trinarkular_probelist_slash24_t *s24,
                                  trinarkular_probelist_host_t *host);

/** Attach user data to the current host
 *
 * @param pl            pointer to the probe list
 * @param host          pointer to the host to attache user data to
 * @param destructor    pointer to the destructor function to use
 * @param user          pointer to user data to attach to the /24
 * @return 0 if the user data was attached successfully, -1 otherwise
 *
 * Although the destructor is passed with every call to set_user, the same
 * destructor function **must** be used for all user data. It is an error to set
 * different destructors
 */
int
trinarkular_probelist_set_host_user(trinarkular_probelist_t *pl,
                            trinarkular_probelist_host_t *host,
                            trinarkular_probelist_user_destructor_t *destructor,
                            void *user);

/** Get the user data associated with the current host
 *
 * @param host          pointer to the host to get user data from
 * @return pointer to the user data if set, NULL otherwise
 */
void *
trinarkular_probelist_get_host_user(trinarkular_probelist_host_t *host);

#endif /* __TRINARKULAR_PROBELIST_H */
