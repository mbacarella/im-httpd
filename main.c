#include <stdio.h>
#include <signal.h>

#include <unistd.h>

#include "sv_core.h"

int main (void)
{
	fclose(stdin);
	fclose(stdout);
	fclose(stderr);

    signal(SIGPIPE,SIG_IGN);

	if (fork() == 0) {
		setsid();
		sv_core_httpd();
	}
	return 0;
}

