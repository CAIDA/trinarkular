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

#ifndef __TRINARKULAR_H
#define __TRINARKULAR_H

/** @file
 *
 * @brief Header file that exposes the public interface of trinarkular
 *
 * @author Alistair King
 *
 */

/**
 * @name Public Constants
 *
 * @{ */

/** The total number of /24s in IPv4 */
#define TRINARKULAR_SLASH24_CNT 0x1000000

/** The number of hosts in a /24 */
#define TRINARKULAR_SLASH24_HOST_CNT 0x100

/** Netmask for a /24 network */
#define TRINARKULAR_SLASH24_NETMASK 0xffffff00

/** Host mask for a /24 network */
#define TRINARKULAR_SLASH24_HOSTMASK 0xff

/** @} */

/**
 * @name Public Enums
 *
 * @{ */

/** @} */

/**
 * @name Public Opaque Data Structures
 *
 * @{ */

/** @} */

/**
 * @name Public Data Structures
 *
 * @{ */

/** @} */

#include "trinarkular_probelist.h"
#include "trinarkular_prober.h"

#endif /* __TRINARKULAR_H */
