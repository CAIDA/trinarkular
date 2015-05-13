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

#include "config.h"

#include <assert.h>
#include <stdlib.h>

#include <czmq.h>

#include "utils.h"

#include "trinarkular_log.h"
#include "trinarkular_probe_io.h"

#define ASSERT_MORE                                                     \
  do {                                                                  \
    if(zsocket_rcvmore(src) == 0) {                                     \
      trinarkular_log("ERROR: Malformed message at line %d\n", __LINE__); \
      goto err;                                                         \
    }                                                                   \
  } while(0)

#define BUFLEN 1024

#define SERIALIZE_VAL(from)				\
  do {							\
    assert((len-written) >= sizeof(from));		\
    memcpy(ptr, &from, sizeof(from));			\
    s = sizeof(from);					\
    written += s;					\
    ptr += s;						\
  } while(0)

#define DESERIALIZE_VAL(to)				\
  do {							\
    assert((len-read) >= sizeof(to));			\
    memcpy(&to, buf, sizeof(to));			\
    s = sizeof(to);					\
    read += s;						\
    buf += s;						\
  } while(0)

char *trinarkular_probe_recv_str(void *src, int flags)
{
  zmq_msg_t llm;
  size_t len;
  char *str = NULL;

  if(zmq_msg_init(&llm) == -1 || zmq_msg_recv(&llm, src, 0) == -1)
    {
      goto err;
    }
  len = zmq_msg_size(&llm);
  if((str = malloc(len + 1)) == NULL)
    {
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

int
trinarkular_probe_req_send(void *dst, seq_num_t seq_num,
                           trinarkular_probe_req_t *req)
{
  assert(dst != NULL);
  assert(seq_num != 0);
  assert(req != NULL);

  uint32_t u32;

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

  // send the sequence number
  seq_num = htonl(seq_num);
  SERIALIZE_VAL(seq_num);

  // send the actual request

  // target ip (already in network order)
  SERIALIZE_VAL(req->target_ip);

  // probecount (1 byte)
  SERIALIZE_VAL(req->probecount);

  // wait
  u32 = htonl(req->wait);
  SERIALIZE_VAL(u32);

  // send the buffer
  if(zmq_send(dst, buf, written, 0) != written) {
    trinarkular_log("ERROR: Could not send request message");
    return -1;
  }

  return 0;
}

seq_num_t
trinarkular_probe_req_recv(void *src, trinarkular_probe_req_t *req)
{
  zmq_msg_t msg;
  uint8_t *buf;
  size_t len;
  size_t read = 0;
  size_t s = 0;

  seq_num_t seq_num = 0;
  assert(src != NULL);
  assert(req != NULL);

  ASSERT_MORE;
  if(zmq_msg_init(&msg) == -1 || zmq_msg_recv(&msg, src, 0) == -1) {
    fprintf(stderr, "Could not receive req message\n");
    goto err;
  }
  assert(zsocket_rcvmore(src) == 0);
  buf = zmq_msg_data(&msg);
  len = zmq_msg_size(&msg);
  read = 0;
  s = 0;

  // recv the sequence number
  DESERIALIZE_VAL(seq_num);
  seq_num = ntohl(seq_num);

  // the actual request

  // target ip (already in network order)
  DESERIALIZE_VAL(req->target_ip);

  // probecount (1 byte)
  DESERIALIZE_VAL(req->probecount);

  // wait
  DESERIALIZE_VAL(req->wait);
  req->wait = ntohl(req->wait);

  return seq_num;

 err:
  return 0;
}

int
trinarkular_probe_resp_send(void *dst, trinarkular_probe_resp_t *resp)
{
  uint8_t buf[BUFLEN];
  uint8_t *ptr = buf;
  size_t len = BUFLEN;
  size_t written = 0;
  size_t s;

  seq_num_t seq;
  uint64_t u64;
  uint8_t u8;
  assert(dst != NULL);
  assert(resp != NULL);

  // send the command type ("RESP")
  if (zmq_send(dst, "RESP", strlen("RESP"), ZMQ_SNDMORE) != strlen("RESP")) {
    trinarkular_log("ERROR: Could not send response command");
    return -1;
  }

  // send the sequence number
  seq = htonl(resp->seq_num);
  SERIALIZE_VAL(seq);

  // target ip (already in network order)
  SERIALIZE_VAL(resp->target_ip);

  // verdict
  u8 = resp->verdict;
  SERIALIZE_VAL(u8);

  // rtt
  u64 = htonll(resp->rtt);
  SERIALIZE_VAL(u64);

  // probes sent
  u8 = resp->probes_sent;
  SERIALIZE_VAL(u8);

  // send the buffer
  if(zmq_send(dst, buf, written, 0) != written) {
    trinarkular_log("ERROR: Could not send request message");
    return -1;
  }

  return 0;
}

int
trinarkular_probe_resp_recv(void *src, trinarkular_probe_resp_t *resp)
{
  zmq_msg_t msg;
  uint8_t *buf;
  size_t len;
  size_t read = 0;
  size_t s = 0;

  uint8_t u8;

  assert(src != NULL);
  assert(resp != NULL);

  ASSERT_MORE;
  if(zmq_msg_init(&msg) == -1 || zmq_msg_recv(&msg, src, 0) == -1) {
    fprintf(stderr, "Could not receive resp message\n");
    goto err;
  }
  assert(zsocket_rcvmore(src) == 0);
  buf = zmq_msg_data(&msg);
  len = zmq_msg_size(&msg);
  read = 0;
  s = 0;

  // recv the sequence number
  DESERIALIZE_VAL(resp->seq_num);
  resp->seq_num = ntohl(resp->seq_num);

  // target ip (already in network order)
  DESERIALIZE_VAL(resp->target_ip);

  // verdict
  DESERIALIZE_VAL(u8);
  resp->verdict = u8;

  // rtt
  DESERIALIZE_VAL(resp->rtt);
  resp->rtt = ntohll(resp->rtt);

  // probes_sent
  DESERIALIZE_VAL(u8);
  resp->probes_sent = u8;

  return 0;

 err:
  return -1;
}
