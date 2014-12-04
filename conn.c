
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <syslog.h>

#include "config.h"
#include "conn.h"
#include "content_type.h"

enum { AWAITING_REQUEST, SENDING_HEADER, SENDING_FILE };

typedef struct conn_t {
	int     state;
	int     sd, fd;
	off_t	foff;
	char *head;
	size_t	hlen;
	size_t	hoff;
} conn_t;

extern char *se(void);  /* defined in sv_core.c */

conn_t	*vector[MAX_CONNECTIONS];
size_t num_connections;

int streq_case(char *t, char *s, size_t len)
{
	while (len--) {
		if (*t == '\0') {
			if (*s == '\0')
				return 1;
			return 0;
		}
		if (*t != *s)
			if (tolower(*t) != tolower(*s))
				return 0;
		t++; s++;
	}
	return 1;
}

#if 0
int main(int argc, char *argv[])
{
	printf("streq_case: %d\n", streq_case(argv[1], argv[2], strlen(argv[1])));
}
#endif

void conn_init(void)
{
	size_t i;

	for (i = 0; i < MAX_CONNECTIONS; i++)
		vector[i] = NULL;

	num_connections = 0;
}

static conn_t *mkconn(int sd)
{
	conn_t *c = (conn_t *)malloc(sizeof (conn_t));
	if (c == NULL)
		return NULL;
	
	c->state = AWAITING_REQUEST;
	c->fd = -1;
	c->sd = sd;
	c->foff = 0;

	num_connections++;
	return c;
}

static conn_t *rmconn(conn_t *c)
{
	if (c->state == SENDING_FILE
	|| c->state == SENDING_HEADER)
		close(c->fd);
	close(c->sd);
	free(c);

	num_connections--;
	return NULL;
}	


int conn_insert(int sd)
{
	conn_t **v = vector;
	size_t i;

	for (i = 0; i < MAX_CONNECTIONS; i++)
		if (v[i] == NULL) {
			if ((v[i] = mkconn(sd)) == NULL)
				return -1;
			return 0;
		}	
	close(sd);
	return -1;
}


	/* getfname - pull the file name out of buf */

static int getfname(char *buf, char *fname)
{
	struct stat st;
	size_t off, len;
	
	strtok(buf, "\r\n");

	if (strlen(buf) < 5)
		return -1;

		/* start at 4 to skip "GET ", then skip leading whitespace
		 * and any number of / chars (force relative pathname) */
	off = 4;
	off += strspn(buf+off, " \t\v");
	off += strspn(buf+off, "/");

	len = strcspn(buf+off, " ");
	if (len >= IMHTTPD_MAX_PATH)
		return -1;

	strncpy(fname, buf+off, len);
	fname[len] = 0;

	if (strstr(fname, ".."))	/* foil annoying hack attempts */
		return -1;
	if (stat(fname, &st) == -1)
		return -1;
	if (!S_ISREG(st.st_mode))	/* regular files only */
		return -1;

	return 0;
}


	/* getreq - get HTTP request, sanity checks */

static int getreq(conn_t *c)
{
	struct content_type *ct;
	char buf[MAX_REQUEST_LENGTH];
	char fname[IMHTTPD_MAX_PATH];
	size_t pl;
	int enhanced = 0;

/* FIXME- this should be able to handle errno == EWOULDBLOCK
 * and be able to pick up the next time around  */

	while (read(c->sd, buf, sizeof buf) == -1)
		if (errno != EINTR) {
			syslog(LOG_WARNING, "read(): %s", se());
			return -1;
		}

	if (strstr(buf, "HTTP/1."))
		enhanced = 1;
	if (getfname(buf, fname))
		return -1;

	pl = strlen(fname);
	for (ct = content_types; ct->len; ct++)
		if (streq_case(fname+(pl-ct->len), ct->ext, ct->len))
			break;
	if (ct->len == 0)
		return -1;
	
	if ((c->fd = open(fname, O_RDONLY)) == -1)
		return -1;

	if (enhanced) {
		c->state = SENDING_HEADER;
		
		c->head = ct->http_header;
		c->hlen = ct->header_len;
		if (c->hlen == 0)
			c->hlen = ct->header_len = strlen(ct->http_header);
		c->hoff = 0;
	} else
		c->state = SENDING_FILE;

	if ((fcntl(c->fd, F_SETFL, O_NONBLOCK)) == -1) {
		syslog(LOG_WARNING, "fcntl(): %s", strerror(errno));
		return -1;
	}
	return 0;
}


	/* sendhdr - send all/more of http header/content-type */

