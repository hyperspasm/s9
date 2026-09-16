#include "s9_www.h"

#include <stdio.h>
#include <string.h>

const s9_www_file_t *s9_www_find(const char *path) {
	char resolved[512];
	size_t len = strlen(path);
	if (len > 0 && path[len - 1] == '/') {
		if (snprintf(resolved, sizeof(resolved), "%sindex.html", path) >= (int)sizeof(resolved))
			return NULL;
		path = resolved;
	}
	for (unsigned int i = 0; i < s9_www_file_count; i++) {
		if (s9_www_files[i].path && strcmp(s9_www_files[i].path, path) == 0)
			return &s9_www_files[i];
	}
	return NULL;
}
