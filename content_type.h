
#ifndef CONTENT_TYPE_hx__
#define CONTENT_TYPE_hx__

#define HTTP_HEADER     "HTTP/1.0 200 OK\nContent-type: "
#define EOL	"\r\n\r\n"

/**
 ** Content-type table
 **
 **     The connection is silently closed if the requested file is
 **	not found in this table.
 **
 **	content types are determined by filename extensions.
 **
 **     This table is searched linearly.
 **	The compare is case insensitive.
 **/

struct content_type {
	size_t len;
        char *ext;
        char *http_header;
	int header_len;		/* calculated once at run time */
} content_types[] = {
	{4, ".jpg", HTTP_HEADER "image/jpeg" EOL, 0},
	{5, ".jpeg", HTTP_HEADER "image/jpeg" EOL, 0},
        {4, ".gif", HTTP_HEADER "image/gif" EOL, 0},
        {4, ".txt", HTTP_HEADER "text/plain" EOL, 0},
        {4, ".htm", HTTP_HEADER "text/html" EOL, 0},
        {5, ".html", HTTP_HEADER "text/html" EOL, 0},
        {0,	0,	0}
};

#endif /* whole file */

