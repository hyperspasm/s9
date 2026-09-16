#include "s9_home.h"
#include "s9.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DIRECTORY_NAME ".s9"
#define SETTINGS_NAME "settings"
#define CACHE_NAME "cache"
#define PATH_MAX_LENGTH 4096

struct s9_home {
	char directory[PATH_MAX_LENGTH];
};

static int is_directory(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// make the directory and any missing above it
static int make_directories(const char *path) {
	char buf[PATH_MAX_LENGTH];
	size_t len = strlen(path);
	if (len == 0 || len >= sizeof buf) return -1;
	memcpy(buf, path, len + 1);
	for (size_t i = 1; i <= len; i++) {
		if (buf[i] == '/' || buf[i] == '\0') {
			char c = buf[i];
			buf[i] = '\0';
			if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
			buf[i] = c;
		}
	}
	return is_directory(path) ? 0 : -1;
}

static int join(char *out, size_t size, const char *a, const char *b) {
	return snprintf(out, size, "%s/%s", a, b) < (int)size ? 0 : -1;
}

s9_home_t *s9_home_new(const char *override) {
	s9_home_t *home = malloc(sizeof(s9_home_t));
	CHECK_ALLOC(home, return NULL);
	memset(home, 0, sizeof(*home));

	const char *env = getenv("S9_HOME");
	const char *user = getenv("HOME");
	int ok;
	if (override && *override) {
		ok = snprintf(home->directory, sizeof home->directory, "%s", override) < (int)sizeof home->directory;
	} else if (env && *env) {
		ok = snprintf(home->directory, sizeof home->directory, "%s", env) < (int)sizeof home->directory;
	} else if (is_directory(DIRECTORY_NAME)) {
		ok = snprintf(home->directory, sizeof home->directory, "%s", DIRECTORY_NAME) < (int)sizeof home->directory;
	} else if (user && *user) {
		ok = join(home->directory, sizeof home->directory, user, DIRECTORY_NAME) == 0;
	} else {
		ok = snprintf(home->directory, sizeof home->directory, "%s", DIRECTORY_NAME) < (int)sizeof home->directory;
	}
	CHECK_ERROR(ok, free(home); return NULL, "the home directory's path is too long");
	return home;
}

void s9_home_free(s9_home_t **home) {
	if (home && *home) {
		free(*home);
		*home = NULL;
	}
}

const char *s9_home_directory(const s9_home_t *home) {
	return home->directory;
}

static int writing(const char *mode) {
	return strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+');
}

// open a file under the directory, making the directories it needs when writing
static FILE *open_under(s9_home_t *home, const char *relative, const char *mode) {
	char path[PATH_MAX_LENGTH];
	if (join(path, sizeof path, home->directory, relative) != 0) return NULL;
	if (writing(mode)) {
		char *slash = strrchr(path, '/');
		*slash = '\0';
		if (make_directories(path) != 0) {
			S9_LOG_ERROR("could not make %s", path);
			return NULL;
		}
		*slash = '/';
	}
	return fopen(path, mode);
}

FILE *s9_home_open_settings(s9_home_t *home, const char *mode) {
	return open_under(home, SETTINGS_NAME, mode);
}

static int plain(const char *name) {
	if (!name || !*name || *name == '.') return 0;
	for (const char *c = name; *c; c++) {
		int ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.';
		if (!ok) return 0;
	}
	return 1;
}

FILE *s9_home_open_cache(s9_home_t *home, const char *kind, const char *key, const char *mode) {
	CHECK_PARAMETER(plain(kind) && plain(key), return NULL);
	char relative[PATH_MAX_LENGTH];
	if (snprintf(relative, sizeof relative, "%s/%s/%s", CACHE_NAME, kind, key) >= (int)sizeof relative) return NULL;
	return open_under(home, relative, mode);
}
