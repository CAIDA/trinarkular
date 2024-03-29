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
#include "trinarkular.h"
#include "trinarkular_driver.h" // not included in trinarkular.h
#include "trinarkular_log.h"
#include "trinarkular_signal.h"
#include "config.h"
#include "utils.h"
#include "wandio_utils.h"
#include <assert.h>
#include <czmq.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wandio.h>

static trinarkular_driver_t *driver = NULL;

/** Indicates that we are waiting to shutdown */
volatile sig_atomic_t driver_shutdown = 0;

/** IP address list file handle */
io_t *infile = NULL;

/** The number of SIGINTs to catch before aborting */
#define HARD_SHUTDOWN 3

#define WAIT 3
#define TARGET_CNT 10

/** Handles SIGINT gracefully and shuts down */
static void catch_sigint(int sig)
{
  driver_shutdown++;
  if (driver_shutdown == HARD_SHUTDOWN) {
    fprintf(stderr, "caught %d SIGINT's. shutting down NOW\n", HARD_SHUTDOWN);
    exit(-1);
  }

  fprintf(stderr, "caught SIGINT, shutting down at the next opportunity\n");

  if (driver != NULL) {
    trinarkular_driver_destroy(driver);
  }

  signal(sig, catch_sigint);
}

static int get_ip(trinarkular_probe_req_t *req)
{
  char buf[1024];

  // grab a line of text from the file
  if (wandio_fgets(infile, buf, 1024, 1) == 0) {
    return 0;
  }

  // now convert the string to an integer
  inet_pton(AF_INET, buf, &req->target_ip);

  return 1;
}

static void usage(char *name)
{
  const char **driver_names = trinarkular_driver_get_driver_names();
  int i;
  assert(driver_names != NULL);

  fprintf(stderr, "Usage: %s [options] -d driver\n"
                  "       -d <driver>      driver to use for probes\n"
                  "                        options are:\n",
          name);

  for (i = 0; i <= TRINARKULAR_DRIVER_ID_MAX; i++) {
    if (driver_names[i] != NULL) {
      fprintf(stderr, "                          - %s\n", driver_names[i]);
    }
  }

  fprintf(stderr,
          "       -f <first-ip>    first IP to probe (default: random)\n"
          "       -i <wait>        sec to wait between probes (default: %d)\n"
          "       -l <ip-file>     list of IP addresses to probe\n"
          "       -t <targets>     number of targets to probe (default: %d)\n",
          WAIT, TARGET_CNT);
}

static void cleanup()
{
  trinarkular_driver_destroy(driver);
  driver = NULL;

  if (infile != NULL) {
    wandio_destroy(infile);
    infile = NULL;
  }
}

