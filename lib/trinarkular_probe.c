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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <inttypes.h>

#include "trinarkular_probe.h"

void
trinarkular_probe_req_fprint(FILE *fh, trinarkular_probe_req_t *req,
                             seq_num_t seq_num)
{
  char ipbuf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &req->target_ip, ipbuf, INET_ADDRSTRLEN);
  fprintf(fh,
          "----- REQUEST -----\n");
  if (seq_num > 0) {
    fprintf(fh, "seq-num:\t%"PRIu32"\n", seq_num);
  }
  fprintf(fh,
          "target-ip:\t%s (%x)\n"
          "probe-count:\t%d\n"
          "wait:\t%d\n"
          "-------------------\n\n",
          ipbuf,
          ntohl(req->target_ip),
          req->probecount,
          req->wait);
}

void
trinarkular_probe_resp_fprint(FILE *fh, trinarkular_probe_resp_t *resp)
{
  char ipbuf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &resp->target_ip, ipbuf, INET_ADDRSTRLEN);
  fprintf(fh,
          "----- RESPONSE -----\n"
          "seq-num:\t%"PRIu32"\n"
          "target-ip:\t%s (%x)\n"
          "verdict:\t%s\n"
          "rtt:\t%"PRIu64"\n"
          "probes-sent:\t%d\n"
          "-------------------\n\n",
          resp->seq_num,
          ipbuf,
          ntohl(resp->target_ip),
          (resp->verdict == TRINARKULAR_PROBE_RESPONSIVE) ? "responsive" : "unresponsive",
          resp->rtt,
          resp->probes_sent);
}
