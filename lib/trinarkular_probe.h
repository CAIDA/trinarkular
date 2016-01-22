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

/* NB: if changing any of these structures, IO functions must also be updated */

/** Special sequence number indicating that the probe was dropped */
#define REQ_DROPPED UINT32_MAX

/** Structure used when making a probe request to a driver */
typedef struct trinarkular_probe_req {

  /** The target of the probe (network byte order) */
  uint32_t target_ip;

#if 0 /* 2016-03-17 -- AK decides that retrying probes doesn't make sense */
  /** Maximum number of probes to send */
  uint8_t probecount;
#endif

  /** Number of seconds to wait for a reply */
  uint16_t wait;

} __attribute__((packed)) trinarkular_probe_req_t;

/** The overall verdict of the probe */
typedef enum trinarkular_probe_resp_verdict {

  /** No responses were received to the probe packet(s) */
  TRINARKULAR_PROBE_UNRESPONSIVE = 0,

  /** At least one response was received to a probe packet */
  TRINARKULAR_PROBE_RESPONSIVE = 1,

} trinarkular_probe_resp_verdict_t;

/** Structure returned by driver when a probe is complete */
typedef struct trinarkular_probe_resp {

  /** The IP that was probed (network byte order) */
  uint32_t target_ip;

  /** The overall probe verdict */
  uint8_t verdict;

  /** The RTT of the first response received */
  uint32_t rtt;

#if 0
  /** The number of probes that were sent in total */
  uint8_t probes_sent;
#endif

} __attribute__((packed)) trinarkular_probe_resp_t;


/** Print a human-readable version of the given request to the given file handle
 *
 * @param fh            file handle to write to (e.g. stdout)
 * @param req           pointer to the request to dump
 */
void
trinarkular_probe_req_fprint(FILE *fh, trinarkular_probe_req_t *req);

/** Print a human-readable version of the given response to the given file
 * handle
 *
 * @param fh            file handle to write to (e.g. stdout)
 * @param resp          pointer to the response to dump
 */
void
trinarkular_probe_resp_fprint(FILE *fh, trinarkular_probe_resp_t *resp);

#endif /* __TRINARKULAR_PROBE_H */
