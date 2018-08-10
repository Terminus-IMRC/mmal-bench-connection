/*
 * Copyright (c) 2018 Sugizaki Yukimasa (ysugi@idein.jp)
 * All rights reserved.
 *
 * This software is licensed under a Modified (3-Clause) BSD License.
 * You should have received a copy of this license along with this
 * software. If not, contact the copyright holder above.
 */

#ifndef COMMON_H_
#define COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <interface/vcos/vcos_types.h>
#include <interface/mmal/mmal.h>
#include <time.h>


#define print_info(fmt, ...) \
    do { \
        fprintf(stderr, "%s:%d (%s): info: ", __FILE__, __LINE__, __func__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
    } while (0)

#define print_error(fmt, ...) \
    do { \
        fprintf(stderr, "%s:%d (%s): error: ", __FILE__, __LINE__, __func__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
    } while (0)

#define check_mmal(x) \
    do { \
        MMAL_STATUS_T status = (x); \
        if (status != MMAL_SUCCESS) { \
            print_error("MMAL call failed: %s (0x%08x)\n", \
                    mmal_status_to_string(status), status); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define check_vcos(x) \
    do { \
        VCOS_STATUS_T status = (x); \
        if (status != VCOS_SUCCESS) { \
            print_error("VCOS call failed: 0x%08x\n", status); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)


#define config_port(port, enc, width, height) \
    do { \
        port->format->encoding = enc; \
        port->format->es->video.width  = VCOS_ALIGN_UP(width,  32); \
        port->format->es->video.height = VCOS_ALIGN_UP(height, 16); \
        port->format->es->video.crop.x = 0; \
        port->format->es->video.crop.y = 0; \
        port->format->es->video.crop.width  = width; \
        port->format->es->video.crop.height = height; \
        check_mmal(mmal_port_format_commit(port)); \
    } while (0)

    static inline double get_time(void)
    {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC_RAW, &t);
        return (double) t.tv_sec + t.tv_nsec * 1e-9;
    }

#endif /* COMMON_H_ */
