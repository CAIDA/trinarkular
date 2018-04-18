#
# This file is part of trinarkular
#
# Copyright (C) 2015 The Regents of the University of California.
# Authors: Alistair King
#
# All rights reserved.
#
# This code has been developed by CAIDA at UC San Diego.
# For more information, contact alistair@caida.org
#
# This source code is proprietary to the CAIDA group at UC San Diego and may
# not be redistributed, published or disclosed without prior permission from
# CAIDA.
#
# Report any bugs, questions or comments to alistair@caida.org
#

SUBDIRS = drivers
AM_CPPFLAGS = -I$(top_srcdir) \
	      -I$(top_srcdir)/common \
	      -I$(top_srcdir)/common/libjsmn \
	      -I$(top_srcdir)/lib/drivers

lib_LTLIBRARIES = libtrinarkular.la

include_HEADERS = 			\
	trinarkular.h			\
	trinarkular_driver.h		\
	trinarkular_probe.h		\
	trinarkular_probelist.h		\
	trinarkular_prober.h		\
	trinarkular_signal.h

libtrinarkular_la_SOURCES = 		\
	trinarkular.h			\
	trinarkular_driver.c		\
	trinarkular_driver.h		\
	trinarkular_driver_interface.h	\
	trinarkular_log.c		\
	trinarkular_log.h		\
	trinarkular_probe.h		\
	trinarkular_probe.c		\
	trinarkular_probe_io.c		\
	trinarkular_probe_io.h		\
	trinarkular_probelist.c		\
	trinarkular_probelist.h		\
	trinarkular_prober.c		\
	trinarkular_prober.h		\
	trinarkular_signal.c		\
	trinarkular_signal.h

libtrinarkular_la_LIBADD = $(top_builddir)/common/libcccommon.la \
			   $(top_builddir)/lib/drivers/libtrinarkular_drivers.la
libtrinarkular_la_LDFLAGS = -version-info @TRINARKULAR_MAJOR_VERSION@:@TRINARKULAR_MID_VERSION@:@TRINARKULAR_MINOR_VERSION@

ACLOCAL_AMFLAGS = -I m4

CLEANFILES = *~
