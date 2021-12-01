/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021 Tobias Kortkamp <tobik@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <sys/param.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libias/array.h>
#include <libias/mempool.h>
#include <libias/str.h>

#include "portscan/status.h"

// Prototypes
static void portscan_status_signal_handler(int);
static void portscan_status_print_progress(void);

static enum PortscanState state = PORTSCAN_STATUS_START;
static struct timespec tic;
static uint32_t interval;
static atomic_int status_requested = ATOMIC_VAR_INIT(0);
static atomic_size_t scanned = ATOMIC_VAR_INIT(0);
static size_t max_scanned;

static struct {
	char buf[32][PATH_MAX];
	size_t len;
	atomic_size_t i;
} current_paths;
static const char *endline = "\n";
static const char *startline = "";

void
portscan_status_init(uint32_t progress_interval)
{
	ssize_t n_threads = sysconf(_SC_NPROCESSORS_ONLN);
	if (n_threads < 0) {
		err(1, "sysconf");
	}
	current_paths.len = MIN(32, n_threads);
	current_paths.i = 0;

	interval = progress_interval;
	clock_gettime(CLOCK_MONOTONIC, &tic);

	if (isatty(STDERR_FILENO)) {
		endline = "";
		startline = "\x1b[2K\r";
	}

#ifdef SIGINFO
	if (signal(SIGINFO, portscan_status_signal_handler)) {
		err(1, "signal");
	}
#endif
	if (signal(SIGUSR2, portscan_status_signal_handler)) {
		err(1, "signal");
	}
	if (interval) {
		if (signal(SIGALRM, portscan_status_signal_handler)) {
			err(1, "signal");
		}
		alarm(interval);
	}
}

void
portscan_status_inc()
{
	scanned++;
}

void
portscan_status_reset(enum PortscanState new_state, size_t max)
{
	state = new_state;
	scanned = 0;
	max_scanned = max;
	if (interval) {
		status_requested = SIGALRM;
	}
	for (size_t i = 0; i < current_paths.len; i++) {
		current_paths.buf[i][0] = 0;
	}
}

void
portscan_status_print_progress()
{
	int percent = 0;
	if (max_scanned > 0) {
		percent = scanned * 100 / max_scanned;
	}
	struct timespec toc;
	clock_gettime(CLOCK_MONOTONIC, &toc);
	int seconds = (toc.tv_nsec - tic.tv_nsec) / 1000000000.0 + (toc.tv_sec  - tic.tv_sec);
	switch (state) {
	case PORTSCAN_STATUS_START:
		fprintf(stderr, "%s[  0%%] starting (%ds)%s", startline, seconds, endline);
		break;
	case PORTSCAN_STATUS_CATEGORIES:
		fprintf(stderr, "%s[%3d%%] scanning categories %zu/%zu (%ds)%s", startline, percent, scanned, max_scanned, seconds, endline);
		break;
	case PORTSCAN_STATUS_PORTS:
		fprintf(stderr, "%s[%3d%%] scanning ports %zu/%zu (%ds)%s", startline, percent, scanned, max_scanned, seconds, endline);
		break;
	case PORTSCAN_STATUS_FINISHED:
		// End output with newline
		fprintf(stderr, "%s[100%%] finished in %ds\n", startline, seconds);
		break;
	}
	if (interval) {
		alarm(interval);
	}

	fflush(stderr);
}

void
portscan_status_print(const char *port)
{
	if (port) {
		strlcpy(current_paths.buf[current_paths.i++ % current_paths.len], port, PATH_MAX);
	}

	int expected = SIGUSR2;
	if (atomic_compare_exchange_strong(&status_requested, &expected, 0)) {
		const char *name = NULL;
		if (state == PORTSCAN_STATUS_CATEGORIES) {
			name = "categories";
		} else if (state == PORTSCAN_STATUS_PORTS) {
			name = "ports";
		}
		if (name) {
			SCOPE_MEMPOOL(pool);
			struct Array *ports = mempool_array(pool);
			for (size_t i = 0; i < current_paths.len; i++) {
				if (*current_paths.buf[i] != 0) {
					array_append(ports, current_paths.buf[i]);
				}
			}
			fprintf(stderr, "Current %s: %s\n", name, str_join(pool, ports, ", "));
		}

		portscan_status_print_progress();
		return;
	}

	expected = SIGALRM;
	if (atomic_compare_exchange_strong(&status_requested, &expected, 0)) {
		portscan_status_print_progress();
	}
}

void
portscan_status_signal_handler(int si)
{
	if (si == SIGALRM) {
		status_requested = SIGALRM;
#ifdef SIGINFO
	} else if (si == SIGINFO) {
		status_requested = SIGUSR2;
#endif
	} else if (si == SIGUSR2) {
		status_requested = SIGUSR2;
	}
}
