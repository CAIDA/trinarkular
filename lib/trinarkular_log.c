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
#include <stdarg.h>
#include <stdio.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include "utils.h"

#include "trinarkular_log.h"

static char *timestamp_str(char *buf, const size_t len)
{
  struct timeval tv;
  struct tm *tm;
  int ms;
  time_t t;

  buf[0] = '\0';
  gettimeofday_wrap(&tv);
  t = tv.tv_sec;
  if ((tm = localtime(&t)) == NULL)
    return buf;

  ms = tv.tv_usec / 1000;
  snprintf(buf, len, "[%04d-%02d-%02d %02d:%02d:%02d:%03d] ",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour,
           tm->tm_min, tm->tm_sec, ms);

  return buf;
}

void _trinarkular_log(const char *func, const char *format, ...)
{
  char message[512];
  char ts[27];
  char fs[64];
  va_list ap;
  va_start(ap, format);

  assert(format != NULL);

  vsnprintf(message, sizeof(message), format, ap);

  timestamp_str(ts, sizeof(ts));

  if (func != NULL) {
    snprintf(fs, sizeof(fs), "%s: ", func);
  } else {
    fs[0] = '\0';
  }

  fprintf(stderr, "%s%s%s\n", ts, fs, message);
  fflush(stderr);
  va_end(ap);
}
