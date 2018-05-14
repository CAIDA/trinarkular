/*
 * This file is part of trinarkular
 *
 * Copyright (C) 2018 The Regents of the University of California.
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

#ifndef __TRINARKULAR_SIGNAL_H
#define __TRINARKULAR_SIGNAL_H

#include <signal.h>

/** @file
 *
 * @brief Header file that exposes a variable to manage SIGHUP.
 *
 * @author Philipp Winter
 *
 */

extern volatile sig_atomic_t sighup_received;

#endif /* __TRINARKULAR_SIGNAL_H */
