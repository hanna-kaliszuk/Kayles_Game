/**
 * @file err.h
 * @brief Error handling utilities for terminating the program
 *
 * This module provides two functions for terminating the program:
 * - syserr() - for system errors; it automatically appends errno and its text description.
 * - fatal() - for logical / user errors, without errno information.
 *
 * Both functions are marked with [[noreturn]] to allow the compiler to suppress warnings about missing return
 * statements in the calling code.
**/

#ifndef KAYLES_ERR_H
#define KAYLES_ERR_H

/**
 * @brief Prints information about a system error and quits.
 * * Appends the current 'errno' value and its string representation to the output.
 * * @param ftm A pritf-style format string
 * @param ... Additional arguments matching the format string
**/
[[noreturn]] void syserr(const char* fmt, ...);

/**
 * @brief Prints information about a logical / user error and quits.
 * * @param ftm A printf-style format string
 * @param ... Additional arguments matching the format string
**/
[[noreturn]] void fatal(const char* fmt, ...);

#endif //KAYLES_ERR_H
