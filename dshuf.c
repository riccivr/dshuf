/*
 * dshuf - Suckless multi-key balanced shuffler
 * See LICENSE file for copyright and license details.
 */

#define _POSIX_C_SOURCE 200809L
#ifndef __USE_MINGW_ANSI_STDIO
#define __USE_MINGW_ANSI_STDIO 1
#endif

#define DSHUF_IMPLEMENTATION
#include "dshuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#include "arg.h"

static ssize_t
portable_getdelim(char **lineptr, size_t *n, int delim, FILE *stream)
{
	char *buf;
	size_t pos = 0;
	int c;

	if (!lineptr || !n || !stream) {
		errno = EINVAL;
		return -1;
	}

	if (!*lineptr || *n == 0) {
		*n = 128;
		*lineptr = (char *)malloc(*n);
		if (!*lineptr) {
			errno = ENOMEM;
			return -1;
		}
	}

	buf = *lineptr;
	while ((c = fgetc(stream)) != EOF) {
		if (pos + 2 >= *n) {
			if (*n > SIZE_MAX / 2) {
				errno = ENOMEM;
				return -1;
			}
			size_t new_size = *n * 2;
			char *new_buf = (char *)realloc(buf, new_size);
			if (!new_buf) {
				errno = ENOMEM;
				return -1;
			}
			buf = new_buf;
			*lineptr = buf;
			*n = new_size;
		}
		buf[pos++] = (char)c;
		if (c == delim)
			break;
	}

	if (c == EOF && pos == 0)
		return -1;

	buf[pos] = '\0';
	return (ssize_t)pos;
}

char *argv0;

static size_t opt_key_fields[DSHUF_MAX_KEYS];
static float  opt_key_weights[DSHUF_MAX_KEYS];
static size_t opt_num_keys = 0;

static char   opt_delim = '\t';
static float  opt_jitter = DSHUF_DEFAULT_JITTER;
static size_t opt_window = DSHUF_DEFAULT_WINDOW;
static int    opt_streaming = 0;
static long   opt_limit = -1;
static int    opt_zero_term = 0;
static uint64_t opt_seed = 0;
static int    opt_seed_set = 0;

static void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(2);
}

static void
usage(void)
{
	fprintf(stderr,
		"usage: %s [-Shvz] [-k field[:weight]] [-d delim] [-j jitter]\n"
		"             [-w window] [-s seed] [-n count] [file ...]\n",
		argv0);
	exit(2);
}

static uint64_t
get_default_seed(void)
{
	uint64_t seed = 0;
	FILE *f = fopen("/dev/urandom", "rb");
	if (f) {
		if (fread(&seed, sizeof(seed), 1, f) == 1 && seed != 0) {
			fclose(f);
			return seed;
		}
		fclose(f);
	}

	struct timespec ts;
#if defined(CLOCK_REALTIME)
	if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
		seed = ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)ts.tv_nsec;
	} else
#endif
	{
		seed = (uint64_t)time(NULL);
	}
	seed ^= ((uint64_t)getpid() << 16);
	seed ^= (uintptr_t)&seed;
	return seed ? seed : 0x123456789abcdef0ULL;
}

static void
parse_key_arg(const char *arg)
{
	if (opt_num_keys >= DSHUF_MAX_KEYS) {
		die("%s: maximum of %d keys exceeded", argv0, DSHUF_MAX_KEYS);
	}

	char *end = NULL;
	long field = strtol(arg, &end, 10);
	if (field < 1) {
		die("%s: invalid key field index '%s'", argv0, arg);
	}

	float weight = -1.0f;
	if (end && *end == ':') {
		weight = (float)strtod(end + 1, NULL);
		if (weight < 0.0f) {
			die("%s: invalid key weight in '%s'", argv0, arg);
		}
	}

	opt_key_fields[opt_num_keys] = (size_t)field;
	opt_key_weights[opt_num_keys] = weight;
	opt_num_keys++;
}

