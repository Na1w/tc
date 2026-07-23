/* ==================================================================
 * Minimal Preprocessor
 * ==================================================================
 *
 * Text-based preprocessing pass that handles:
 *   #include <file.h>   - Include from tc/include/ directory
 *   #include "file.h"   - Include from source file's directory
 *   #define MACRO val   - Simple macro definition
 *   #ifdef MACRO        - Conditional inclusion
 *   #ifndef MACRO       - Conditional inclusion
 *   #endif              - End conditional
 *
 * All other # directives are silently skipped.
 * ================================================================== */

/* Simple macro definition for preprocessor */
typedef struct MacroDef {
    char name[64];
    char value[256];
    struct MacroDef *next;
} MacroDef;

static MacroDef *g_macros = NULL;

static void preprocessor_add_macro(const char *name, const char *value) {
    MacroDef *m = (MacroDef *)malloc(sizeof(MacroDef));
    strncpy(m->name, name, 63);
    m->name[63] = '\0';
    strncpy(m->value, value, 255);
    m->value[255] = '\0';
    m->next = g_macros;
    g_macros = m;
}

static int preprocessor_is_defined(const char *name) {
    for (MacroDef *m = g_macros; m; m = m->next) {
        if (strcmp(m->name, name) == 0) return 1;
    }
    return 0;
}

static void preprocessor_free_macros(void) {
    MacroDef *m = g_macros;
    while (m) {
        MacroDef *next = m->next;
        free(m);
        m = next;
    }
    g_macros = NULL;
}

/*
 * preprocess_source - Perform text-based preprocessing on source code.
 *
 * Returns a newly allocated string with preprocessing done, or NULL on error.
 * The original source is NOT freed.
 * depth: recursion depth limit (max 10)
 */
