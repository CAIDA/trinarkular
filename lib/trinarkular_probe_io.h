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

#ifndef __TRINARKULAR_PROBE_IO_H
#define __TRINARKULAR_PROBE_IO_H

#include "trinarkular_probe.h"

/** @file
 *
 * @brief Header file that exposes the internal zmq io interface of the probe
 * objects.
 *
 * @author Alistair King
 *
 */

/** Receive a string from the given socket
 *
 * @param src           socket to receive from
 * @param flags         ZMQ flags to be passed to zmq_msg_recv
 * @return dynamically allocated string if successful, NULL otherwise
 */
char *trinarkular_probe_recv_str(void *src, int flags);

/** Send the given probe request over the given socket
 *
 * @param dst           socket to send the request over
 * @param seq_num       sequence number of the request
 * @param req           pointer to the request to send
 * @return 0 if the request was sent, -1 otherwise
 */
int
trinarkular_probe_req_send(void *dst, uint64_t seq_num,
                           trinarkular_probe_req_t *req);

/** Receive a probe request from the given socket
 *
 * @param src           socket to receive the request from
 * @param req           pointer to the request to receive into
 * @return the sequence number of the request if successful, 0 otherwise
 */
uint64_t
trinarkular_probe_req_recv(void *src, trinarkular_probe_req_t *req);

/** Send the given probe response over the given socket
 *
 * @param dst           socket to send the response over
 * @param resp          pointer to the response to send
 * @return 0 if the response was sent, -1 otherwise
 */
int
trinarkular_probe_resp_send(void *dst, trinarkular_probe_resp_t *resp);

/** Receive a probe response from the given socket
 *
 * @param src           socket to receive the response from
 * @param resp          pointer to the response to receive into
 * @return 0 if the response was received, -1 otherwise
 */
int
trinarkular_probe_resp_recv(void *src, trinarkular_probe_resp_t *resp);

#endif /* __TRINARKULAR_PROBE_IO_H */
