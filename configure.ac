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

AC_INIT([trinarkular], [0.1.0], [alistair@caida.org])

# The following define the version numbers for the libtool-created library
# (libtrinarkular), and not for the overall package. The package version (above)
# should use semantic versioning, for more info on the library versioning, see
# https://www.sourceware.org/autobook/autobook/autobook_91.html

TRINARKULAR_MAJOR_VERSION=0
TRINARKULAR_MID_VERSION=0
TRINARKULAR_MINOR_VERSION=0

AC_DEFINE_UNQUOTED([TRINARKULAR_MAJOR_VERSION],$TRINARKULAR_MAJOR_VERSION,
	[trinarkular major version])
AC_DEFINE_UNQUOTED([TRINARKULAR_MID_VERSION],$TRINARKULAR_MID_VERSION,
	[trinarkular mid version])
AC_DEFINE_UNQUOTED([TRINARKULAR_MINOR_VERSION],$TRINARKULAR_MINOR_VERSION,
	[trinarkular minor version])


LT_INIT
AM_INIT_AUTOMAKE
AC_CONFIG_MACRO_DIR([m4])
# Checks for programs.
AC_PROG_LIBTOOL
AC_PROG_CXX
AM_PROG_LIBTOOL
AC_PROG_CC_C99
AC_CONFIG_HEADERS([config.h])


AC_CONFIG_SRCDIR([lib/trinarkular_probelist.c])


AC_SYS_LARGEFILE
AH_VERBATIM([_GNU_SOURCE],
[/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif])


#AC_CHECK_FUNCS([])


# should we dump debug output to stderr and not optmize the build?
AC_MSG_CHECKING([whether to build with debug information])
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
        [enable debug data generation (def=no)])],
    [debugit="$enableval"],
    [debugit=no])
AC_MSG_RESULT([$debugit])
if test x"$debugit" = x"yes"; then
    AC_DEFINE([DEBUG],[],[Debug Mode])
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC

# Checks for libraries.
AC_SEARCH_LIBS([wandio_create], [wandio trace], with_wandio=yes,
               [AC_MSG_ERROR(
	         [libwandio required (included with libtrace since 3.0.15)]
	       )])
AM_CONDITIONAL([WITH_WANDIO], [test x$with_wandio = xyes])

AC_CHECK_LIB([czmq], [zctx_new], ,[AC_MSG_ERROR([CZMQ is required])])

AC_CHECK_LIB([timeseries], [timeseries_init], , [
    AC_MSG_ERROR([libtimeseries is required])
])

## @@ add more AC_CHECK_LIB / AC_SEARCH_LIBS calls here for needed libraries

## @note AC_CHECK_LIB is for when you know the name of the library,
## AC_SEARCH_LIBS is when you need to determine the library that a specific
## function is in. For example, above we use AC_SEARCH_LIBS to find the
## wandio_create function as it may be in either the libtrace or libwandio
## libraries depending on the version of libtrace in use.

#AC_CHECK_LIB([trace], [trace_get_payload_length], ,[AC_MSG_ERROR(
#		      [libtrace >= 3.0.8 required])])

# check if we can build the probelist generator
AC_MSG_CHECKING([whether to build probelist generator])
AC_ARG_WITH([probelist-gen],
	[AS_HELP_STRING([--with-probelist-gen],
	  [build probelist generator (defaults to yes)])],
	  [],
	  [with_probelist_gen=yes])
AC_MSG_RESULT([$with_probelist_gen])
AS_IF([test "x$with_probelist_gen" == xyes],
	[
	    AC_DEFINE_UNQUOTED([WITH_PROBELIST_GEN],[1],
		[Build probelist generator])
                AC_CHECK_LIB([ipmeta], [ipmeta_init], , [
                    AC_MSG_ERROR([libipmeta is required])
                    ])
	])
AM_CONDITIONAL([WITH_PROBELIST_GEN], [test "x$with_probelist_gen" == xyes])

# check if we can build the scamper driver
AC_MSG_CHECKING([whether to build scamper driver])
AC_ARG_WITH([scamper],
	[AS_HELP_STRING([--with-scamper],
	  [build scamper probe driver (defaults to yes)])],
	  [],
	  [with_scamper=yes])
AC_MSG_RESULT([$with_scamper])
AS_IF([test "x$with_scamper" == xyes],
	[
	    AC_DEFINE_UNQUOTED([WITH_SCAMPER],[1],
		[Build scamper probe driver])
            AC_CHECK_LIB([scamperfile],
                [scamper_file_read],
                [],
                [AC_MSG_ERROR([scamper required (--without-scamper to disable)])]
                )
	])
AM_CONDITIONAL([WITH_SCAMPER], [test "x$with_scamper" == xyes])


# Checks for header files.
AC_CHECK_HEADERS([inttypes.h limits.h math.h stdlib.h string.h time.h sys/time.h])

AC_HEADER_ASSERT

AC_SUBST([TRINARKULAR_MAJOR_VERSION])
AC_SUBST([TRINARKULAR_MID_VERSION])
AC_SUBST([TRINARKULAR_MINOR_VERSION])

AC_CONFIG_FILES([Makefile
		common/Makefile
                common/libinterval3/Makefile
                common/libinterval3/rb_tree/Makefile
                common/libjsmn/Makefile
		common/libpatricia/Makefile
		common/libcsv/Makefile
                lib/Makefile
                lib/drivers/Makefile
		tools/Makefile
		])
AC_OUTPUT
