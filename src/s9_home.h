#ifndef s9_home_h
#define s9_home_h

#include <stdio.h>

// Where s9 keeps its own files: the settings, and a cache of what is
// expensive to make. The directory is resolved once: an override, else
// S9_HOME in the environment, else .s9 in the working directory when it
// already exists, else .s9 in the user's home. It is made when first
// written to. Components are handed this and open what they need by kind
// and key, never by path.

typedef struct s9_home s9_home_t;

// resolve the directory; override NULL resolves as above
s9_home_t *s9_home_new(const char *override);
void s9_home_free(s9_home_t **home);

// the directory, whether or not it exists yet
const char *s9_home_directory(const s9_home_t *home);

// The settings file, opened with mode as fopen() takes it: NULL when there
// is none to read, or none can be made.
FILE *s9_home_open_settings(s9_home_t *home, const char *mode);

// An entry of the cache: cache/<kind>/<key> under the directory, kind and
// key being plain names of letters, digits, '-', '_' and '.', not starting
// with a dot. NULL as for the settings, or for a name that is not plain.
FILE *s9_home_open_cache(s9_home_t *home, const char *kind, const char *key, const char *mode);

#endif /* s9_home_h */
