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

#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "trinarkular_probe.h"

void trinarkular_probe_req_fprint(FILE *fh, trinarkular_probe_req_t *req)
{
  char ipbuf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &req->target_ip, ipbuf, INET_ADDRSTRLEN);
  fprintf(fh, "----- REQUEST -----\n");
  fprintf(fh, "target-ip:\t%s (%x)\n"
              "wait:\t%d\n"
              "-------------------\n\n",
          ipbuf, ntohl(req->target_ip), req->wait);
}

void trinarkular_probe_resp_fprint(FILE *fh, trinarkular_probe_resp_t *resp)
{
  char ipbuf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &resp->target_ip, ipbuf, INET_ADDRSTRLEN);
  fprintf(fh, "----- RESPONSE -----\n"
              "target-ip:\t%s (%x)\n"
              "verdict:\t%s\n"
              "rtt:\t%" PRIu32 "\n"
              "-------------------\n\n",
          ipbuf, ntohl(resp->target_ip),
          (resp->verdict == TRINARKULAR_PROBE_RESPONSIVE) ? "responsive"
                                                          : "unresponsive",
          resp->rtt);
}
