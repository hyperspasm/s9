#ifndef note_h
#define note_h

#include <stddef.h>

// Notes in twelve-tone equal temperament, named from a reference frequency
// for A4. A note is numbered as MIDI numbers it, 69 being A4 and 60 middle
// C, and a frequency reads as the nearest note plus its distance from it in
// cents. Pure functions; nothing here allocates.

typedef struct {
	int number;      // 69 is A4, 60 is C4, and there are 12 to the octave
	float cents;     // how far the frequency is above the note, from -50 up to 50
} note_t;

// the nearest note to a frequency, at a reference for A4; both in Hz and positive
note_t note_from_frequency(double hz, double a4_hz);

// the frequency of a note, so many cents above it, at a reference for A4
double note_frequency(int number, float cents, double a4_hz);

// the note as MIDI's fractional number: 60.11 is C4, eleven cents sharp
float note_midi(const note_t *note);

// The note's name with sharps and its octave, as "C#4"; C4 is middle C and
// the octaves change at C. Writes at most NOTE_NAME_MAX bytes.
#define NOTE_NAME_MAX 8
void note_name(int number, char *name, size_t size);

#endif /* note_h */
