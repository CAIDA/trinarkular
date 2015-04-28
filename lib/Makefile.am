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
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/common

lib_LTLIBRARIES = libtrinarkular.la

include_HEADERS = trinarkular.h

libtrinarkular_la_SOURCES = 		\
	trinarkular.c 			\
	trinarkular.h			\
	trinarkular_int.h

libtrinarkular_la_LIBADD = $(top_builddir)/common/libcccommon.la

libtrinarkular_la_LDFLAGS = -version-info @TRINARKULAR_MAJOR_VERSION@:@TRINARKULAR_MID_VERSION@:@TRINARKULAR_MINOR_VERSION@

ACLOCAL_AMFLAGS = -I m4

CLEANFILES = *~
