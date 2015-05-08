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

#include <stdint.h>
#include <stdio.h>

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
  uint64_t seq_num;

  // TODO: add response type (timeout etc)

  /** The IP that was probed (network byte order) */
  uint32_t target_ip;

  /** The elapsed time in msec between probing and response */
  uint64_t rtt;

  // TODO: add other fields here

} trinarkular_probe_resp_t;


/** Print a human-readable version of the given request to the given file handle
 *
 * @param fh            file handle to write to (e.g. stdout)
 * @param req           pointer to the request to dump
 * @param seq_num       sequence number of the request
 *
 * If seq_num is 0 it will not be printed.
 */
void
trinarkular_probe_req_fprint(FILE *fh, trinarkular_probe_req_t *req,
                             uint64_t seq_num);

/** Print a human-readable version of the given response to the given file
 * handle
 *
 * @param fh            file handle to write to (e.g. stdout)
 * @param resp          pointer to the response to dump
 */
void
trinarkular_probe_resp_fprint(FILE *fh, trinarkular_probe_resp_t *resp);

#endif /* __TRINARKULAR_PROBE_H */
