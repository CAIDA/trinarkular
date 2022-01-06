#
# This file is part of trinarkular
#
# Copyright (C) 2015 The Regents of the University of California.
# Authors: Alistair King
#
# This software is Copyright (c) 2015 The Regents of the University of
# California. All Rights Reserved. Permission to copy, modify, and distribute this
# software and its documentation for academic research and education purposes,
# without fee, and without a written agreement is hereby granted, provided that
# the above copyright notice, this paragraph and the following three paragraphs
# appear in all copies. Permission to make use of this software for other than
# academic research and education purposes may be obtained by contacting:
#
# Office of Innovation and Commercialization
# 9500 Gilman Drive, Mail Code 0910
# University of California
# La Jolla, CA 92093-0910
# (858) 534-5815
# invent@ucsd.edu
#
# This software program and documentation are copyrighted by The Regents of the
# University of California. The software program and documentation are supplied
# "as is", without any accompanying services from The Regents. The Regents does
# not warrant that the operation of the program will be uninterrupted or
# error-free. The end-user understands that the program was developed for research
# purposes and is advised not to rely exclusively on the program for any reason.
#
# IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
# DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST
# PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
# THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
# DAMAGE. THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
# FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS
# IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE
# MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
#
# Report any bugs, questions or comments to alistair@caida.org

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
