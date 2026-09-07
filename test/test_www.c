// The embedded web files are byte-identical to the files in the www directory,
// "/" resolves to the index page, and an unknown path finds nothing.
#include "s9_www.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *what) {
	fprintf(stderr, "FAIL: %s\n", what);
	return 1;
}

int main(void) {
	const s9_www_file_t *f = s9_www_find("/");
	if (!f || strcmp(f->path, "/index.html") != 0) return fail("\"/\" resolves to /index.html");
	if (strcmp(f->mime, "text/html") != 0) return fail("index.html is text/html");

	FILE *fp = fopen(S9_WWW_SRC "/index.html", "rb");
	if (!fp) return fail("open " S9_WWW_SRC "/index.html");
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	unsigned char *disk = malloc((size_t)size);
	if (fread(disk, 1, (size_t)size, fp) != (size_t)size) return fail("read index.html");
	fclose(fp);

	if ((long)f->size != size) return fail("embedded size equals the file size");
	if (memcmp(f->data, disk, (size_t)size) != 0) return fail("embedded bytes equal the file bytes");
	free(disk);

	if (s9_www_find("/nope.html") != NULL) return fail("unknown path finds nothing");
	if (s9_www_find("/index.html") != f) return fail("explicit path finds the same file");

	printf("www: %u embedded files, index.html matches disk\n", s9_www_file_count);
	return 0;
}