static char *preprocess_source(const char *source, size_t source_len,
                                const char *input_file, int verbose,
                                int depth) {
    /* Prevent infinite recursion */
    if (depth > 10) {
        fprintf(stderr, "preprocessor: include depth exceeded (max 10)\n");
        return NULL;
    }

    /* Build include search path: directory of input file + tc/include/ */
    char include_local[512] = ".";
    char include_std[512] = "include";

    /* Determine local include directory from input file path */
    const char *slash = strrchr(input_file, '/');
    if (slash && slash != input_file) {
        size_t dir_len = (size_t)(slash - input_file);
        if (dir_len < sizeof(include_local) - 1) {
            memcpy(include_local, input_file, dir_len);
            include_local[dir_len] = '\0';
        }
    }

    /* Allocate output buffer - start with 4x source size for includes */
    size_t cap = source_len * 4 + 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t out_len = 0;

    /* Process line by line */
    const char *p = source;
    const char *end = source + source_len;
    int skip_block = 0; /* for #ifdef/#ifndef blocks */
    int skip_depth = 0; /* nested skip tracking */

    while (p < end) {
        /* Find start of next line */
        const char *line_start = p;

        /* Skip leading whitespace */
        while (p < end && (*p == ' ' || *p == '\t')) p++;

        /* Find end of line (the \n character) */
        const char *line_end = p;
        while (line_end < end && *line_end != '\n') line_end++;

        /* Check if this is a preprocessor directive */
        if (p < end && *p == '#') {
            /* Extract directive text (skip # and whitespace) */
            const char *dir = p + 1;
            while (dir < line_end && (*dir == ' ' || *dir == '\t')) dir++;

            /* Parse directive keyword */
            char directive[64];
            int di = 0;
            while (dir < line_end && di < 63 &&
                   (*dir != ' ' && *dir != '\t' && *dir != '\n')) {
                directive[di++] = *dir++;
            }
            directive[di] = '\0';

            /* Skip rest of line whitespace */
            while (dir < line_end && (*dir == ' ' || *dir == '\t')) dir++;

            /* Process directive */
            if (strcmp(directive, "include") == 0) {
                /* #include directive */
                if (skip_block) {
                    /* Skip this line */
                    p = line_end + 1;
                    continue;
                }

                /* Extract filename */
                char filename[256];
                int is_std = 0; /* 1 for <>, 0 for "" */

                if (*dir == '<') {
                    is_std = 1;
                    dir++;
                    int fi = 0;
                    while (dir < line_end && *dir != '>' && fi < 255) {
                        filename[fi++] = *dir++;
                    }
                    filename[fi] = '\0';
                } else if (*dir == '"') {
                    dir++;
                    int fi = 0;
                    while (dir < line_end && *dir != '"' && fi < 255) {
                        filename[fi++] = *dir++;
                    }
                    filename[fi] = '\0';
                } else {
                    fprintf(stderr, "preprocessor: bad #include syntax\n");
                    p = line_end + 1;
                    continue;
                }

                /* Build include path */
                char include_path[512];
                if (is_std) {
                    snprintf(include_path, sizeof(include_path),
                             "%s/%s", include_std, filename);
                } else {
                    snprintf(include_path, sizeof(include_path),
                             "%s/%s", include_local, filename);
                }

                /* Read included file */
                FILE *f = fopen(include_path, "r");
                if (!f) {
                    /* Try alternate path: ./include/ */
                    snprintf(include_path, sizeof(include_path),
                             "./include/%s", filename);
                    f = fopen(include_path, "r");
                }

                if (!f) {
                    fprintf(stderr, "preprocessor: cannot open '%s' "
                            "(tried '%s')\n", filename, include_path);
                    p = line_end + 1;
                    continue;
                }

                fseek(f, 0, SEEK_END);
                long inc_size = ftell(f);
                fseek(f, 0, SEEK_SET);

                char *inc_buf = (char *)malloc((size_t)inc_size + 1);
                if (!inc_buf) {
                    fclose(f);
                    p = line_end + 1;
                    continue;
                }
                fread(inc_buf, 1, (size_t)inc_size, f);
                inc_buf[inc_size] = '\0';
                fclose(f);

                /* Recursively preprocess the included file */
                char *processed = preprocess_source(inc_buf, (size_t)inc_size,
                                                     include_path, verbose,
                                                     depth + 1);
                free(inc_buf);

                if (!processed) {
                    p = line_end + 1;
                    continue;
                }

                /* Append to output buffer */
                size_t proc_len = strlen(processed);
                if (out_len + proc_len + 1 >= cap) {
                    cap = (out_len + proc_len + 1) * 2;
                    char *new_buf = (char *)realloc(buf, cap);
                    if (!new_buf) {
                        free(processed);
                        free(buf);
                        return NULL;
                    }
                    buf = new_buf;
                }
                memcpy(buf + out_len, processed, proc_len);
                out_len += proc_len;
                free(processed);

                if (verbose) {
                    fprintf(stderr, "preprocessor: included '%s'\n", filename);
                }

                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "define") == 0) {
                /* #define NAME [value] */
                if (skip_block) {
                    p = line_end + 1;
                    continue;
                }
                char name[64], value[256];
                int ni = 0;
                while (dir < line_end && ni < 63 &&
                       (*dir != ' ' && *dir != '\t')) {
                    name[ni++] = *dir++;
                }
                name[ni] = '\0';

                while (dir < line_end && (*dir == ' ' || *dir == '\t')) dir++;

                int vi = 0;
                while (dir < line_end && vi < 255 && *dir != '\n') {
                    value[vi++] = *dir++;
                }
                value[vi] = '\0';

                preprocessor_add_macro(name, value);
                if (verbose) {
                    fprintf(stderr, "preprocessor: defined %s = %s\n",
                            name, value);
                }
                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "ifdef") == 0) {
                if (skip_block) {
                    skip_depth++;
                    p = line_end + 1;
                    continue;
                }
                char name[64];
                int ni = 0;
                while (dir < line_end && ni < 63 &&
                       (*dir != ' ' && *dir != '\t')) {
                    name[ni++] = *dir++;
                }
                name[ni] = '\0';

                if (!preprocessor_is_defined(name)) {
                    skip_block = 1;
                    skip_depth = 0;
                }
                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "ifndef") == 0) {
                if (skip_block) {
                    skip_depth++;
                    p = line_end + 1;
                    continue;
                }
                char name[64];
                int ni = 0;
                while (dir < line_end && ni < 63 &&
                       (*dir != ' ' && *dir != '\t')) {
                    name[ni++] = *dir++;
                }
                name[ni] = '\0';

                if (preprocessor_is_defined(name)) {
                    skip_block = 1;
                    skip_depth = 0;
                }
                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "endif") == 0) {
                if (skip_block) {
                    if (skip_depth > 0) {
                        skip_depth--;
                    } else {
                        skip_block = 0;
                    }
                }
                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "undef") == 0 ||
                       strcmp(directive, "pragma") == 0 ||
                       strcmp(directive, "error") == 0 ||
                       strcmp(directive, "warning") == 0 ||
                       strcmp(directive, "line") == 0 ||
                       strcmp(directive, "elif") == 0 ||
                       strcmp(directive, "else") == 0) {
                /* Skip these directives */
                p = line_end + 1;
                continue;
            }

            /* Unknown directive - skip the line */
            p = line_end + 1;
            continue;
        }

        /* Not a preprocessor directive - copy line as-is INCLUDING the newline */
        if (!skip_block) {
            size_t line_len = (size_t)(line_end - line_start);
            /* Include the newline character if present */
            if (line_end < end && *line_end == '\n') {
                line_len++;
            }
            if (out_len + line_len + 1 >= cap) {
                cap = (out_len + line_len + 1) * 2;
                char *new_buf = (char *)realloc(buf, cap);
                if (!new_buf) {
                    free(buf);
                    return NULL;
                }
                buf = new_buf;
            }
            memcpy(buf + out_len, line_start, line_len);
            out_len += line_len;
        }

        p = line_end + 1;
    }

    buf[out_len] = '\0';
    return buf;
}
