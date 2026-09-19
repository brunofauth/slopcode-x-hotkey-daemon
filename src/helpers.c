/* Copyright (c) 2013, Bastien Dejean
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Modifications Copyright (c) 2026 Bruno Fauth
 *
 * This file is part of a fork of sxhkd distributed as a whole under the GNU
 * General Public License, version 3 or (at your option) any later version;
 * see LICENSE. The original code remains available under the BSD 2-Clause
 * license reproduced above and in LICENSE.BSD-2-Clause.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include "sxhkd.h"

static void wait_for_child(pid_t child_pid);

void warn(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

__attribute__((noreturn))
void err(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void run(char *command, bool sync)
{
	char *cmd[] = {shell, "-c", command, NULL};
	spawn(cmd, sync);
}

void spawn(char *cmd[], bool sync)
{
	const pid_t child_pid = fork();
	if (child_pid == -1) {
		warn("Can't fork to run the command: %s.\n", strerror(errno));
		return;
	}
	if (child_pid == 0) {
		/* Children inherit the X connection and the chain indicator's cairo
		 * and pango state in memory. They must neither use them nor call any
		 * indicator function: they close the connection and exec or exit. */
		if (dpy != NULL)
			close(xcb_get_file_descriptor(dpy));
		if (sync)
			execute(cmd);
		/* Asynchronous: exec from a grandchild, which init reaps, so that the
		 * daemon never has to. */
		const pid_t grandchild_pid = fork();
		if (grandchild_pid == -1)
			warn("Can't fork to run the command: %s.\n", strerror(errno));
		else if (grandchild_pid == 0)
			execute(cmd);
		/* _exit(): the daemon's stdio buffers were duplicated by fork and must
		 * not be flushed a second time from here, and no atexit handler may
		 * run in a child (see fail_status_fifo()). */
		_exit(grandchild_pid == -1 ? EXIT_FAILURE : EXIT_SUCCESS);
	}
	wait_for_child(child_pid);
}

/* The main loop keeps the handled signals blocked; waiting with that mask
 * would make a hanging synchronous command leave sxhkd unstoppable and
 * unreloadable. Wait with the mask sxhkd was started with instead, then put
 * the loop's mask back so that its invariant (handled signals blocked in the
 * loop body) holds again when this returns. A terminating signal stops the
 * wait: the daemon goes on to shut down and the command keeps running on its
 * own (it has its own session); init reaps it. Every other handled signal
 * only sets its flag, which the loop examines once the command has ended.
 * For an asynchronous command the child exits at once, so the wait is
 * short. */
static void wait_for_child(pid_t child_pid)
{
	sigset_t loop_signal_mask;
	if (sigprocmask(SIG_SETMASK, &original_signal_mask, &loop_signal_mask) != 0)
		err("Can't unblock the handled signals: %s.\n", strerror(errno));
	for (;;) {
		if (waitpid(child_pid, NULL, 0) == child_pid)
			break;
		if (errno == EINTR) {
			if (!running)
				break;
			continue;
		}
		warn("Can't wait for the command: %s.\n", strerror(errno));
		break;
	}
	if (sigprocmask(SIG_SETMASK, &loop_signal_mask, NULL) != 0)
		err("Can't block the handled signals: %s.\n", strerror(errno));
}

void execute(char *cmd[])
{
	/* The main loop keeps its handled signals blocked and the mask survives
	 * fork and exec; the command must not run with SIGTERM and friends blocked. */
	sigprocmask(SIG_SETMASK, &original_signal_mask, NULL);
	setsid();
	if (redir_fd != -1) {
		dup2(redir_fd, STDOUT_FILENO);
		dup2(redir_fd, STDERR_FILENO);
		close(redir_fd);
	}
	execvp(cmd[0], cmd);
	/* Only reached when the exec failed. This is the child: report on the
	 * inherited stderr (unbuffered) and leave through _exit() so that the
	 * daemon's stdio buffers are not flushed a second time and no atexit
	 * handler runs. */
	warn("Can't execute '%s': %s.\n", cmd[0], strerror(errno));
	_exit(EXIT_FAILURE);
}

char *lgraph(char *s)
{
	size_t len = strlen(s);
	unsigned int i = 0;
	while (i < len && !isgraph(s[i]))
		i++;
	if (i < len)
		return (s + i);
	else
		return NULL;
}

char *rgraph(char *s)
{
	int i = strlen(s) - 1;
	while (i >= 0 && !isgraph(s[i]))
		i--;
	if (i >= 0)
		return (s + i);
	else
		return NULL;
}
