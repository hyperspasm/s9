# The fundamental tracker: objective

What s9's pitch tracking is for, what it promises, and how the promises are checked. This is the contract the tracker is designed and judged against, written before the design so that the design answers to it and not the other way round. Numbers marked *draft* are proposals to be settled once the baselines have been scored.

## Purpose

The tracker serves three displays: a tuner, which shows the note and its cents; the harmonics view, which reads the partials of a note against its fundamental; and the waveform view, which sweeps whole cycles of it. All three want the same thing: the fundamental frequency of one steady or steadily moving source, held while it sounds, with a clear signal when there is none.

It is a tuner, not a transcriber. One source at a time. A note is held or moves continuously; a new note is an event the tracker may take a moment to lock on. It never has to segment notes or name them, and it may say "not sure" rather than guess. Once it is sure, it stays sure: the octave does not flip while a note sounds.

## Two problems

Tracking is split in two, and the two are observable separately.

**The lock.** Whatever the tracker has hold of: the period of the waveform, or a harmonic of the source. The lock frequency is followed continuously through glides, bends, vibrato, tremolo and changes of harmonic balance, over the whole range, with no limit on how far it may move, only on how fast: a bound on its rate of change relative to its frequency (see Tracking under Promises). A change beyond that bound between two observations is a discontinuity: the lock is lost and reacquired. The tracker is *unlocked* or *locked*, with a confidence in the lock.

**The fundamental.** The lock frequency times a small rational: the lock is harmonic k of the fundamental, or a sub-multiple of it. Deciding k is a separate, slower, stateful decision made from spectral evidence accumulated over time. It is *undecided* or *committed*, with a confidence, and a committed decision changes only on strong evidence. The reported fundamental is the lock frequency divided by k, so it moves as fast as the lock and is as right as the decision. The ratio is a property of the timbre, not of the pitch: a glide changes the note, not the decision, which holds across it. What can change the decision is a change of timbre, such as a filter removing the partials it rested on.

The displays show both: the lock as a dim marker on the note strip, the fundamental as the bright one, each persisting while its state holds. The tuner's reading is the committed fundamental. Both stages, their confidences and their states are pushed over pn as read-only values beside the frequency and validity pushed today. The lock's marker is shown for now; once the tracker is proven it may become a runtime option with a default.

## Scope

A trackable signal is quasi-periodic: its partials lie at multiples of one fundamental, some of which may be missing, including the fundamental itself, with a period jitter and an inharmonicity no worse than real instruments have, and modulation that is slow compared with the period.

Out of scope, with a required behaviour: more than one source, inharmonic spectra such as ring modulation or non-integer FM, noise, and silence. On these the tracker must not lock falsely, must not wander between candidates, and must report invalid, or lock to the louder of two voices and stay there.

## Promises

Each promise names the metric that checks it. Draft numbers.

- **Lock.** On a steady in-scope note anywhere in the range, the fundamental is committed and within 5 cents within 150 ms of onset. Once committed, the octave does not change while the note sounds, unless it was wrong. Metrics: lock latency, revisions per note.
- **Revision.** A committed decision is never permanent: when clear evidence against it accumulates, it is revised, within 1 s *draft* of the evidence appearing, and not before a non-zero threshold of evidence has built up, so that a moment of ambiguity does not move it. The threshold is to be settled with the corpus, against the two failures it trades off: a mistake that stands, and a decision that wavers. Metrics: correction latency on the named case whose fundamental appears late; revisions per note on clean notes, expected to be zero.
- **Precision.** On a steady tone, jitter under 0.5 cents and bias under 0.5 cents, holding down to 20 dB signal-to-noise ratio. On a stretched string, whose partials are not exact multiples, the reading is the lowest partial's frequency within a few cents: the perceived pitch and the best common period both sit above it by more than the promise's half cent, so the half cent cannot apply. Metrics: fine error median and 90th percentile, jitter.
- **Tracking.** The lock follows any continuous change of up to 100 cents per cycle of the lock frequency and up to 10 octaves per second, whichever is smaller, over the whole range. The per-cycle bound is the physical one at low frequencies, where a period yields one observation; the per-second bound is the algorithm's at high ones. A larger change between observations is a discontinuity, and the lock is regained within the lock latency. Vibrato and glides are instances: ±50 cents at 5 Hz peaks at about 1.3 octaves per second, and is followed with less than a third of a cycle of lag and less than 20 % loss of depth. The fundamental's decision holds across a glide. Metrics: the slew at which the lock breaks, per frequency, found by sweeping it; modulation gain and lag at 1, 3 and 5 Hz; raw pitch accuracy along glides; revisions during glides.
- **Coasting.** Once acquired, the lock survives a glitch: a dropout, a click, a burst of noise, a beat null, a moment without a usable observation. It coasts on its last frequency and rate of change for up to a hold time, 150 ms *draft*, about the lock latency, so that an interruption shorter than a re-lock would take never shows. During a coast the tracker stays locked with a low confidence, the fundamental's decision holds, and validity holds. Past the hold time the lock is lost. What resumes after a glitch within the tracking bound is the same lock; what resumes elsewhere is a discontinuity. Metrics: the dropout length at which the lock breaks, found by sweeping it; validity and octave continuity across glitches shorter than the hold.
- **Balance.** The promises above hold with the fundamental absent or up to 40 dB below the strongest partial, with only odd partials, with two partials, with sixty, with a formant-shaped envelope, with partials stretched as a piano's, and while a filter sweeps partials in and out. Metric: gross error rate per timbre.
- **Immunity.** White and pink noise to the SNR above, hum with harmonics 20 dB or more below the note *draft*, clicks, a room, tremolo to full depth, plucked decays: none causes a false lock or a revision, and validity drops cleanly at silence. Metrics: voicing precision and recall, revisions.
- **Negatives.** On out-of-scope input, no false lock and no wandering. Metric: voicing precision on negatives.
- **Cost.** Runs beside the rest of s9 on the Pi 5. The Pi 3 is measured and welcome, not required. Metric: time per update on each.

