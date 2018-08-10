/*
 * Copyright (c) 2018 Sugizaki Yukimasa (ysugi@idein.jp)
 * All rights reserved.
 *
 * This software is licensed under a Modified (3-Clause) BSD License.
 * You should have received a copy of this license along with this
 * software. If not, contact the copyright holder above.
 */

#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_connection.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "common.h"

static void cb_control(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    print_info("Called by %s\n", port->name);
    mmal_buffer_header_release(buffer);
}

static void cb_conn(MMAL_CONNECTION_T *conn)
{
    print_info("Called by %s\n", conn->name);
}

static char *progname = NULL;

static void usage(void)
{
    /* xxx: -z: zero copy */
    printf("Usage: %s [OPTION]...\n", progname);
    printf(""
            "\n"
            "  -?            Print this help\n"

            "\n"
            " General image options:\n"
            "\n"
            "  -e ENC        Encoding of a frame (default: opaque)\n"
            "                Must be one of: i420, opaque\n"
            "  -w WIDTH\n"
            "  -h HEIGHT     Size of a frame to produce (default: 1920x1080)\n"
            "  -t MSEC       Run MMAL connection for MSEC milliseconds (default: 1000)\n"

            "\n"
            " MMAL component options:\n"
            "\n"
            "  -s SOURCE     Source component to use (default: source)\n"
            "                Must be one of: source, camera\n"
            "  -p PATTERN    Source pattern to produce (default: white)\n"
            "                Must be one of: white, black, diagonal, noise, random, colour,\n"
            "                                blocks, swirly\n"
            "  -n CAMERA     Camera number to use (default: -1 (not set))\n"
            "  -o PORT       Camera output port to use (default: 0)\n"
            "                0:preview 1:video 2:capture\n"
            "  -d DEST       Destination component to use (default: null)\n"
            "                Must be one of: null, render\n"
            "  -c CONN       Connection method to use (default: tunnel)\n"
            "                Must be one of: tunnel, callback, queue\n"
            );
}

/*
 * Fuzzy-matches given string in an array
 * @array:  Array of strings
 * @n:      Number of string in @array or %<0 for %NULL terminated arrays
 * @string: String to match with
 *
 * Return: Index of a @string in @array if matches, or <0 otherwise.
 */
static int match_string_fuzzy(const char * const *array, int n,
        const char *string)
{
    int index;
    int count;

    for (index = 0; index < n; index ++) {
        const char *item = array[index];
        if (item == NULL)
            break;
        if (item[0] == '\0')
            return -ENOENT;
    }

    for (count = 1; ; count ++) {
        int matched_index;
        _Bool is_null_found = 0, is_matched = 0, is_ambiguous = 0;
        for (index = 0; index < n; index ++) {
            const char *item = array[index];
            if (item == NULL)
                break;
            if (item[count] == '\0')
                is_null_found = !0;
            if (!strncasecmp(item, string, count)) {
                if (is_matched) {
                    is_ambiguous = !0;
                    break;
                } else {
                    is_matched = !0;
                    matched_index = index;
                }
            }
        }
        if (is_matched && !is_ambiguous)
            return matched_index;
        if (is_null_found)
            return -ENOTUNIQ;
    }
    return -ENOENT;
}

static void show_stats(const char * const name, MMAL_PORT_T * const port,
        const double elapsed)
{
    MMAL_PARAMETER_STATISTICS_T param = {
        .hdr = {
            .id = MMAL_PARAMETER_STATISTICS,
            .size = sizeof(param),
        },
    };

    check_mmal(mmal_port_parameter_get(port, &param.hdr));

    print_info("%s: buffer_count: %u\n", name,  param.buffer_count);
    print_info("%s: frame_count: %u\n", name, param.frame_count);
    print_info("%s: frames_skipped: %u\n", name, param.frames_skipped);
    print_info("%s: frames_discarded: %u\n", name, param.frames_discarded);
    print_info("%s: total_bytes: %lld\n", name, param.total_bytes);
    print_info("%s: %f [frame/s]\n", name, param.frame_count / elapsed);
    print_info("%s: %e [B/s]\n", name, param.total_bytes / elapsed);
}