static void
extract_keys(const char *line, size_t line_len, uint32_t *out_keys)
{
	if (opt_num_keys == 0) return;

	for (size_t k = 0; k < opt_num_keys; k++) {
		size_t target_field = opt_key_fields[k];
		size_t current_field = 1;
		size_t pos = 0;
		size_t fstart = 0;
		size_t flen = 0;
		int found = 0;

		while (pos < line_len) {
			if (current_field == target_field) {
				fstart = pos;
				while (pos < line_len && line[pos] != opt_delim &&
				       line[pos] != '\n' && line[pos] != '\r' && line[pos] != '\0') {
					pos++;
				}
				flen = pos - fstart;
				found = 1;
				break;
			}

			if (line[pos] == opt_delim) {
				current_field++;
			}
			pos++;
		}

		if (found && flen > 0) {
			out_keys[k] = dshuf_hash_bytes(&line[fstart], flen);
		} else {
			out_keys[k] = 0;
		}
	}
}

/* Streaming processor */
static void
process_streaming(FILE **files, int num_files)
{
	dshuf_stream_t stream;
	if (dshuf_stream_init(&stream, opt_window, opt_num_keys,
	                      opt_key_weights, opt_jitter, opt_seed) != 0) {
		die("%s: failed to initialize stream buffer", argv0);
	}

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t nread;
	long emitted = 0;
	uint32_t keys[DSHUF_MAX_KEYS];
	int term_delim = opt_zero_term ? '\0' : '\n';

	for (int f = 0; f < num_files; f++) {
		FILE *fp = files[f];
		while ((nread = portable_getdelim(&line, &line_cap, term_delim, fp)) != -1) {
			/* Strip trailing delimiter if present */
			if (nread > 0 && line[nread - 1] == term_delim) {
				line[--nread] = '\0';
			}
			/* Also strip CR if CRLF */
			if (!opt_zero_term && nread > 0 && line[nread - 1] == '\r') {
				line[--nread] = '\0';
			}

			extract_keys(line, (size_t)nread, keys);

			/* Pop items if window reached capacity to preserve strict O(W) memory */
			while (dshuf_stream_count(&stream) >= opt_window) {
				void *p = NULL;
				if (dshuf_stream_pop(&stream, &p) && p) {
					fputs((char *)p, stdout);
					fputc(term_delim, stdout);
					free(p);
					emitted++;
					if (opt_limit >= 0 && emitted >= opt_limit) {
						goto done;
					}
				}
			}

			char *dup = strdup(line);
			if (!dup) die("%s: out of memory", argv0);

			if (dshuf_stream_push(&stream, keys, dup) <= 0) {
				free(dup);
				die("%s: stream push failed", argv0);
			}
		}
	}

	/* Drain remaining items in window */
	void *p = NULL;
	while (dshuf_stream_pop(&stream, &p)) {
		if (p) {
			fputs((char *)p, stdout);
			fputc(term_delim, stdout);
			free(p);
			emitted++;
			if (opt_limit >= 0 && emitted >= opt_limit) {
				break;
			}
		}
	}

done:
	free(line);
	/* Clear and free any items left in the window (e.g. if limit -n triggered early exit) */
	dshuf_stream_clear(&stream, free);
	dshuf_stream_free(&stream);
}

