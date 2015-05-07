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

#ifndef __TRINARKULAR_PROBE_H
#define __TRINARKULAR_PROBE_H

/** @file
 *
 * @brief Header file that exposes the public interface of the trinarkular probe
 * objects (both requests and responses)
 *
 * @author Alistair King
 *
 */

/** Structure used when making a probe request to a driver */
typedef struct trinarkular_probe_req {

  /** The target of the probe (network byte order) */
  uint32_t target_ip;

  // TODO: add fields here (look at scamper [ping] options)

} trinarkular_probe_req_t;

/** Structure returned by driver when a probe is complete */
typedef struct trinarkular_probe_resp {

  /** The sequence number of the request that generated this response */
  uint64_t seq;

  // TODO: add response type (timeout etc)

  /** The IP that was probed */
  uint32_t target_ip;

  /** The elapsed time between probing and response */
  uint32_t rtt;

  // TODO: add other fields here

} trinarkular_probe_resp_t;

#endif /* __TRINARKULAR_PROBE_H */
