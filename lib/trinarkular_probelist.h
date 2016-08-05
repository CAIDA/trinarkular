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
 * @brief Header file that exposes the public interface of the trinarkular
 * probelist
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

typedef struct trinarkular_slash24_metrics {

  /** (kp idx) Value will be the 0-100 belief value for the given /24 */
  int32_t belief;

  /** (kp idx) Value will be 0 (uncertain), 1 (down), or 2 (up) */
  int32_t state;

  /** (shared kp idx) Value will be # /24s in each state */
  int32_t overall[3]; // UNCERTAIN, DOWN, UP

} __attribute__((packed)) trinarkular_slash24_metrics_t;

/** Structure representing prober state for a /24
 *
 * NB: When modifying fields here, you MUST also modify the copy_state function
 * in trinarkular_probelist.c
 */
typedef struct trinarkular_slash24_state {

  /** Index of the current host in the slash24 */
  uint8_t current_host;

  /** What was the type of the last probe sent to this /24? */
  uint8_t last_probe_type;

  /** The number of additional probes that can be sent this round.
   * Since AK is a masochist, the bottom 4 bits are used for the adaptive probe
   * budget and the top 4 are used for the recovery probe budget.  Use the
   * ADAPTIVE_BUDGET and RECOVERY_BUDGET macros...
   */
  uint8_t probe_budget;

  /** The current belief value for this /24 */
  float current_belief;

  /** Last stable state for this /24 (Warning: this **may not** match
      BELIEF_STATE(current_belief) if adaptive probing has been used) */
  uint8_t current_state;

  /** How many rounds has it been since this /24 was UP? The value is
      incremented *before* sending a periodic probe. Once the value reaches
      255, it is reset to RECOVERY_BACKOFF_MAX to avoid wrapping */
  uint8_t rounds_since_up;

  /** Set of timeseries associated with this /24 (one per metadata)*/
  trinarkular_slash24_metrics_t *metrics;

  /** Number of metric sets */
  uint8_t metrics_cnt;

} __attribute__((packed)) trinarkular_slash24_state_t;

#define ADAPTIVE_BUDGET(s24state) ((s24state)->probe_budget & 0x0f)

#define ADAPTIVE_BUDGET_SET(s24state, val)                                     \
  (s24state)->probe_budget = ((s24state)->probe_budget & 0xf0) | ((val)&0x0f)

#define RECOVERY_BUDGET(s24state) (((s24state)->probe_budget >> 4) & 0xf)

#define RECOVERY_BUDGET_SET(s24state, val)                                     \
  (s24state)->probe_budget =                                                   \
    ((s24state)->probe_budget & 0x0f) | (((val)&0x0f) << 4)

typedef struct trinarkular_slash24 {

  /** The network IP (first IP) of this /24 (in host byte order) */
  uint32_t network_ip;

  /** List of target host bytes for this /24 (should be OR'd with the network
      IP) */
  uint8_t *hosts;

  /** Number of host bytes */
  uint16_t hosts_cnt;

  /** The average response rate of recently responding hosts in this /24
   * (I.e. the A(E(b)) value from the paper) */
  float aeb;

  /** List of metadata */
  char **md;

  /** Number of items in metadata list */
  uint8_t md_cnt;

} __attribute__((packed)) trinarkular_slash24_t;

/** @} */

/** Create a new Trinarkular Probelist object
 *
 * @return pointer to a probelist object if successful, NULL otherwise
 */
trinarkular_probelist_t *trinarkular_probelist_create(const char *filename);

/** Destroy the given Trinarkular Probelist
 *
 * @param pl            pointer to the probelist to destroy
 */
void trinarkular_probelist_destroy(trinarkular_probelist_t *pl);

/** Get the version of the current probelist
 *
 * @param pl            pointer to the probelist to set version for
 * @return probelist version string
 */
char *trinarkular_probelist_get_version(trinarkular_probelist_t *pl);

/** How many /24s are in this probelist?
 *
 * @param pl            probelist to get /24 count for
 * @return the number of /24s in the probelist
 */
int trinarkular_probelist_get_slash24_cnt(trinarkular_probelist_t *pl);

/** Reset the iterator to the first /24 in the probelist
 *
 * @param pl            pointer to the probelist to reset
 */
void trinarkular_probelist_reset_slash24_iter(trinarkular_probelist_t *pl);

/** Get the next /24 from the probelist
 *
 * @param pl            pointer to the probelist
 * @return borrowed reference to the next /24 in the probelist or NULL
 * if no /24s remain
 */
trinarkular_slash24_t *
trinarkular_probelist_get_next_slash24(trinarkular_probelist_t *pl);

/** Are there more /24s left to iterate over?
 *
 * @param pl            pointer to the probelist
 * @return 1 if there are more /24s remaining, 0 otherwise
 */
int trinarkular_probelist_has_more_slash24(trinarkular_probelist_t *pl);

/** Get the given /24 from the probelist
 *
 * @param pl            pointer to the probelist
 * @param network_ip    network IP of the /24 to retrieve (host byte order)
 * @return borrowed reference to the next /24 in the probelist or NULL
 * if no /24s remain
 */
trinarkular_slash24_t *
trinarkular_probelist_get_slash24(trinarkular_probelist_t *pl,
                                  uint32_t network_ip);

/** Create a /24 state structure
 *
 * @param metrics_cnt   number of metrics to initialize
 * @return pointer to the state structure created if successful, NULL otherwise
 */
trinarkular_slash24_state_t *trinarkular_slash24_state_create(int metrics_cnt);

/** Destroy the given /24 state structure
 *
 * @param state         pointer to the state structure to destroy
 *
 * Be careful to only call this for state structures that you own. The
 * _get_slash24_state function returns a borrowed pointer that does not need to
 * be freed.
 */
void trinarkular_slash24_state_destroy(trinarkular_slash24_state_t *state);

/** Save state for the given /24
 *
 * @param pl            pointer to a probelist
 * @param s24           pointer to the /24 to save state for
 * @param state         pointer to state to set
 * @return 0 if the state was set successfully, -1 otherwise
 */
int trinarkular_probelist_save_slash24_state(
  trinarkular_probelist_t *pl, trinarkular_slash24_t *s24,
  trinarkular_slash24_state_t *state);

/** Get the state associated with the given /24
 *
 * @param s24           pointer to a /24
 * @return borrowed pointer to the state if set, NULL otherwise
 */
void *trinarkular_probelist_get_slash24_state(trinarkular_probelist_t *pl,
                                              trinarkular_slash24_t *s24);

// Helper functions

uint32_t
trinarkular_probelist_get_next_host(trinarkular_slash24_t *s24,
                                    trinarkular_slash24_state_t *state);

#endif /* __TRINARKULAR_PROBELIST_H */
