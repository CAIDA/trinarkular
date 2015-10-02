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

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/common -I$(top_srcdir)/lib

dist_bin_SCRIPTS =

bin_PROGRAMS = \
	trinarkular-manual-prober	\
	trinarkular-manual-driver

if WITH_PROBELIST_GEN
bin_PROGRAMS += trinarkular-gen-probelist

trinarkular_gen_probelist_SOURCES = \
	gen-probelist.c
trinarkular_gen_probelist_LDADD = -ltrinarkular
trinarkular_gen_probelist_LDFLAGS = -L$(top_builddir)/lib
endif

trinarkular_manual_prober_SOURCES = \
	manual-prober.c
trinarkular_manual_prober_LDADD = -ltrinarkular
trinarkular_manual_prober_LDFLAGS = -L$(top_builddir)/lib

trinarkular_manual_driver_SOURCES = \
	manual-driver.c
trinarkular_manual_driver_LDADD = -ltrinarkular
trinarkular_manual_driver_LDFLAGS = -L$(top_builddir)/lib

ACLOCAL_AMFLAGS = -I m4

CLEANFILES = *~
