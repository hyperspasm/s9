// Notes named from frequencies: what note.h promises.
#include "greatest.h"
#include "note.h"

#include <math.h>

// The reference is A4, number 69, in tune; middle C is 60, named C4; and
// they are named from the reference given.
TEST reference_and_middle_c(void) {
	note_t a = note_from_frequency(440.0, 440.0);
	ASSERT_EQ(69, a.number);
	ASSERT_IN_RANGE(0.0, a.cents, 1e-6);
	note_t c = note_from_frequency(261.6256, 440.0);
	ASSERT_EQ(60, c.number);
	ASSERT_IN_RANGE(0.0, c.cents, 0.01);
	char name[NOTE_NAME_MAX];
	note_name(60, name, sizeof(name));
	ASSERT_STR_EQ("C4", name);
	note_name(69, name, sizeof(name));
	ASSERT_STR_EQ("A4", name);

	note_t at442 = note_from_frequency(442.0, 442.0);
	ASSERT_EQ(69, at442.number);
	ASSERT_IN_RANGE(0.0, at442.cents, 1e-6);
	note_t flat = note_from_frequency(440.0, 442.0);
	ASSERT_EQ(69, flat.number);
	ASSERT_IN_RANGE(-7.85, flat.cents, 0.01);
	PASS();
}

// A frequency reads as the nearest note and the cents above it, from -50
// up to 50: a quarter tone above A4 is A#4 fifty cents flat, and just
// under that is A4 nearly fifty sharp.
TEST nearest_note_and_cents(void) {
	note_t n = note_from_frequency(445.0, 440.0);
	ASSERT_EQ(69, n.number);
	ASSERT_IN_RANGE(19.56, n.cents, 0.01);
	n = note_from_frequency(435.0, 440.0);
	ASSERT_EQ(69, n.number);
	ASSERT_IN_RANGE(-19.79, n.cents, 0.01);
	n = note_from_frequency(440.0 * pow(2.0, 50.0 / 1200.0), 440.0);
	ASSERT_EQ(70, n.number);
	ASSERT_IN_RANGE(-50.0, n.cents, 1e-6);
	n = note_from_frequency(440.0 * pow(2.0, 49.9 / 1200.0), 440.0);
	ASSERT_EQ(69, n.number);
	ASSERT_IN_RANGE(49.9, n.cents, 1e-4);
	PASS();
}

// Names use sharps, and the octaves change at C, from -1 upward.
TEST names_and_octaves(void) {
	char name[NOTE_NAME_MAX];
	note_name(61, name, sizeof(name));
	ASSERT_STR_EQ("C#4", name);
	note_name(59, name, sizeof(name));
	ASSERT_STR_EQ("B3", name);
	note_name(0, name, sizeof(name));
	ASSERT_STR_EQ("C-1", name);
	note_name(127, name, sizeof(name));
	ASSERT_STR_EQ("G9", name);
	note_name(-1, name, sizeof(name));
	ASSERT_STR_EQ("B-2", name);
	PASS();
}

// A note's frequency reads back as that note, and as MIDI's fractional
// number C4 eleven cents sharp is 60.11.
TEST round_trip_and_midi(void) {
	double f = note_frequency(60, 11.0f, 440.0);
	note_t n = note_from_frequency(f, 440.0);
	ASSERT_EQ(60, n.number);
	ASSERT_IN_RANGE(11.0, n.cents, 1e-4);
	ASSERT_IN_RANGE(60.11, note_midi(&n), 1e-5);
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
	GREATEST_MAIN_BEGIN();
	RUN_TEST(reference_and_middle_c);
	RUN_TEST(nearest_note_and_cents);
	RUN_TEST(names_and_octaves);
	RUN_TEST(round_trip_and_midi);
	GREATEST_MAIN_END();
}