int main(int argc, char *argv[])
{
    int opt, idx;
    char fourcc[5];
    MMAL_FOURCC_T encoding_mmal;
    MMAL_SOURCE_PATTERN_T pattern_mmal;
    int width = 1920, height = 1080;
    int msec = 1000;
    int camera_num = -1;
    int source_output_port = 0;
    MMAL_COMPONENT_T *cp_source, *cp_dest;
    MMAL_CONNECTION_T *conn_source_dest;
    double start, elapsed;

    /* Encoding */
    enum encoding {
        ENCODING_I420 = 0, ENCODING_RGBA,
    } encoding = ENCODING_I420;
    const char* const encoding_table[] = {
        "i420", "rgba", NULL
    };
    const MMAL_FOURCC_T const encoding_to_mmal[] = {
        MMAL_ENCODING_I420, MMAL_ENCODING_RGBA,
    };
    /* Source */
    enum source {
        SOURCE_SOURCE = 0, SOURCE_CAMERA,
    } source = SOURCE_SOURCE;
    const char* const source_table[] = {
        "source", "camera", NULL
    };
    const char* const source_to_mmal[] = {
        "vc.ril.source", "vc.ril.camera",
    };
    /* Pattern */
    enum pattern {
        PATTERN_WHITE = 0, PATTERN_BLACK, PATTERN_DIAGONAL, PATTERN_NOISE,
        PATTERN_RANDOM, PATTERN_COLOUR, PATTERN_BLOCKS, PATTERN_SWIRLY,
    } pattern = PATTERN_WHITE;
    const char* const pattern_table[] = {
        "white", "black", "diagonal", "noise", "random", "colour", "blocks",
        "swirly", NULL
    };
    const MMAL_SOURCE_PATTERN_T const pattern_to_mmal[] = {
        MMAL_VIDEO_SOURCE_PATTERN_WHITE,
        MMAL_VIDEO_SOURCE_PATTERN_BLACK,
        MMAL_VIDEO_SOURCE_PATTERN_DIAGONAL,
        MMAL_VIDEO_SOURCE_PATTERN_NOISE,
        MMAL_VIDEO_SOURCE_PATTERN_RANDOM,
        MMAL_VIDEO_SOURCE_PATTERN_COLOUR,
        MMAL_VIDEO_SOURCE_PATTERN_BLOCKS,
        MMAL_VIDEO_SOURCE_PATTERN_SWIRLY,
    };
    /* Dest */
    enum dest {
        DEST_NULL = 0, DEST_RENDER,
    } dest = DEST_NULL;
    const char* const dest_table[] = {
        "null", "render", NULL
    };
    const char* const dest_to_mmal[] = {
        "vc.null_sink", "vc.ril.video_render",
    };
    /* Conn */
    enum conn {
        CONN_TUNNEL = 0, CONN_CALLBACK, CONN_QUEUE,
    } conn = CONN_TUNNEL;
    const char* const conn_table[] = {
        "tunnel", "callback", "queue", NULL
    };

    progname = argv[0];
    while ((opt = getopt(argc, argv, "e:w:h:t:s:p:n:o:d:c:?")) != -1) {
        switch (opt) {
            case 'e':
                idx = match_string_fuzzy(encoding_table,
                        MMAL_COUNTOF(encoding_table), optarg);
                if (idx == -ENOTUNIQ) {
                    print_error("Encoding is ambiguous: %s\n", optarg);
                    exit(EXIT_FAILURE);
                } else if (idx == -ENOENT) {
                    print_error("Unknown encoding: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                encoding = (enum encoding) idx;
                break;
            case 'w':
                width = atoi(optarg);
                break;
            case 'h':
                height = atoi(optarg);
                break;
            case 't':
                msec = atoi(optarg);
                break;
            case 's':
                idx = match_string_fuzzy(source_table,
                        MMAL_COUNTOF(source_table), optarg);
                if (idx == -ENOTUNIQ) {
                    print_error("Source is ambiguous: %s\n", optarg);
                    exit(EXIT_FAILURE);
                } else if (idx == -ENOENT) {
                    print_error("Unknown source: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                source = (enum source) idx;
                break;
            case 'p':
                idx = match_string_fuzzy(pattern_table,
                        MMAL_COUNTOF(pattern_table), optarg);
                if (idx == -ENOTUNIQ) {
                    print_error("Pattern is ambiguous: %s\n", optarg);
                    exit(EXIT_FAILURE);
                } else if (idx == -ENOENT) {
                    print_error("Unknown pattern: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                pattern = (enum pattern) idx;
                break;
            case 'n':
                camera_num = atoi(optarg);
                break;
            case 'o':
                source_output_port = atoi(optarg);
                break;
            case 'd':
                idx = match_string_fuzzy(dest_table,
                        MMAL_COUNTOF(dest_table), optarg);
                if (idx == -ENOTUNIQ) {
                    print_error("Dest is ambiguous: %s\n", optarg);
                    exit(EXIT_FAILURE);
                } else if (idx == -ENOENT) {
                    print_error("Unknown dest: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                dest = (enum dest) idx;
                break;
            case 'c':
                idx = match_string_fuzzy(conn_table,
                        MMAL_COUNTOF(conn_table), optarg);
                if (idx == -ENOTUNIQ) {
                    print_error("Conn is ambiguous: %s\n", optarg);
                    exit(EXIT_FAILURE);
                } else if (idx == -ENOENT) {
                    print_error("Unknown conn: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                conn = (enum conn) idx;
                break;
            case '?':
                usage();
                exit(EXIT_SUCCESS);
            default:
                print_error("Unknown option: %x\n", opt);
                exit(EXIT_FAILURE);
        }
    }
    if (optind != argc) {
        print_error("Extra argument(s) after options\n");
        exit(EXIT_FAILURE);
    }

    encoding_mmal = encoding_to_mmal[encoding];
    pattern_mmal = pattern_to_mmal[pattern];

    print_info("encoding: %s (%s)\n", encoding_table[encoding],
            mmal_4cc_to_string(fourcc, sizeof(fourcc), encoding_mmal));
    print_info("width: %d\n", width);
    print_info("height: %d\n", height);
    print_info("msec: %d\n", msec);
    print_info("source: %s (%s)\n", source_table[source],
            source_to_mmal[source]);
    print_info("pattern: %s\n", pattern_table[pattern]);
    print_info("camera_num: %d\n", camera_num);
    print_info("source_output_port: %d\n", source_output_port);
    print_info("dest: %s (%s)\n", dest_table[dest], dest_to_mmal[dest]);
    print_info("conn: %s\n", conn_table[conn]);

    if (source == SOURCE_SOURCE && source_output_port != 0) {
        print_error("Output port must be 0 for source source\n");
        exit(EXIT_FAILURE);
    }

    {
        const char * const name = source_to_mmal[source];
        check_mmal(mmal_component_create(name, &cp_source));
        {
            MMAL_PORT_T *port = mmal_util_get_port(cp_source,
                    MMAL_PORT_TYPE_CONTROL, 0);
            check_mmal(mmal_port_enable(port, cb_control));
        }
        {
            MMAL_PORT_T *port = mmal_util_get_port(cp_source,
                    MMAL_PORT_TYPE_OUTPUT, source_output_port);
            switch (source) {
                case SOURCE_SOURCE:
                    {
                        MMAL_PARAMETER_VIDEO_SOURCE_PATTERN_T param = {
                            .hdr = {
                                .id = MMAL_PARAMETER_VIDEO_SOURCE_PATTERN,
                                .size = sizeof(param),
                            },
                            .pattern = pattern_mmal,
                        };
                        check_mmal(mmal_port_parameter_set(port, &param.hdr));
                    }
                    break;
                case SOURCE_CAMERA:
                    {
                        if (camera_num >= 0) {
                            print_info("Setting camera_num to %d\n",
                                    camera_num);
                            check_mmal(mmal_port_parameter_set_int32(
                                        cp_source->control,
                                        MMAL_PARAMETER_CAMERA_NUM, camera_num));
                        }
                    }
                    break;
            }
            config_port(port, encoding_mmal, width, height);
        }
        check_mmal(mmal_component_enable(cp_source));
    }

    {
        const char * const name = dest_to_mmal[dest];
        check_mmal(mmal_component_create(name, &cp_dest));
        {
            MMAL_PORT_T *port = mmal_util_get_port(cp_dest,
                    MMAL_PORT_TYPE_CONTROL, 0);
            check_mmal(mmal_port_enable(port, cb_control));
        }
        {
            MMAL_PORT_T *port = mmal_util_get_port(cp_dest,
                    MMAL_PORT_TYPE_INPUT, 0);
            config_port(port, encoding_mmal, width, height);
        }
        check_mmal(mmal_component_enable(cp_dest));
    }

    check_mmal(mmal_connection_create(&conn_source_dest,
            cp_source->output[source_output_port], cp_dest->input[0],
            MMAL_CONNECTION_FLAG_TUNNELLING));
    conn_source_dest->callback = cb_conn;

    check_mmal(mmal_connection_enable(conn_source_dest));
    if (source == SOURCE_CAMERA
            && (source_output_port == 1 || source_output_port == 2)) {
        print_info("Setting capture to true\n");
        check_mmal(mmal_port_parameter_set_boolean(
                cp_source->output[source_output_port],
                MMAL_PARAMETER_CAPTURE, MMAL_TRUE));
    }
    print_info("Sleeping for %d milliseconds\n", msec);
    start = get_time();
    {
        struct timespec t = {
            .tv_sec = msec * 1e-3,
            .tv_nsec = (msec % 1000) * 1e6,
        };
        (void) nanosleep(&t, NULL);
    }
    check_mmal(mmal_connection_disable(conn_source_dest));
    elapsed = get_time() - start;

    /*
     * Only vc.ril.source and vc.ril.video_render have an ability to query
     * stats here.  Note that the latter always sets total_bytes to 0.
     */
    if (source == SOURCE_SOURCE)
        show_stats("source", cp_source->output[source_output_port], elapsed);
    if (dest == DEST_RENDER)
        show_stats("dest", cp_dest->input[0], elapsed);

    check_mmal(mmal_connection_destroy(conn_source_dest));
    check_mmal(mmal_component_destroy(cp_dest));
    check_mmal(mmal_component_destroy(cp_source));
    return 0;
}
