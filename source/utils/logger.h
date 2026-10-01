/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  logger.h
 * @brief Logging utilities.
 */

#ifndef SOLOADER_LOGGER_H
#define SOLOADER_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#define LT_DEBUG   0
#define LT_INFO    1
#define LT_WARN    2
#define LT_ERROR   3
#define LT_FATAL   4
#define LT_SUCCESS 5
#define LT_WAIT    6

// Hardware-test phase: info/warn/success always reach the log file
// (<DATA_PATH>logs/ragingthunder2_NNN.log). Only l_debug depends on
// DEBUG_SOLOADER (CMAKE_BUILD_TYPE=Debug).
#ifdef DEBUG_SOLOADER
#define l_debug(...)   _log_print(LT_DEBUG,   __VA_ARGS__)
#else
#define l_debug(...)
#endif
#define l_info(...)    _log_print(LT_INFO,    __VA_ARGS__)
#define l_warn(...)    _log_print(LT_WARN,    __VA_ARGS__)
#define l_success(...) _log_print(LT_SUCCESS, __VA_ARGS__)
#define l_wait(...)    _log_print(LT_WAIT,    __VA_ARGS__)

#define l_error(...)   _log_print(LT_ERROR,   __VA_ARGS__)
#define l_fatal(...)   _log_print(LT_FATAL,   __VA_ARGS__)

void _log_print(int t, const char* fmt, ...)
                __attribute__ ((format (printf, 2, 3)));

/** Write every pending line to the log file. */
void log_flush(void);

/** 0: write each line immediately (boot). 1: buffer, flush via log_flush(). */
void log_set_buffered(int enable);

/** Unconditional trace line, always flushed (used by PORT_TRACE builds). */
void port_trace(const char *fmt, ...)
                __attribute__ ((format (printf, 1, 2)));

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_LOGGER_H
