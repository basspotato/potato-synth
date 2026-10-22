# Huge Layered Synth Sound Design: Layering and Frequency Allocation

Scope: how producers stack synth layers into one large sound without mud, phase cancellation or lost punch, and which of those techniques could become synth defaults. Note on sources: most web write-ups of YouTube tutorials (Virtual Riot, Seamless R, Mr. Bill, Au5) put the exact numbers in the video or in screenshots, so few hard values are available as citable text. Where a source gives no number, that is noted under Gaps.

## 1. Frequency band allocation for layered sounds (which layer owns which band, crossover points, slopes)

### Takeaway
Sources agree on a 3–4 layer split. A sine/simple **sub** (about 30–100 Hz) is low-passed somewhere between 100 and 300 Hz. A **body/low-mid** layer is high-passed at about 100–300 Hz. A **mid "character"** layer sits around 300 Hz–3 kHz, where the sound is heard on small speakers. A **top/air** layer is high-passed at about 2–3 kHz and is often an octave or two up. Crossover points vary by source (100 Hz, 120–130 Hz, 190 Hz, 250 Hz, 300 Hz). No source states filter slopes.

### Cited Findings
- iZotope bass-layering guide: sub layer low-passed to remove everything above **100 Hz**; chord layer high-passed below **100 Hz** to avoid masking the sub; midrange layer "around **300–3000 Hz**" for presence on laptops/earbuds (with Trash 2 distortion for crunch); top layer with "all frequencies below **3 kHz**" EQ'd out, filtered with a sweeping wah and optionally pitched up 1–2 octaves. The 100–300 Hz region is not assigned — [iZotope](https://www.izotope.com/en/learn/how-to-program-and-layer-electronic-basslines-in-your-daw?page=6)
- Attack Magazine "Layering Synths" (3-layer worked example):
  - **Sub:** sine at F#0 peaking around **60 Hz**; sub range given as **40–60 Hz**. EQ rolls off above **190 Hz**, low cut at **30 Hz** removes rumble, and the first note of each 2-bar section is muted to avoid the kick.
  - **Lower-mid layer:** two octaves up (F#2), saw + triangle an octave below. EQ cuts up to **~130 Hz** and rolls off above **3 kHz** "since layer 3 covers those frequencies". Soft clip (high amount, low mix), tape saturation, and **2–3 dB** compression.
  - **Chord/top layer:** three saws forming a minor chord plus noise, low cut up to **250 Hz**, width added only above **2 kHz**.
  - Source: [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/)
- Soundbridge: three layers, low / mid / high. The low layer is monophonic, one sine or saw oscillator, low-passed at **~300 Hz**. The mid layer is high-passed at **~300 Hz**, a saw an octave down detuned "a few" cents. The high layer plays chords with 2+ saws, reverb/delay, and low cut. MIDI ranges given as C-2–C1 (low), C2–C5 (mid), C5–C8 (high). The article says splitting the spectrum "prevents phasing issues caused by overlapping frequencies, especially in the low-end" — [Soundbridge](https://soundbridge.io/layered-synth-sounds)
- MODE Audio gives an example band-pass split for a 4-layer stack: **150–300 Hz (low), 300–550 Hz (low-mid), 550 Hz–2 kHz (high-mid), 2–15 kHz (high)**. Start with band-passes at these edges, then widen them so they "overlap slightly"; too much overlap causes mud. Distortion goes only on the low-mid or high-mid layer, reverb only on the high-mid/high layer. Octave-shift MIDI up for high layers and down for low layers — [MODE Audio](https://modeaudio.com/magazine/layering-synths)
- Xfer OTT's fixed (non-adjustable) crossovers are **88.3 Hz and 2.5 kHz** (lows < 88.3 Hz, mids 88.3 Hz–2.5 kHz, highs > 2.5 kHz). These are a widely used de facto 3-band split in EDM. The original Ableton Multiband Dynamics "OTT" preset moved the low-mid crossover from its 250 Hz default to 88.3 Hz — [EDMProd](https://edmprod.com/ott-plugin)

### Inferences
- A sensible default layer template for a synth: **Sub** (sine/triangle, mono, LP about 100–120 Hz), **Body** (HP about 100–130 Hz, LP about 3 kHz), **Character/Mid** (about 300 Hz–3 kHz, where distortion goes), **Top/Air** (HP about 2–3 kHz, noise/octave-up, width and reverb only here). OTT's 88 Hz / 2.5 kHz points are a good alternative default for a 3-band split.
- The crossover numbers cluster in two zones, 88–130 Hz (sub vs body) and 2–3 kHz (mid vs top). A layered-synth engine could expose just two crossover knobs with those defaults.
- Low-pass the sub and high-pass the body at the same frequency. Use matched complementary filters (for example Linkwitz-Riley) so the sum is flat; this is standard crossover practice, not stated in these sources.

### Gaps
- No source gives crossover **slopes** (12/24/48 dB/oct) for layer splits. Linkwitz-Riley 24 dB/oct is common crossover practice but was not found in these sources.
- The 100–300 Hz "body/low-mid" zone is handled inconsistently (iZotope leaves it unassigned; Soundbridge puts the split at 300 Hz; Attack uses 130/190 Hz). There is no consensus value.

## 2. Mono bass crossover, phase coherence, retriggered vs free-running oscillators, comb filtering, polarity

### Takeaway
Keep the low layers mono and phase-retriggered. That means phase randomization at 0, so every note starts at the same point, and no stereo unison in the sub/reese core. Keep stereo width, Haas delay and random-phase unison above the low cutoff, roughly 120–200 Hz. Overlapping detuned layers in the same band is the main cause of comb filtering and phasing, which is why sources split bands.

### Cited Findings
- Reese bass: set voicing to Mono, set unison stereo spread to mono, and "turn the phase randomization down so they start at the exact same point." The beating then comes only from the detune — [Future Proof Music School](https://futureproofmusicschool.com/blog/reese-bass-sound-design-everything-you-need-to-know)
- "Phase randomization set to 0 or 0% is the same thing as setting your oscillator to retrigger." In Serum this is the Rand knob (Rand > 0 picks a phase inside a range set by Rand and Phase), in Vital it is "Phase Randomization %", and in Massive it is "Restart via gate" in the Osc tab — [r/synthrecipes wiki (mirror)](https://red.127.pp.ua/r/synthrecipes/wiki/cookbook/glossary/phase)
- Syntorial's Martin Garrix "Animal" lead recipe sets "Reset all oscillator phases on note start: Yes", so even a lead uses retriggered phase, which gives consistent transients — [Syntorial](https://www.syntorial.com/preset-recipe/martin-garrix-animal-lead/)
- Ableton Utility's Bass Mono threshold frequency range is **50–500 Hz** (default not stated in the doc) — [Cycling '74 Ableton DSP reference](https://docs.cycling74.com/reference/abl.device.utility~)
- Haas widening: 1–30 ms delay, feedback 0. "Too short, and you can get the comb filtering effect. Too long, and it'll sound like a distinct echo." Extreme panning causes "phase issues in mono playback," so check in mono often. On bass, remove lows "up to around **200 Hz**" from the delayed copy — [eMastered](https://emastered.com/blog/haas-effect)
- Splitting layers by frequency "prevents phasing issues caused by overlapping frequencies, especially in the low-end" — [Soundbridge](https://soundbridge.io/layered-synth-sounds)

### Inferences
- Defaults a synth could build in:
  - Sub and lowest-band oscillators: retrigger on (phase rand 0%), unison off or mono.
  - Upper layers: random phase allowed (e.g. 100%) for "analog" unison spread.
  - An automatic "bass mono below X Hz" stage on the patch output, with X adjustable over roughly 50–500 Hz like Utility; 100–150 Hz is the common choice.
  - A Haas/width module that high-passes its delayed side at about 150–200 Hz.
- A mono-sum correlation check, or an automatic polarity/phase-align between the sub and body layers at the crossover, would address cancellation that EQ alone can't fix.

### Gaps
- I didn't find a sourced, specific Ableton Utility Bass Mono **default** value. It is widely reported as 120 Hz, but the doc I fetched did not state it.
- No source in this pass gave quantified polarity-flip or sample-offset alignment practice between layers (for example how many ms of offset causes audible cancellation at a given crossover).

## 3. Genre-specific big sounds (supersaw leads, reese/growl basses, hoovers, pads, festival leads): layers and roles

### Takeaway
Supersaws are typically 7–16 unison voices per oscillator, with two oscillators an octave apart. Reese basses are 2 saws (or sines) detuned by about ±0.27 semitones, mono and phase-locked, with a separate sub and distorted mids. Big room/festival leads use single-oscillator unison (around 4 voices at about 19 cents with full spread), a fast pitch-envelope "pluck", noise, and heavy chorus/reverb, and are often layered with other patches.

### Cited Findings
- **Supersaw (Vital), FaderPro:** two saws. Osc 1 has **7 unison voices** with detune left near the **20% default** ("above about 50% the sound becomes dissonant"). Osc 2 is **one octave down** with **3 voices, ~7% detune**, at a lower level. Filters: LP on Osc 2 to cut fizz, HP on both. Env attack opens the LP. Reverb about **500 ms**, **30–40%** mix, with low/high cut in the reverb — [FaderPro](https://blog.faderpro.com/techniques/supersaw-how-make-iconic-sound/)
- **Future bass chords (Serum), Loopmasters:** "two **16 voice** unison detuned sawtooth oscillators playing an octave apart". A quick upward pitch envelope at the start, reverb then a gate, a sample loop layered on top, and sidechain ducking from kick and snare. The bass layer is a sine plus a square bass with chorus and HP so it doesn't crowd the sub — [Loopmasters](https://loopmasters.com/articles/4372-How-To-Make-A-Future-Bass-Drop-A-Future-Bass-Production-Tutorial)
- **Supersaw without layering (Serum), Production Music Live:** use unison Stack "12 (1x)" on Osc B so some voices sit an octave up, with Osc B also transposed +1 octave over Osc A. Multiband compression (OTT-style) at full mix with only the first threshold adjusted. Haas delay with feedback 0 and one channel a few ms later. HP plus a high-shelf boost. Dimension at low size, chorus/flanger in moderation — [Production Music Live](https://www.productionmusiclive.com/blogs/news/10-supersaw-tips-for-xfer-serum-get-a-big-lush-sound-without-layering)
- **Big room lead (Martin Garrix "Animal"), Syntorial:** saw, **4 unison voices, 19 cents detune, 100% stereo spread**, phases reset on note start, **noise 15%**, HP filter. Pitch mod envelope **+3 octaves, 0 ms attack, 10 ms decay, 0% sustain** (a pitch "click/pluck" transient). Amp: A 0 ms, S 100%, R 140 ms. Chorus 100% wet at slow rate, reverb 40% wet. The page notes the original track layers several patches — [Syntorial](https://www.syntorial.com/preset-recipe/martin-garrix-animal-lead/)
- **Reese (Ableton Analog), Attack Magazine:** two sines, osc 1 fine **+0.27 st**, osc 2 **−0.27 st** (about ±27 cents, ~54 cents total spread), mono voicing, release 24 ms. Changing the detune changes the beat rate. Later DnB styles distorted reeses into mid-heavy basses — [Attack Magazine](https://www.attackmagazine.com/technique/tutorials/reese-bass-redux/)
- **Reese, Future Proof:** either 2 saws with fine detune on one, or 1 osc with **2 unison voices**. Mono, a little glide, unison spread mono, phase randomization off — [Future Proof Music School](https://futureproofmusicschool.com/blog/reese-bass-sound-design-everything-you-need-to-know)
- **DnB growl mid-bass (Point Blank / Ali Jamieson):** an 808 sample in Sampler, Drum Buss distortion, split into frequency bands with each band processed separately, then Overdrive, envelope, EQ and compression. No numbers are given in the text — [Point Blank](https://www.pointblankmusicschool.com/blog/techtorial-how-to-create-a-growling-mid-range-dnb-bass/)
- **Layered bassline/chord stack, Attack Magazine:** sub (sine) + lower-mid VA (saw + tri −1 oct, soft clip + tape) + chord layer (3 saws forming a minor chord + noise + 40% tube distortion + filter env). Bus: delay, sidechain, reverb (2.88 s, darkened), glue comp — [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/)
- Virtual Riot (interview) says he still uses Nexus "a bunch for layering". He also records long knob-twisting sessions, chops them and resynthesizes them into Serum wavetables (a resampling workflow). No bass-patch numbers are given — [Antares](https://www.antarestech.com/blog/virtual-riot-interview)

### Inferences
- A "supersaw" default could be 7 voices at about 20% detune for leads and 16 voices for future-bass chords, plus a second oscillator one octave down with fewer voices (3) and lower detune.
- A "reese" default could be 2 voices, about ±25–30 cents, mono, retrigger, with an automatic sub layer.
- A "festival lead" default could be about 4 voices at about 19 cents with 100% spread, a 3-octave/10 ms pitch envelope transient, 10–15% noise, and chorus plus reverb on.

### Gaps
- I found no citable text with concrete settings for **hoovers** or for **Seamless R, Mr. Bill, Au5 or Zedd** tutorials; their numbers are in videos. Common lore says the hoover is the Roland Alpha Juno "What the" patch with PWM saws, chorus and a downward pitch bend, but I didn't verify this in this pass.
- No numeric layer counts were found for dubstep growl stacks beyond the general "split into bands, process per band" pattern.

## 4. Common techniques (unison, octave stacking, noise, parallel distortion, OTT, sidechain, Haas, chorus, sends, resampling)

### Takeaway
The recurring toolkit is:
- unison (4–16 voices)
- octave stacking (−1 oct or +1 oct oscillator or unison stack)
- 10–15% noise for top
- distortion limited to the mid layers, often at low mix
- OTT (Depth commonly 20–40% on instruments, fixed 88.3 Hz / 2.5 kHz bands)
- Haas at 1–30 ms with feedback 0 and lows removed
- chorus and reverb/delay placed only on the top layers
- resampling into wavetables or audio for further processing

### Cited Findings
- **OTT controls:**
  - **Depth** is the dry/wet blend. Author's examples: 30% drums, 39% guitar/leads with movement, 100% FX; on the master "no more than 20%, usually ~10%".
  - **Time** sets attack/release for all bands together.
  - **Upward/Downward** behave like ratio controls.
  - Per-band H/M/L output gains act as EQ.
  - Bands are fixed at 88.3 Hz and 2.5 kHz.
  - Original Ableton OTT preset: +5.2 dB input per band; high band ratio ∞:1, mids/lows 1:66.7 (as given); below-threshold 4:1 upward ratios; output +10.3 dB lows/highs and +5.7 dB mids.
  - The author also advises lowering the output gain to avoid "loudness = better" bias.
  - Source: [EDMProd](https://edmprod.com/ott-plugin)
- OTT "amplifies filter sweeps and volume or pitch changes," so it suits leads and basses with movement — [EDMProd](https://edmprod.com/ott-plugin)
- **Parallel distortion:** Attack uses soft-clip distortion at "high amount, low mix" on the lower-mid layer, tape saturation, and 40% tube distortion at the chord layer's filter stage — [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/). MODE Audio applies light distortion only to the low-mid or high-mid layer — [MODE Audio](https://modeaudio.com/magazine/layering-synths)
- **Noise layer:** 15% noise in the Garrix lead recipe — [Syntorial](https://www.syntorial.com/preset-recipe/martin-garrix-animal-lead/). A noise generator on the chord layer adds "top-end detail" — [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/)
- **Stereo width placement:** Ozone Imager adds width only to bands above 2 kHz (Stereoize 13.2) — [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/). Haas: 1–30 ms, feedback 0, HP the delayed side up to about 200 Hz on bass — [eMastered](https://emastered.com/blog/haas-effect)
- **Sends/space:**
  - Stereo delay at 1/8 L/R with high crossfeed and a low cut, ducked by a sidechain compressor plus LFO Tool on the kick. Reverb decay 2.88 s, darkened with reduced HF damping and high cut — [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/)
  - Supersaw reverb about 500 ms at 30–40% mix — [FaderPro](https://blog.faderpro.com/techniques/supersaw-how-make-iconic-sound/)
  - Envelope mapped to FX mix so reverb/dimension shuts off after the note — [Production Music Live](https://www.productionmusiclive.com/blogs/news/10-supersaw-tips-for-xfer-serum-get-a-big-lush-sound-without-layering)
  - Reverb then a gate to tighten future-bass chords — [Loopmasters](https://loopmasters.com/articles/4372-How-To-Make-A-Future-Bass-Drop-A-Future-Bass-Production-Tutorial)
- **Sidechain pumping:** a sidechain compressor with fast attack and release keyed from the kick, plus LFO Tool — [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/). Synth and bass duck on kick and snare in future bass — [Loopmasters](https://loopmasters.com/articles/4372-How-To-Make-A-Future-Bass-Drop-A-Future-Bass-Production-Tutorial). Neutron sidechain with Ext Full input from the kick send — [iZotope](https://www.izotope.com/en/learn/how-to-program-and-layer-electronic-basslines-in-your-daw?page=6)
- **Resampling:** Virtual Riot turns recorded knob-twisting sessions into Serum wavetables — [Antares](https://www.antarestech.com/blog/virtual-riot-interview)

### Inferences
- Built-in features worth defaulting:
  - an OTT-style 3-band up/down compressor with 88/2500 Hz bands, Depth default about 30%, and output auto-gain
  - a per-layer "drive with mix" control (parallel distortion)
  - a noise layer at about 10–15% with HP
  - a width/Haas control that only affects content above about 150–200 Hz (or above 2 kHz for "air" width)
  - a sidechain/volume-shaper "pump" LFO synced to 1/4
  - reverb/delay sends with a built-in low cut and post-send ducking

### Gaps
- No source gave **OTT Time** default values or attack/release in ms. Commonly cited lore (Depth 100 / Time 100 / Up 100 / Down 100 defaults) wasn't verified here.
- No specific chorus rate/depth numbers, or sidechain attack/release ms and depth dB, were found beyond "fast" and "slow rate".

## 5. Gain staging, headroom, and stopping layers from fighting (EQ carving, dynamic EQ, sidechain between layers)

### Takeaway
The cited practice is EQ carving by band (complementary HP/LP at the crossover), per-layer light compression (2–5 dB GR), glue compression on the layer bus (about 2:1, ~4 dB GR, medium or slow times), and kick-keyed ducking. Exact headroom targets (dBFS per layer) were not found. Balancing is done by relative layer levels: pull the bass/low-mid layers down if the sound is heavy, push the mids up if it's thin.

### Cited Findings
- Per-layer compression: lower-mid layer **2–3 dB** (CA-2A), chord layer about **5 dB** peak reduction when the filter opens. Group glue: Solid Bus Comp, **~4 dB GR, ratio 2**, medium attack/release — [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/)
- Bus glue: light compression, slow attack/release, threshold "a few dB", gentle ratio. On the bus EQ, boost thin areas and cut overloaded areas without flattening the character. If the sound is too heavy, reduce the bass/low-mid layers; if it's too bright or thin, boost the low-mid/high-mid layers — [MODE Audio](https://modeaudio.com/magazine/layering-synths)
- Layers go to a group track for shared EQ and sidechain/ducking — [Soundbridge](https://soundbridge.io/layered-synth-sounds)
- To avoid kick clashes: mute the sub's first note per 2-bar section and use LFO Tool for overlapping releases — [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/)
- The second, octave-down oscillator in a supersaw is turned down "so it doesn't stick out" — [FaderPro](https://blog.faderpro.com/techniques/supersaw-how-make-iconic-sound/)
- Level-match after OTT (output gain down) so loudness doesn't bias judgement — [EDMProd](https://edmprod.com/ott-plugin)

### Inferences
- Synth defaults: per-layer level and auto-gain compensation when unison voices or layers are added. N uncorrelated voices sum by about 10·log10(N) dB, so 16 voices add about +12 dB. This is general acoustics, not from these sources.
- Other defaults: a layer-bus glue compressor (2:1, ~4 dB GR target) and an optional internal sidechain, for example the sub layer ducking the body layer's low end, or a dynamic EQ on the body keyed from the sub.

### Gaps
- No source in this pass gave numeric per-layer headroom targets (for example "keep each layer peaking at −12 dBFS" or "bus at −6 dBFS").
- No source described **dynamic EQ** or **inter-layer sidechain** settings specifically. These are commonly recommended in mixing literature but weren't found with numbers here.

## 6. Named producer tutorials and breakdowns with concrete settings

### Takeaway
Concrete text-based settings were found only for the Martin Garrix "Animal" lead (Syntorial recipe) and general Virtual Riot workflow notes. Tutorials from Seamless R, Mr. Bill, Au5 and Zedd are video-only, and I found no reliable written settings.

### Cited Findings
- Martin Garrix "Animal" lead recipe: 4 unison voices / 19 cents / 100% spread, phase reset, 15% noise, HP filter, pitch env +3 oct with 10 ms decay, chorus 100% wet, reverb 40% wet. Values are knob positions and "starting points" — [Syntorial](https://www.syntorial.com/preset-recipe/martin-garrix-animal-lead/)
- Virtual Riot: Nexus for layering, resampling knob-twisting sessions into Serum wavetables, and automating the master quieter in the build so the drop hits louder — [Antares](https://www.antarestech.com/blog/virtual-riot-interview)

### Inferences
- Producer-specific "signature" settings are mostly available only as video content. If they are needed, a follow-up pass would have to use video transcripts.

### Gaps
- No text sources with concrete settings for Seamless R, Mr. Bill, Au5, or Zedd style leads.
- No verified source for the hoover patch parameters.
