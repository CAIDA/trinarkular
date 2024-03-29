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

#include "config.h"

#include <assert.h>
#include <stdlib.h>

#include <czmq.h>

#include "utils.h"

#include "trinarkular_log.h"
#include "trinarkular_probe_io.h"

#define ASSERT_MORE                                                            \
  do {                                                                         \
    if (zsocket_rcvmore(src) == 0) {                                           \
      trinarkular_log("ERROR: Malformed message at line %d\n", __LINE__);      \
      goto err;                                                                \
    }                                                                          \
  } while (0)

#define BUFLEN 1024

#define SERIALIZE_VAL(from)                                                    \
  do {                                                                         \
    assert((len - written) >= sizeof(from));                                   \
    memcpy(ptr, &from, sizeof(from));                                          \
    s = sizeof(from);                                                          \
    written += s;                                                              \
    ptr += s;                                                                  \
  } while (0)

#define DESERIALIZE_VAL(to)                                                    \
  do {                                                                         \
    assert((len - read) >= sizeof(to));                                        \
    memcpy(&to, buf, sizeof(to));                                              \
    s = sizeof(to);                                                            \
    read += s;                                                                 \
    buf += s;                                                                  \
  } while (0)

char *trinarkular_probe_recv_str(void *src, int flags)
{
  zmq_msg_t llm;
  size_t len;
  char *str = NULL;

  if (zmq_msg_init(&llm) == -1 || zmq_msg_recv(&llm, src, 0) == -1) {
    goto err;
  }
  len = zmq_msg_size(&llm);
  if ((str = malloc(len + 1)) == NULL) {
    goto err;
  }
  memcpy(str, zmq_msg_data(&llm), len);
  str[len] = '\0';
  zmq_msg_close(&llm);

  return str;

err:
  free(str);
  return NULL;
}

int trinarkular_probe_req_send(void *dst, trinarkular_probe_req_t *req)
{
  assert(dst != NULL);
  assert(req != NULL);

  uint8_t buf[BUFLEN];
  uint8_t *ptr = buf;
  size_t len = BUFLEN;
  size_t written = 0;
  size_t s;

  // send the command type ("REQ")
  if (zmq_send(dst, "REQ", strlen("REQ"), ZMQ_SNDMORE) != strlen("REQ")) {
    trinarkular_log("ERROR: Could not send request command");
    return -1;
  }

  // send the actual request

  // target ip (already in network order)
  SERIALIZE_VAL(req->target_ip);

  // wait
  SERIALIZE_VAL(req->wait);

  // send the buffer
  if (zmq_send(dst, buf, written, 0) != written) {
    trinarkular_log("ERROR: Could not send request message");
    return -1;
  }

  return 0;
}

int trinarkular_probe_req_recv(void *src, trinarkular_probe_req_t *req)
{
  zmq_msg_t msg;
  uint8_t *buf;
  size_t len;
  size_t read = 0;
  size_t s = 0;

  ASSERT_MORE;
  if (zmq_msg_init(&msg) == -1 || zmq_msg_recv(&msg, src, 0) == -1) {
    fprintf(stderr, "Could not receive req message\n");
    goto err;
  }
  assert(zsocket_rcvmore(src) == 0);
  buf = zmq_msg_data(&msg);
  len = zmq_msg_size(&msg);
  read = 0;
  s = 0;

  // the actual request

  // target ip (already in network order)
  DESERIALIZE_VAL(req->target_ip);

  // wait
  DESERIALIZE_VAL(req->wait);

  return 0;

err:
  return -1;
}

int trinarkular_probe_resp_send(void *dst, trinarkular_probe_resp_t *resp)
{
  uint8_t buf[BUFLEN];
  uint8_t *ptr = buf;
  size_t len = BUFLEN;
  size_t written = 0;
  size_t s;

  //uint64_t u32;

  // send the command type ("RESP")
  if (zmq_send(dst, "RESP", strlen("RESP"), ZMQ_SNDMORE) != strlen("RESP")) {
    trinarkular_log("ERROR: Could not send response command");
    return -1;
  }

  // target ip (already in network order)
  SERIALIZE_VAL(resp->target_ip);

  // verdict
  SERIALIZE_VAL(resp->verdict);

  // rtt
  //u32 = htonl(resp->rtt);
  //SERIALIZE_VAL(u32);

  // send the buffer
  if (zmq_send(dst, buf, written, 0) != written) {
    trinarkular_log("ERROR: Could not send request message");
    return -1;
  }

  return 0;
}

int trinarkular_probe_resp_recv(void *src, trinarkular_probe_resp_t *resp)
{
  zmq_msg_t msg;
  uint8_t *buf;
  size_t len;
  size_t read = 0;
  size_t s = 0;

  ASSERT_MORE;
  if (zmq_msg_init(&msg) == -1 || zmq_msg_recv(&msg, src, 0) == -1) {
    fprintf(stderr, "Could not receive resp message\n");
    goto err;
  }
  assert(zsocket_rcvmore(src) == 0);
  buf = zmq_msg_data(&msg);
  len = zmq_msg_size(&msg);
  read = 0;
  s = 0;

  // target ip (already in network order)
  DESERIALIZE_VAL(resp->target_ip);

  // verdict
  DESERIALIZE_VAL(resp->verdict);

  // rtt
  //DESERIALIZE_VAL(resp->rtt);
  //resp->rtt = ntohl(resp->rtt);

  return 0;

err:
  return -1;
}
