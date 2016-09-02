#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/time.h>
#include <util/event.h>

#include "command.h"

static bool shell_process(void* context) {
	// Non-block select
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	fd_set temp = *(fd_set*)context;
	int ret = select(STDIN_FILENO + 1, (fd_set*)&temp, NULL, NULL, &tv);

	if(ret == -1) {
		perror("Selector error");

		return false;
	} else if(ret) {
		if(FD_ISSET(STDIN_FILENO, (fd_set*)&temp) != 0) {
			/* Process input command */
			command_process(STDIN_FILENO);
		}
	}

	return true;
}

bool shell_init() {
	cmd_init();

	static fd_set stdin;
	FD_ZERO(&stdin);
	FD_SET(STDIN_FILENO, &stdin);

	printf("PacketNgin ver 2.0\n");
	printf("> ");
	fflush(stdout);

	/* Commands input processing */
	event_busy_add(shell_process, &stdin);

	return true;
}
