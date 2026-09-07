#ifndef s9_www_h
#define s9_www_h

// The web interface's files, embedded in the binary at build time from the
// www directory (see cmake/embed_www.cmake).

typedef struct {
	const char *path;           // URL path, e.g. "/index.html"
	const char *mime;           // Content-Type
	const unsigned char *data;
	unsigned int size;
} s9_www_file_t;

extern const s9_www_file_t s9_www_files[];
extern const unsigned int s9_www_file_count;

// The embedded file for a URL path, or NULL. A path ending in "/" resolves to
// its index.html.
const s9_www_file_t *s9_www_find(const char *path);

#endif /* s9_www_h */