/* Batch processor */
static void
process_batch(FILE **files, int num_files)
{
	char **lines = NULL;
	uint32_t *keys = NULL;
	size_t count = 0;
	size_t cap = 0;

	char *line = NULL;
	size_t line_cap = 0;
	ssize_t nread;
	int term_delim = opt_zero_term ? '\0' : '\n';

	for (int f = 0; f < num_files; f++) {
		FILE *fp = files[f];
		while ((nread = portable_getdelim(&line, &line_cap, term_delim, fp)) != -1) {
			if (nread > 0 && line[nread - 1] == term_delim) {
				line[--nread] = '\0';
			}
			if (!opt_zero_term && nread > 0 && line[nread - 1] == '\r') {
				line[--nread] = '\0';
			}

			if (count >= cap) {
				size_t new_cap = cap ? cap * 2 : 1024;
				char **new_lines = (char **)realloc(lines, new_cap * sizeof(char *));
				if (!new_lines) die("%s: out of memory", argv0);
				lines = new_lines;

				if (opt_num_keys > 0) {
					uint32_t *new_keys = (uint32_t *)realloc(keys, new_cap * opt_num_keys * sizeof(uint32_t));
					if (!new_keys) die("%s: out of memory", argv0);
					keys = new_keys;
				}
				cap = new_cap;
			}

			char *dup = strdup(line);
			if (!dup) die("%s: out of memory", argv0);
			lines[count] = dup;

			if (opt_num_keys > 0) {
				extract_keys(line, (size_t)nread, &keys[count * opt_num_keys]);
			}
			count++;
		}
	}
	free(line);

	if (count == 0) {
		free(lines);
		free(keys);
		return;
	}

	size_t *indices = (size_t *)malloc(count * sizeof(size_t));
	if (!indices) die("%s: out of memory", argv0);

	if (dshuf_batch(indices, keys, count, opt_num_keys, opt_key_weights,
	                opt_jitter, opt_window, opt_seed) != 0) {
		die("%s: batch shuffle failed", argv0);
	}

	size_t emit_count = count;
	if (opt_limit >= 0 && (size_t)opt_limit < emit_count) {
		emit_count = (size_t)opt_limit;
	}

	for (size_t i = 0; i < emit_count; i++) {
		size_t idx = indices[i];
		fputs(lines[idx], stdout);
		fputc(term_delim, stdout);
	}

	for (size_t i = 0; i < count; i++) {
		free(lines[i]);
	}
	free(lines);
	free(keys);
	free(indices);
}

int
main(int argc, char *argv[])
{
	ARGBEGIN {
	case 'k':
		parse_key_arg(EARGF(usage()));
		break;
	case 'd': {
		char *d = EARGF(usage());
		if (strcmp(d, "\\t") == 0) opt_delim = '\t';
		else if (strcmp(d, "\\0") == 0) opt_delim = '\0';
		else opt_delim = d[0];
		break;
	}
	case 'j':
		opt_jitter = (float)strtod(EARGF(usage()), NULL);
		break;
	case 'w':
		opt_window = (size_t)strtoul(EARGF(usage()), NULL, 10);
		if (opt_window == 0) opt_window = 1;
		break;
	case 'S':
		opt_streaming = 1;
		break;
	case 's':
		opt_seed = (uint64_t)strtoull(EARGF(usage()), NULL, 10);
		opt_seed_set = 1;
		break;
	case 'n':
		opt_limit = strtol(EARGF(usage()), NULL, 10);
		break;
	case 'z':
		opt_zero_term = 1;
		break;
	case 'v':
		fprintf(stderr, "dshuf %s\n", VERSION);
		return 0;
	case 'h':
	default:
		usage();
	} ARGEND;

	if (!opt_seed_set) {
		opt_seed = get_default_seed();
	}

	FILE *input_files[64];
	int num_files = 0;

	if (argc == 0) {
		input_files[num_files++] = stdin;
	} else {
		for (int i = 0; i < argc; i++) {
			if (num_files >= 64) {
				die("%s: too many input files", argv0);
			}
			if (strcmp(argv[i], "-") == 0) {
				input_files[num_files++] = stdin;
			} else {
				FILE *fp = fopen(argv[i], "r");
				if (!fp) {
					die("%s: cannot open '%s':", argv0, argv[i]);
				}
				input_files[num_files++] = fp;
			}
		}
	}

	if (opt_streaming) {
		process_streaming(input_files, num_files);
	} else {
		process_batch(input_files, num_files);
	}

	for (int i = 0; i < num_files; i++) {
		if (input_files[i] != stdin) {
			fclose(input_files[i]);
		}
	}

	return 0;
}