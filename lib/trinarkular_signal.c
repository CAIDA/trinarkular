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

#include "trinarkular_signal.h"

/** Indicates that we received a SIGHUP */
volatile sig_atomic_t sighup_received = 0;
