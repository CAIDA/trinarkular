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

SUBDIRS =

AM_CPPFLAGS = 	-I$(top_srcdir)			\
		-I$(top_srcdir)/common 		\
		-I$(top_srcdir)/lib

noinst_LTLIBRARIES = libtrinarkular_drivers.la

EXTRA_DIST = \
	scamper-trinarkular.patch

include_HEADERS =

DRIVER_SRCS=
DRIVER_LIBS=

# Test Driver
DRIVER_SRCS += \
	trinarkular_driver_test.c \
	trinarkular_driver_test.h

if WITH_SCAMPER
# Scamper driver
DRIVER_SRCS += \
	trinarkular_driver_scamper.c \
	trinarkular_driver_scamper.h
endif

# -- sample how to add conditional driver
#if WITH_<NAME>
#SUBDIRS += lib<name>
#AM_CPPFLAGS += -I$(top_srcdir)/lib/drivers/lib<name>
#DRIVER_SRCS += \
#	trinarkular_driver_<name>.c \
#	trinarkular_driver_<name>.h
#DRIVER_LIBS += $(top_builddir)/lib/drivers/lib<name>/lib<name>.la
#endif

libtrinarkular_drivers_la_SOURCES = $(DRIVER_SRCS)

libtrinarkular_drivers_la_LIBADD = $(DRIVER_LIBS)

ACLOCAL_AMFLAGS = -I m4

CLEANFILES = *~