## The corpus

Every vector is synthesized from a recipe: a point in the space below, generated on demand from committed recipe data, with its truth. No audio is committed. Recordings enter only when a failure cannot be reproduced synthetically, and then as short clips whose characteristic becomes a recipe.

### Axes

- **Pitch**: static notes over the range, C1 to C7 (32.7 Hz to 2093 Hz); vibrato by depth (5 to 100 cents) and rate (0.5 to 8 Hz); glides by extent (1 to 24 semitones) and duration (0.1 to 3 s), including sweeps up and down over the whole range at rising slew until the lock breaks; steps between notes; the sharpening at a pluck's onset; LFO shapes.
- **Timbre**: sine; sawtooth; square; pulse with moving width; formant-shaped with a weak fundamental; fundamental absent, alone or with the second harmonic; sub-oscillator at −6 to −30 dB; stretched partials with inharmonicity B from 1e-5 to 1e-3; partial counts from 2 to 60; a resonant filter with an LFO on its cutoff.
- **Amplitude**: level −60 to 0 dBFS; sustained; plucked, each partial decaying at its own rate; tremolo by depth and rate; gated silences.
- **Nuisance**: white and pink noise at 0 to 40 dB SNR; hum at 50 and 60 Hz with harmonics; clicks; a synthetic room; a detuned copy for chorus by depth (2 to 30 cents) and rate; a second voice at an interval; glitches: dropouts and noise bursts of 5 to 500 ms within a held note, at rising length until the lock breaks; the negatives: ring modulation, non-integer FM, noise alone, silence.
- **Realism**, applied to any of the above: period jitter and slow drift; random phases per partial; the phase shifts a resonant filter imposes; amplitude noise per partial; breath and pick noise at onsets; a noise floor; bandwidth limits.
- **Timing**: attack 1 to 200 ms; note length; gaps; 44.1 and 48 kHz.

### Sampling

Three sets. A grid over pitch, timbre and noise with the other axes fixed, so failures appear as regions. A seeded random sample over every axis for combinations nobody would write by hand. Named cases that encode particular promises: the fundamental that appears two seconds into a note; a glide over two octaves; a filter sweep that removes the partial the lock started on; a note that stops and restarts an octave away.

### Truth

Each vector carries the fundamental's trajectory at 1 ms steps, the intervals where the tracker should be valid, and, for the lock stage, which partials exist. The awkward cases are defined here, since scoring them needs a definition:

- Chorus: the truth is the mean of the two; the beat depth is tolerance, not error.
- Stretched partials: the truth is the frequency of the lowest partial, what a string is tuned by. The ear hears a few cents sharp of it, and a period-based method measures the best common period of all the partials, sharper still, since the waveform never repeats exactly; neither is chased.
- A sub-oscillator: the truth is the physics. A sub an octave below the played note is the fundamental of the whole series, however quiet, since any threshold on its level would be a guess at the player's intention. A quiet sub is then a test of the balance promise. By the same physics, hum an octave or two below a note is a sub-oscillator and is read as one; at any other ratio it is another source, and the note, being louder, wins.
- A second voice: the truth is the louder voice, or invalid. The mean is wrong.
- Negatives: the truth is invalid throughout.
- Silence: a note below −60 dBFS, the bottom of the amplitude range above, or within 6 dB of the noise floor, is silence, and the truth is invalid there; a plucked note's truth ends where its decay reaches either.
- Onsets: validity is expected from the lock latency after the onset, not from the onset.
- Glitches: through a dropout shorter than the hold time the truth is the note's trajectory as if uninterrupted, and validity is expected throughout; through a longer one, validity is expected to lapse and to return within the lock latency of the note resuming.

## Metrics

- **Raw pitch accuracy**: frames within 50 cents of the truth, over voiced frames.
- **Gross error rate**: frames off by more than 50 cents, octave errors counted separately.
- **Fine error**: cents from the truth on frames within 50 cents; median and 90th percentile.
- **Jitter**: standard deviation of cents on a steady tone.
- **Voicing precision and recall**: valid when the truth is voiced, invalid when it is not.
- **Lock latency**: onset to the first committed frame within 5 cents.
- **Revisions**: changes of the committed octave during a note.
- **Modulation response**: gain and lag of the tracked vibrato against the true one, at 1, 3 and 5 Hz.
- **Cost**: time per update on the Pi 5 and the Pi 3.

Every metric is reported per vector and aggregated along each axis, so a report is a map of where an estimator works.

## Baselines

The harness runs any estimator, a function from audio to a trajectory of lock frequency, fundamental, states and confidences. The first ones through it: the current tracker, pYIN, SWIPE, and the learned estimators CREPE and PESTO. They are not references; they calibrate the promises against what is achievable, and their per-axis maps say where the corpus is hard. A learned stage remains a candidate for the fundamental decision if the classical one fails on something that matters and the model fits the target.

## Open questions

- The revision threshold: how much evidence, over how long, before a committed octave moves. Non-zero; settled with the corpus.
- The hold time for coasting: settled by sweeping dropout lengths, and by feel at the keyboard against a real new note.