static int sendhdr(conn_t *c)
{
	int bc;

	while ((bc = write(c->sd, c->head+c->hoff,  c->hlen-c->hoff)) == -1)
		if (errno != EINTR) {
			if (errno == EWOULDBLOCK)
				return 0;
			syslog(LOG_WARNING, "write(): %s", se());
			return -1;
		}

	c->hoff += bc;
	if (c->hoff == c->hlen)
		c->state = SENDING_FILE;

	return 0;
}


	/* sendfile - push more/all of the file down the socket */

static int sendfile(conn_t *c)
{
	char	buf[BLOCK_SIZE];	
	int	bc, bs;

	lseek(c->fd, c->foff, SEEK_SET);
	/* FIXME: A potential inefficiency: if the block-size is much larger than the amount
           we can write to the client without blocking, we'll end up re-reading the same data
           over and over again.  In practice this doesn't seem terrible.  Hmmm. */
	while ((bc = read(c->fd, buf, BLOCK_SIZE)) == -1) {
		if (errno != EINTR) {
			if (errno == EWOULDBLOCK)
				return 0;
			syslog(LOG_WARNING, "read(): %s", se());
			return -1;
		}
	}
	if (bc == 0)	/* hooray! eof! */
		return 1;
	
	while ((bs = write(c->sd, buf, bc)) == -1) {
		if (errno != EINTR) {
			if (errno == EWOULDBLOCK)
				return 0;
			syslog(LOG_WARNING, "write(): %s", se());
			return -1;
		}
	}
	c->foff += bs;
	return 0;
}

void conn_upkeep(fd_set *rfs, fd_set *wfs)
{
	size_t i, n;
	
	if (vector == NULL)
		return;

	for (i = n = 0; i < MAX_CONNECTIONS; i++) {
		conn_t *v;

		if (vector[i] == NULL)
			continue;
		n++;
		if (n > num_connections)
			break;			
	
		v = vector[i];
		switch (v->state) {
		case AWAITING_REQUEST:
			if (FD_ISSET(v->sd, rfs))
				if (getreq(v) == -1) {
					vector[i] = rmconn(v);
					continue;
				}
			break;
		case SENDING_HEADER:
			if (FD_ISSET(v->sd, wfs))
				if (sendhdr(v) == -1) {
					vector[i] = rmconn(v);
					continue;
				}
			break;
		case SENDING_FILE:
			if (FD_ISSET(v->sd, wfs) || FD_ISSET(v->fd, rfs))
				if (sendfile(v) != 0)
					vector[i] = rmconn(v);
			break;
		}
	}
}

int conn_cullselect(fd_set *rfs, fd_set *wfs)
{
	int maxfd = -1;
	size_t i;

	if (vector == NULL)
		return 0;

	for (i = 0; i < MAX_CONNECTIONS; i++) {
		conn_t *v;

		if (vector[i] == NULL)
			continue;
		v = vector[i];
		switch (v->state) {
		case AWAITING_REQUEST:
			if (maxfd < v->sd)
				maxfd = v->sd;
			FD_SET(v->sd, rfs);
			break;
		case SENDING_FILE:
			if (maxfd < v->fd) 
				maxfd = v->fd;
			FD_SET(v->fd, rfs);
		case SENDING_HEADER:
			if (maxfd < v->sd)
				maxfd = v->sd;
			FD_SET(v->sd, wfs);
			break;
		}
	}
	return maxfd;
}

