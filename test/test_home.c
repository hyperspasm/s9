// Where s9 keeps its files: the directory resolved in order, the settings
// and cache entries opened under it, directories made as needed, and
// names that could leave it refused.
#include "greatest.h"
#include "s9_home.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char scratch[256];

// a fresh directory to play in, removed by the tests that fill it
static const char *fresh(void) {
	snprintf(scratch, sizeof scratch, "/tmp/s9-home-test-XXXXXX");
	return mkdtemp(scratch);
}

static void remove_tree(const char *dir) {
	char cmd[512];
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
	if (system(cmd) != 0) {}
}

// An override wins over the environment, and the environment over the
// user's home.
TEST directory_is_resolved_in_order(void) {
	setenv("S9_HOME", "/tmp/from-env", 1);
	s9_home_t *home = s9_home_new("/tmp/from-option");
	ASSERT_STR_EQ("/tmp/from-option", s9_home_directory(home));
	s9_home_free(&home);

	home = s9_home_new(NULL);
	ASSERT_STR_EQ("/tmp/from-env", s9_home_directory(home));
	s9_home_free(&home);

	unsetenv("S9_HOME");
	setenv("HOME", "/tmp/user", 1);
	home = s9_home_new(NULL);
	ASSERT_STR_EQ("/tmp/user/.s9", s9_home_directory(home));
	s9_home_free(&home);
	PASS();
}

// Reading finds nothing until something is written; writing makes the
// directory and the file.
TEST settings_are_made_on_first_write(void) {
	const char *dir = fresh();
	ASSERT(dir != NULL);
	char inside[512];
	snprintf(inside, sizeof inside, "%s/home", dir);
	s9_home_t *home = s9_home_new(inside);

	ASSERT(s9_home_open_settings(home, "r") == NULL);
	FILE *f = s9_home_open_settings(home, "w");
	ASSERT(f != NULL);
	fputs("sample_rate=48000\n", f);
	fclose(f);
	f = s9_home_open_settings(home, "r");
	ASSERT(f != NULL);
	char line[64] = "";
	ASSERT(fgets(line, sizeof line, f) != NULL);
	ASSERT_STR_EQ("sample_rate=48000\n", line);
	fclose(f);

	s9_home_free(&home);
	remove_tree(dir);
	PASS();
}

// A cache entry lives under cache/<kind>/<key>, made on first write, and a
// kind or key that is not a plain name is refused.
TEST cache_entries_by_kind_and_key(void) {
	const char *dir = fresh();
	ASSERT(dir != NULL);
	s9_home_t *home = s9_home_new(dir);

	ASSERT(s9_home_open_cache(home, "kernels", "0123abcd", "rb") == NULL);
	FILE *f = s9_home_open_cache(home, "kernels", "0123abcd", "wb");
	ASSERT(f != NULL);
	fputc('k', f);
	fclose(f);
	char path[512];
	snprintf(path, sizeof path, "%s/cache/kernels/0123abcd", dir);
	ASSERT_EQ(0, access(path, R_OK));
	f = s9_home_open_cache(home, "kernels", "0123abcd", "rb");
	ASSERT(f != NULL);
	ASSERT_EQ('k', fgetc(f));
	fclose(f);

	ASSERT(s9_home_open_cache(home, "../kernels", "x", "wb") == NULL);
	ASSERT(s9_home_open_cache(home, "kernels", "a/b", "wb") == NULL);
	ASSERT(s9_home_open_cache(home, "kernels", ".hidden", "wb") == NULL);
	ASSERT(s9_home_open_cache(home, "", "x", "wb") == NULL);

	s9_home_free(&home);
	remove_tree(dir);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(directory_is_resolved_in_order);
	RUN_TEST(settings_are_made_on_first_write);
	RUN_TEST(cache_entries_by_kind_and_key);
	GREATEST_MAIN_END();
}
