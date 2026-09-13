#include "note.h"

#include <math.h>
#include <stdio.h>

#define A4_NUMBER 69

static const char *const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

note_t note_from_frequency(double hz, double a4_hz) {
	double position = A4_NUMBER + 12.0 * log2(hz / a4_hz);   // in notes, fractional
	double nearest = floor(position + 0.5);
	return (note_t){ .number = (int)nearest, .cents = (float)(100.0 * (position - nearest)) };
}

double note_frequency(int number, float cents, double a4_hz) {
	return a4_hz * pow(2.0, (number - A4_NUMBER + cents / 100.0) / 12.0);
}

float note_midi(const note_t *note) {
	return (float)note->number + note->cents / 100.0f;
}

void note_name(int number, char *name, size_t size) {
	int octave = number / 12, index = number % 12;
	if (index < 0) {
		index += 12;
		octave--;
	}
	snprintf(name, size, "%s%d", names[index], octave - 1);
}