int main(int argc, char **argv)
{
  int opt, prevoptind;

  char *driver_name = NULL;
  char *driver_arg_ptr = NULL;

  int ret;
  trinarkular_probe_req_t req;
  int req_cnt = 0;
  int target_cnt = TARGET_CNT;

  trinarkular_probe_resp_t resp;
  int resp_cnt = 0;

  int responsive_count = 0;
  int probe_count = 0;

  int first_addr_set = 0;

  char *file = NULL;

  signal(SIGINT, catch_sigint);

  // set defaults for the request
  req.wait = WAIT;

  while (prevoptind = optind,
         (opt = getopt(argc, argv, ":c:d:f:i:l:t:v?")) >= 0) {
    if (optind == prevoptind + 2 && optarg && *optarg == '-' &&
        *(optarg + 1) != '\0') {
      opt = ':';
      --optind;
    }
    switch (opt) {
    case 'd':
      driver_name = strdup(optarg);
      assert(driver_name != NULL);
      break;

    case 'f':
      inet_pton(AF_INET, optarg, &req.target_ip);
      first_addr_set = 1;
      break;

    case 'i':
      req.wait = atoi(optarg);
      break;

    case 'l':
      file = optarg;
      break;

    case 't':
      target_cnt = atoi(optarg);
      break;

    case ':':
      fprintf(stderr, "ERROR: Missing option argument for -%c\n", optopt);
      usage(argv[0]);
      return -1;
      break;

    case '?':
    case 'v':
      fprintf(stderr, "trinarkular version %d.%d.%d\n",
              TRINARKULAR_MAJOR_VERSION, TRINARKULAR_MID_VERSION,
              TRINARKULAR_MINOR_VERSION);
      usage(argv[0]);
      goto err;
      break;

    default:
      usage(argv[0]);
      goto err;
    }
  }

  /* reset getopt for drivers to use */
  optind = 1;

  if (driver_name == NULL) {
    fprintf(stderr, "ERROR: Driver name must be specifed using -d\n");
    usage(argv[0]);
    goto err;
  }

  if (first_addr_set != 0 && file != NULL) {
    trinarkular_log("WARN: first-addr and file set. Ignoring first-addr");
  }

  /* the driver_name string will contain the name of the driver, optionally
     followed by a space and then the arguments to pass to the driver */
  if ((driver_arg_ptr = strchr(driver_name, ' ')) != NULL) {
    /* set the space to a nul, which allows driver_name to be used for the
       provider name, and then increment driver_arg_ptr to point to the next
       character, which will be the start of the arg string (or at worst case,
       the terminating \0 */
    *driver_arg_ptr = '\0';
    driver_arg_ptr++;
  }

  if ((driver = trinarkular_driver_create_by_name(driver_name,
                                                  driver_arg_ptr)) == NULL) {
    usage(argv[0]);
    goto err;
  }

  srand(zclock_time());

  if (file == NULL) {
    if (first_addr_set == 0) {
      req.target_ip = rand() % (((uint64_t)1 << 32) - 1);
    }

    // queue a bunch of measurements
    for (req_cnt = 0; req_cnt < target_cnt; req_cnt++) {
      if ((ret = trinarkular_driver_queue_req(driver, &req)) < 0) {
        trinarkular_log("ERROR: Could not queue probe request");
        goto err;
      }
      trinarkular_probe_req_fprint(stdout, &req);

      // create the next ip address
      if (first_addr_set) {
        req.target_ip = htonl(ntohl(req.target_ip) + 1);
      } else {
        req.target_ip = rand() % (((uint64_t)1 << 32) - 1);
      }
    }
  } else {
    if ((infile = wandio_create(file)) == NULL) {
      trinarkular_log("ERROR: Could not open %s for reading", file);
      goto err;
    }

    while (req_cnt < target_cnt && (ret = get_ip(&req)) > 0) {
      if (ret < 0) {
        goto err;
      }

      if (trinarkular_driver_queue_req(driver, &req) < 0) {
        trinarkular_log("ERROR: Could not queue probe request");
        goto err;
      }

      req_cnt++;
    }
  }

  trinarkular_log("INFO: Queued %d requests, waiting for responses", req_cnt);

  // do blocking recv's until all replies are received
  for (resp_cnt = 0; resp_cnt < req_cnt; resp_cnt++) {
    if (trinarkular_driver_recv_resp(driver, &resp, 1) != 1) {
      trinarkular_log("Could not receive response");
      goto err;
    }

    // lets dump out the responses if there aren't too many
    if (req_cnt < 100) {
      trinarkular_probe_resp_fprint(stdout, &resp);
    }

    responsive_count += resp.verdict;
    probe_count++;
  }

  assert(resp_cnt == req_cnt);

  trinarkular_log("done probing");

  fprintf(stdout, "\n----- SUMMARY -----\n"
                  "Responsive Targets: %d/%d (%0.0f%%)\n"
                  "Responsive Probes: %d/%d (%0.0f%%)\n"
                  "-------------------\n",
          responsive_count, target_cnt, responsive_count * 100.0 / req_cnt,
          responsive_count, probe_count,
          responsive_count * 100.0 / probe_count);

  cleanup();
  return 0;

err:
  cleanup();
  return -1;
}
