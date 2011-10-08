
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <fcntl.h>
#include <syslog.h>
#include <netdb.h>

#include "config.h"
#include "conn.h"
#include "sv_core.h"

char *se(void)
{
	return strerror(errno);
}


static int bindsock(void)
{
	int sv;
	struct sockaddr_in	sin;

	if ((sv = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		syslog(LOG_ERR, "socket(): %s", se());
		return -1;
	}

	memset(&sin, 0, sizeof (struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(SERVER_PORT);

	if (SERVER_ADDR) {
		struct hostent *he;

		if ((he = gethostbyname(SERVER_ADDR)) == NULL) {
			syslog(LOG_ERR, "error resolving %s", (char *)SERVER_ADDR);
			return -1;
		}
		memcpy(&sin.sin_addr, he->h_addr, he->h_length);
	} else
		sin.sin_addr.s_addr = INADDR_ANY;

	if (bind(sv, (struct sockaddr *)&sin,
			sizeof (struct sockaddr_in)) == -1) {
		syslog(LOG_ERR, "bind(): %s", se());
		return -1;
	}

	if (listen(sv, LISTEN_BACKLOG) == -1) {
		syslog(LOG_ERR, "listen(): %s", se());
		return -1;
	}

	return sv;
}


static int getcl(int sv)
{
	int sl, cl;
	struct sockaddr_in sin;

	sl = sizeof (sin);
	while ((cl = accept(sv, (struct sockaddr *)&sin,
				(unsigned int *)&sl)) == -1) {
		if (errno != EINTR) {
			syslog(LOG_ERR, "accept(): %s", se());
			return -1;
		}
	}	

	fcntl(cl, F_SETFL, O_NONBLOCK);
	return cl;
}	


	/* sv_core_httpd - wait on file descriptors */

void sv_core_httpd(void)
{
	int	sv;

	if (chdir(DOCUMENT_ROOT)) {
		syslog(LOG_ERR, "chdir(): %s", se());
		return;
	}

	if ((sv = bindsock()) == -1)
		return;

	syslog(LOG_INFO, "awaiting connections");
	while (1) {
		int maxfd;
		fd_set rfs, wfs;

		FD_ZERO(&rfs);
		FD_ZERO(&wfs);

		FD_SET(sv, &rfs);

		maxfd = conn_cullselect(&rfs, &wfs);
		if (sv > maxfd)
			maxfd = sv;

		if (select(maxfd+1, &rfs, &wfs, NULL, NULL) == -1) {
			if (errno == EINTR)
				continue;
			syslog(LOG_ERR, "select(): %s", se());
			break;
		}
			/* sv coughed, pop a connection off of it */
		if (FD_ISSET(sv, &rfs)) {
			int cl;

			if ((cl = getcl(sv)) == -1)
				break;
			conn_insert(cl);
		}
		conn_upkeep(&rfs, &wfs);
	}
	syslog(LOG_ERR, "terminated due to last error");
	return;
}

