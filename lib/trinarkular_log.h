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

#ifndef __TRINARKULAR_LOG_H
#define __TRINARKULAR_LOG_H

/** @file
 *
 * @brief Header file exposing the trinarkular logging sub-system
 *
 * @author Alistair King
 *
 */

/** Write a formatted string to stderr
 *
 * @param func         The name of the calling function (__func__)
 * @param format       The printf style formatting string
 * @param ...          Variable list of arguments to the format string
 *
 * This function takes the same style of arguments that printf(3) does.
 */
void _trinarkular_log(const char *func, const char *format, ...);

/** Write a formatted string to stderr */
#define trinarkular_log(...) _trinarkular_log(__func__, __VA_ARGS__)

#endif /* __TRINARKULAR_LOG_H */
