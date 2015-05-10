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
      trinarkular_log("ERROR: Malformed view message at line %d\n", __LINE__); \
      goto err;                                                         \
    }                                                                   \
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

  // send the command type ("REQ")
  if (zmq_send(dst, "REQ", strlen("REQ"), ZMQ_SNDMORE) != strlen("REQ")) {
    trinarkular_log("ERROR: Could not send request command");
    return -1;
  }

  // send the sequence number
  seq_num = htonl(seq_num);
  if (zmq_send(dst, &seq_num, sizeof(seq_num), ZMQ_SNDMORE)
      != sizeof(seq_num)) {
    trinarkular_log("ERROR: Could not send request sequence number");
    return -1;
  }

  // send the actual request

  // target ip (already in network order)
  if(zmq_send(dst, &req->target_ip, sizeof(req->target_ip), ZMQ_SNDMORE)
     != sizeof(req->target_ip)) {
    trinarkular_log("ERROR: Could not send request target IP");
    return -1;
  }

  // probecount
  if(zmq_send(dst, &req->probecount, sizeof(req->probecount), ZMQ_SNDMORE)
     != sizeof(req->probecount)) {
    trinarkular_log("ERROR: Could not send request probecount");
    return -1;
  }

  // wait
  u32 = htonl(req->wait);
  if(zmq_send(dst, &u32, sizeof(u32), 0) // NO SNDMORE
     != sizeof(u32)) {
    trinarkular_log("ERROR: Could not send request wait");
    return -1;
  }

  return 0;
}

seq_num_t
trinarkular_probe_req_recv(void *src, trinarkular_probe_req_t *req)
{
  seq_num_t seq_num = 0;
  assert(src != NULL);
  assert(req != NULL);

  // recv the sequence number
  ASSERT_MORE;
  if(zmq_recv(src, &seq_num, sizeof(seq_num), 0) != sizeof(seq_num)) {
    trinarkular_log("ERROR: Could not receive req seq num");
    goto err;
  }
  seq_num = ntohl(seq_num);

  // recv the actual request

  // target ip (already in network order)
  ASSERT_MORE;
  if(zmq_recv(src, &req->target_ip, sizeof(req->target_ip), 0)
     != sizeof(req->target_ip)) {
    trinarkular_log("ERROR: Could not receive req target ip");
    goto err;
  }

  // probecount
  ASSERT_MORE;
  if(zmq_recv(src, &req->probecount, sizeof(req->probecount), 0)
     != sizeof(req->probecount)) {
    trinarkular_log("ERROR: Could not receive req probecount");
    goto err;
  }

  ASSERT_MORE;
  if(zmq_recv(src, &req->wait, sizeof(req->wait), 0)
     != sizeof(req->wait)) {
    trinarkular_log("ERROR: Could not receive req wait");
    goto err;
  }
  req->wait = ntohl(req->wait);

  assert(zsocket_rcvmore(src) == 0);

  return seq_num;

 err:
  return 0;
}

int
trinarkular_probe_resp_send(void *dst, trinarkular_probe_resp_t *resp)
{
  seq_num_t seq;
  uint64_t u64;
  assert(dst != NULL);
  assert(resp != NULL);

  // send the command type ("RESP")
  if (zmq_send(dst, "RESP", strlen("RESP"), ZMQ_SNDMORE) != strlen("RESP")) {
    trinarkular_log("ERROR: Could not send response command");
    return -1;
  }

  // send the sequence number
  seq = htonl(resp->seq_num);
  if (zmq_send(dst, &seq, sizeof(seq), ZMQ_SNDMORE)
      != sizeof(seq)) {
    trinarkular_log("ERROR: Could not send response sequence number");
    return -1;
  }

  // TODO: replace multi-messages with byte array (a la bgpwatcher view)

  // target ip (already in network order)
  if(zmq_send(dst, &resp->target_ip, sizeof(resp->target_ip), ZMQ_SNDMORE)
     != sizeof(resp->target_ip)) {
    trinarkular_log("ERROR: Could not send response target IP");
    return -1;
  }

  // verdict
  if(zmq_send(dst, &resp->verdict, sizeof(uint8_t), ZMQ_SNDMORE)
     != sizeof(uint8_t)) {
    trinarkular_log("ERROR: Could not send response verdict");
    return -1;
  }

  // rtt
  u64 = htonll(resp->rtt);
  if (zmq_send(dst, &u64, sizeof(uint64_t), ZMQ_SNDMORE)
      != sizeof(uint64_t)) {
    trinarkular_log("ERROR: Could not send response rtt");
    return -1;
  }

  // probes sent
  if(zmq_send(dst, &resp->probes_sent, sizeof(uint8_t), 0)
     != sizeof(uint8_t)) {
    trinarkular_log("ERROR: Could not send response probe count");
    return -1;
  }

  return 0;
}

int
trinarkular_probe_resp_recv(void *src, trinarkular_probe_resp_t *resp)
{
  assert(src != NULL);
  assert(resp != NULL);

  // recv the sequence number
  ASSERT_MORE;
  if(zmq_recv(src, &resp->seq_num, sizeof(resp->seq_num), 0)
     != sizeof(resp->seq_num)) {
    trinarkular_log("ERROR: Could not receive response seq num");
    goto err;
  }
  resp->seq_num = ntohl(resp->seq_num);

  // target ip (already in network order)
  ASSERT_MORE;
  if(zmq_recv(src, &resp->target_ip, sizeof(resp->target_ip), 0)
     != sizeof(resp->target_ip)) {
    trinarkular_log("ERROR: Could not receive response target ip");
    goto err;
  }

  // verdict
  ASSERT_MORE;
  resp->verdict = 0;
  if(zmq_recv(src, &resp->verdict, sizeof(uint8_t), 0)
     != sizeof(uint8_t)) {
    trinarkular_log("ERROR: Could not receive response verdict");
    goto err;
  }

  // rtt
  ASSERT_MORE;
  if(zmq_recv(src, &resp->rtt, sizeof(resp->rtt), 0) != sizeof(resp->rtt)) {
    trinarkular_log("ERROR: Could not receive response rtt");
    goto err;
  }
  resp->rtt = ntohll(resp->rtt);

  // probes_sent
  ASSERT_MORE;
  resp->probes_sent = 0;
  if(zmq_recv(src, &resp->probes_sent, sizeof(uint8_t), 0)
     != sizeof(uint8_t)) {
    trinarkular_log("ERROR: Could not receive response probe count");
    goto err;
  }

  return 0;

 err:
  return -1;
}
