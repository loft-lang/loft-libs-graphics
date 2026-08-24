// Copyright (c) 2026 Jurjen Stellingwerff
// SPDX-License-Identifier: LGPL-3.0-or-later

//! Audio playback via rodio.
//! Thread-local state: one OutputStream + a list of loaded clips.
//!
//! The browser half of this bridge is `doc/loft-gl-wasm.js` in loft, and the two
//! are one contract: a game writes `audio_play(clip, vol, true, -1.0, 0.0)` once and
//! hears the same thing on a desktop and in a page. Where a decision could go two
//! ways, this file takes the WEB AUDIO answer, because that side has a
//! specification and this one has a mixer:
//!
//! * **Pan** is `StereoPannerNode`'s equal-power law, mono and stereo formulas
//!   alike (see `Panned`). rodio has no pan control on a `Sink` at all.
//! * **A sink id is never reused.** Every id carries the generation of the slot it
//!   names, so a stale id from a finished clip controls nothing — where reusing the
//!   slot number silently handed the next `stop()` someone else's sound.
//! * **`start` seeks the FIRST pass only.** A looping clip repeats from the top,
//!   which is what `AudioBufferSourceNode.start(when, offset)` does with
//!   `loop = true` and no `loopStart`.

use loft_ffi_macros::loft_native;
use std::cell::RefCell;
use std::io::Cursor;
use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};
use std::time::Duration;

use rodio::Source;

/// A loaded audio clip — the raw bytes kept in memory for replay.
struct Clip {
    data: Vec<u8>,
}

/// One playback, and the pan its caller can still move.
struct Playing {
    sink: rodio::Sink,
    /// An `f32`'s bits, -1.0 (hard left) to 1.0 (hard right). Shared with the
    /// `Panned` source, which reads it per frame — which is what makes a pan
    /// change audible on a clip that is already playing.
    pan: Arc<AtomicU32>,
    /// Which handing-out of this slot the live id belongs to.
    generation: i64,
}

struct AudioState {
    _stream: rodio::OutputStream,
    handle: rodio::OutputStreamHandle,
    clips: Vec<Clip>,
    /// Slots, reused as playbacks finish. The id a caller holds carries the
    /// generation, so reuse cannot silently redirect a `stop`.
    sinks: Vec<Playing>,
}

thread_local! {
    static AUDIO: RefCell<Option<AudioState>> = const { RefCell::new(None) };
}

/// How many low bits of a sink id are the slot number. The rest is the
/// generation, so a million concurrent sounds and eight trillion re-uses both fit.
const SLOT_BITS: i64 = 20;
const SLOT_MASK: i64 = (1 << SLOT_BITS) - 1;

/// Ensure the audio output stream is initialised.
fn ensure_audio() -> bool {
    AUDIO.with(|cell| {
        if cell.borrow().is_some() {
            return true;
        }
        match rodio::OutputStream::try_default() {
            Ok((stream, handle)) => {
                *cell.borrow_mut() = Some(AudioState {
                    _stream: stream,
                    handle,
                    clips: Vec::new(),
                    sinks: Vec::new(),
                });
                true
            }
            Err(e) => {
                eprintln!("loft_audio: cannot open audio device: {e}");
                false
            }
        }
    })
}

/// A stereo panner whose position can move while the clip plays.
///
/// rodio's `Sink` has volume and nothing else, and its `Spatial` source places an
/// emitter in 3-D rather than taking the -1..1 a game asks for. So the pan lives in
/// an atomic the sink's owner writes and this source reads once per output frame.
///
/// The arithmetic is Web Audio's `StereoPannerNode`, which is two different laws:
/// a MONO input is placed on the stereo field with an equal-power pair of gains,
/// while a STEREO input keeps its own image and is pushed toward one side. Getting
/// that wrong is not a rounding difference — a stereo clip panned by the mono law
/// loses the far channel entirely.
struct Panned<S> {
    inner: S,
    pan: Arc<AtomicU32>,
    /// The right-channel sample, computed alongside the left one and yielded next.
    pending: Option<f32>,
    /// A one-channel input is placed on a stereo field, so this source answers 2.
    mono: bool,
}

impl<S> Panned<S>
where
    S: Source<Item = f32>,
{
    fn new(inner: S, pan: Arc<AtomicU32>) -> Self {
        let mono = inner.channels() == 1;
        Panned {
            inner,
            pan,
            pending: None,
            mono,
        }
    }

    /// NOT called `position`: `Iterator::position` is in scope on this very type,
    /// and the collision resolves to the trait method with a baffling error.
    fn pan_now(&self) -> f32 {
        f32::from_bits(self.pan.load(Ordering::Relaxed)).clamp(-1.0, 1.0)
    }
}

/// The two gains a MONO sample is split into — Web Audio's equal-power law.
///
/// At the centre both are `sqrt(0.5)`, so a pan sweep holds its loudness instead of
/// dipping through the middle.
pub(crate) fn mono_gains(pan: f32) -> (f32, f32) {
    let x = (pan.clamp(-1.0, 1.0) + 1.0) * 0.5 * std::f32::consts::FRAC_PI_2;
    (x.cos(), x.sin())
}

/// A STEREO frame pushed toward one side — Web Audio's stereo law.
///
/// The near channel is passed through at full gain and the far one is folded into
/// it, so panning a stereo clip narrows its image rather than muting a side.
pub(crate) fn stereo_frame(l: f32, r: f32, pan: f32) -> (f32, f32) {
    let p = pan.clamp(-1.0, 1.0);
    let x = if p <= 0.0 { p + 1.0 } else { p } * std::f32::consts::FRAC_PI_2;
    if p <= 0.0 {
        (l + r * x.cos(), r * x.sin())
    } else {
        (l * x.cos(), r + l * x.sin())
    }
}

impl<S> Iterator for Panned<S>
where
    S: Source<Item = f32>,
{
    type Item = f32;

    fn next(&mut self) -> Option<f32> {
        if let Some(r) = self.pending.take() {
            return Some(r);
        }
        let pan = self.pan_now();
        if self.mono {
            let s = self.inner.next()?;
            let (gl, gr) = mono_gains(pan);
            self.pending = Some(s * gr);
            Some(s * gl)
        } else if self.inner.channels() == 2 {
            let l = self.inner.next()?;
            let r = self.inner.next().unwrap_or(0.0);
            let (out_l, out_r) = stereo_frame(l, r, pan);
            self.pending = Some(out_r);
            Some(out_l)
        } else {
            // More than two channels: pass the layout through untouched. A
            // surround mix has its own placement, and folding it here would be a
            // downmix nobody asked for.
            self.inner.next()
        }
    }
}

impl<S> Source for Panned<S>
where
    S: Source<Item = f32>,
{
    fn current_frame_len(&self) -> Option<usize> {
        let pending = usize::from(self.pending.is_some());
        self.inner
            .current_frame_len()
            .map(|n| if self.mono { n * 2 } else { n } + pending)
    }

    fn channels(&self) -> u16 {
        if self.mono { 2 } else { self.inner.channels() }
    }

    fn sample_rate(&self) -> u32 {
        self.inner.sample_rate()
    }

    fn total_duration(&self) -> Option<Duration> {
        self.inner.total_duration()
    }

    fn try_seek(&mut self, pos: Duration) -> Result<(), rodio::source::SeekError> {
        // The half-computed frame belongs to the OLD position.
        self.pending = None;
        self.inner.try_seek(pos)
    }
}

/// The sources one playback appends to its sink, in order.
///
/// Two of them in exactly one case: a LOOPING clip that also starts late. Web
/// Audio's `start(when, offset)` skips into the first pass and then repeats the
/// buffer WHOLE, so the late first pass is its own source ahead of the endless
/// one rather than an offset baked into the repeat.
///
/// The non-looping, non-offset case is deliberately left unwrapped: `Buffered`
/// cannot seek and `SkipDuration` only forwards, so the plain chain is the one a
/// later `try_seek` can still reach through.
fn playback_sources<S>(
    source: S,
    looping: bool,
    offset: Duration,
    pan: &Arc<AtomicU32>,
) -> Vec<Box<dyn Source<Item = f32> + Send>>
where
    S: Source<Item = f32> + Send + 'static,
{
    let late = offset > Duration::ZERO;
    if looping {
        // `buffered` is what makes the source cloneable, and therefore repeatable.
        let base = source.buffered();
        let mut out: Vec<Box<dyn Source<Item = f32> + Send>> = Vec::new();
        if late {
            out.push(Box::new(Panned::new(
                base.clone().skip_duration(offset),
                Arc::clone(pan),
            )));
        }
        out.push(Box::new(Panned::new(
            base.repeat_infinite(),
            Arc::clone(pan),
        )));
        out
    } else if late {
        vec![Box::new(Panned::new(
            source.skip_duration(offset),
            Arc::clone(pan),
        ))]
    } else {
        vec![Box::new(Panned::new(source, Arc::clone(pan)))]
    }
}

/// Hand out a slot for a new playback, and the id that names it.
fn store_sink(st: &mut AudioState, sink: rodio::Sink, pan: Arc<AtomicU32>) -> i64 {
    for (i, p) in st.sinks.iter_mut().enumerate() {
        if p.sink.empty() {
            p.generation += 1;
            p.sink = sink;
            p.pan = pan;
            return (i as i64) | (p.generation << SLOT_BITS);
        }
    }
    st.sinks.push(Playing {
        sink,
        pan,
        generation: 0,
    });
    (st.sinks.len() - 1) as i64
}

/// The playback an id names, or `None` when the id is stale — its slot has been
/// handed to a later sound, or it never named one.
fn playing(st: &mut AudioState, id: i64) -> Option<&mut Playing> {
    if id < 0 {
        return None;
    }
    let slot = (id & SLOT_MASK) as usize;
    let generation = id >> SLOT_BITS;
    let p = st.sinks.get_mut(slot)?;
    if p.generation == generation {
        Some(p)
    } else {
        None
    }
}

/// Load an audio file (WAV or OGG).  Returns clip index (>= 0) or
/// `i32::MIN` (loft null sentinel) on failure.
#[loft_native]
#[unsafe(no_mangle)]
pub extern "C" fn loft_audio_load(path_ptr: *const u8, path_len: usize) -> i64 {
    let path = unsafe { loft_ffi::text(path_ptr, path_len) };
    if !ensure_audio() {
        return i64::MIN;
    }
    let data = match std::fs::read(path) {
        Ok(d) => d,
        Err(_) => return i64::MIN,
    };
    AUDIO.with(|cell| {
        let mut st = cell.borrow_mut();
        let st = st.as_mut().unwrap();
        let idx = st.clips.len();
        st.clips.push(Clip { data });
        idx as i64
    })
}

/// Play a loaded clip: volume 0..1, `looping` to repeat it forever, `pan` -1..1,
/// and `start` seconds into the clip.  Returns a sink id or -1 on failure.
///
/// `start` skips into the FIRST pass only; a looping clip then repeats whole.
#[loft_native]
#[unsafe(no_mangle)]
pub extern "C" fn loft_audio_play(
    clip: i64,
    volume: f64,
    looping: bool,
    pan: f64,
    start: f64,
) -> i64 {
    if clip < 0 {
        return -1;
    }
    AUDIO.with(|cell| {
        let mut st = cell.borrow_mut();
        let Some(st) = st.as_mut() else { return -1 };
        let idx = clip as usize;
        if idx >= st.clips.len() {
            return -1;
        }
        let data = st.clips[idx].data.clone();
        let decoded = match rodio::Decoder::new(Cursor::new(data)) {
            Ok(s) => s,
            Err(_) => return -1,
        };
        let sink = match rodio::Sink::try_new(&st.handle) {
            Ok(s) => s,
            Err(_) => return -1,
        };
        sink.set_volume(volume as f32);
        let pan_cell = Arc::new(AtomicU32::new((pan as f32).clamp(-1.0, 1.0).to_bits()));
        let offset = Duration::from_secs_f64(start.max(0.0));
        for part in playback_sources(decoded.convert_samples::<f32>(), looping, offset, &pan_cell) {
            sink.append(part);
        }
        store_sink(st, sink, pan_cell)
    })
}

/// Stop a playing clip by sink id.
#[loft_native]
#[unsafe(no_mangle)]
pub extern "C" fn loft_audio_stop(sink_idx: i64) {
    AUDIO.with(|cell| {
        let mut st = cell.borrow_mut();
        let Some(st) = st.as_mut() else { return };
        if let Some(p) = playing(st, sink_idx) {
            p.sink.stop();
        }
    });
}

/// Stop every playing clip: a pause menu, a scene change, a game over.
#[loft_native]
#[unsafe(no_mangle)]
pub extern "C" fn loft_audio_stop_all() {
    AUDIO.with(|cell| {
        let mut st = cell.borrow_mut();
        let Some(st) = st.as_mut() else { return };
        for p in st.sinks.iter_mut() {
            p.sink.stop();
        }
    });
}

/// Set volume of a playing clip (0.0–1.0).
#[loft_native]
#[unsafe(no_mangle)]
pub extern "C" fn loft_audio_set_volume(sink_idx: i64, volume: f64) {
    AUDIO.with(|cell| {
        let mut st = cell.borrow_mut();
        let Some(st) = st.as_mut() else { return };
        if let Some(p) = playing(st, sink_idx) {
            p.sink.set_volume(volume as f32);
        }
    });
}

/// Move a playing clip across the stereo field: -1 left, 0 centre, 1 right.
#[loft_native]
#[unsafe(no_mangle)]
pub extern "C" fn loft_audio_set_pan(sink_idx: i64, pan: f64) {
    AUDIO.with(|cell| {
        let mut st = cell.borrow_mut();
        let Some(st) = st.as_mut() else { return };
        if let Some(p) = playing(st, sink_idx) {
            p.pan
                .store((pan as f32).clamp(-1.0, 1.0).to_bits(), Ordering::Relaxed);
        }
    });
}

/// Jump a playing clip to `seconds` from its start.  Answers whether it moved.
///
/// A LOOPING clip cannot seek: repeating needs a buffered source, and rodio's
/// buffer has no earlier position to go back to. The answer says so, rather than
/// the clip carrying on from where it was while the caller believes otherwise.
#[loft_native]
#[unsafe(no_mangle)]
pub extern "C" fn loft_audio_seek(sink_idx: i64, seconds: f64) -> bool {
    AUDIO.with(|cell| {
        let mut st = cell.borrow_mut();
        let Some(st) = st.as_mut() else { return false };
        let Some(p) = playing(st, sink_idx) else {
            return false;
        };
        p.sink
            .try_seek(Duration::from_secs_f64(seconds.max(0.0)))
            .is_ok()
    })
}

/// A raw PCM source: mono f32 samples at a given sample rate.
struct RawPcmSource {
    samples: Vec<f32>,
    pos: usize,
    sample_rate: u32,
}

impl Iterator for RawPcmSource {
    type Item = f32;
    fn next(&mut self) -> Option<f32> {
        if self.pos < self.samples.len() {
            let v = self.samples[self.pos];
            self.pos += 1;
            Some(v)
        } else {
            None
        }
    }
}

impl rodio::Source for RawPcmSource {
    fn current_frame_len(&self) -> Option<usize> {
        Some(self.samples.len() - self.pos)
    }
    fn channels(&self) -> u16 {
        1
    }
    fn sample_rate(&self) -> u32 {
        self.sample_rate
    }
    fn total_duration(&self) -> Option<Duration> {
        Some(Duration::from_secs_f64(
            self.samples.len() as f64 / self.sample_rate as f64,
        ))
    }
}

/// Play raw PCM samples (mono f32, values -1.0 to 1.0) at the given sample rate.
/// Returns a sink id (for stopping / volume / pan) or -1 on failure.
/// Native compilation path: receives raw pointer + count.
#[unsafe(no_mangle)]
pub extern "C" fn loft_audio_play_raw(
    data_ptr: *const f32,
    data_count: u32,
    sample_rate: i64,
    volume: f64,
) -> i64 {
    if data_ptr.is_null() || data_count == 0 || sample_rate <= 0 {
        return -1;
    }
    if !ensure_audio() {
        return -1;
    }
    let samples = unsafe { std::slice::from_raw_parts(data_ptr, data_count as usize) }.to_vec();
    let source = RawPcmSource {
        samples,
        pos: 0,
        sample_rate: sample_rate as u32,
    };
    AUDIO.with(|cell| {
        let mut st = cell.borrow_mut();
        let Some(st) = st.as_mut() else { return -1 };
        let sink = match rodio::Sink::try_new(&st.handle) {
            Ok(s) => s,
            Err(_) => return -1,
        };
        sink.set_volume(volume as f32);
        let pan = Arc::new(AtomicU32::new(0.0f32.to_bits()));
        sink.append(Panned::new(source, Arc::clone(&pan)));
        store_sink(st, sink, pan)
    })
}

/// Interpreter wrapper: extracts vector<single> via LoftStore + LoftRef.
#[loft_native]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn n_audio_play_raw(
    store: loft_ffi::LoftStore,
    data: loft_ffi::LoftRef,
    sample_rate: i64,
    volume: f64,
) -> i64 {
    let count = unsafe { store.vector_len(&data) };
    let data_ptr = unsafe { store.vector_data_ptr(&data) } as *const f32;
    loft_audio_play_raw(data_ptr, count, sample_rate, volume)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The pan law is a CONTRACT with the browser bridge rather than an
    /// implementation detail: `StereoPannerNode` is specified, and these are the
    /// numbers it produces. They are checked here because checking them needs no
    /// audio device — and CI has none, so nothing else in this file can be
    /// measured on the machine that gates it.
    #[test]
    fn mono_pan_is_equal_power() {
        let (l, r) = mono_gains(0.0);
        assert!((l - r).abs() < 1e-6, "the centre is symmetric: {l} vs {r}");
        assert!(
            (l * l + r * r - 1.0).abs() < 1e-6,
            "equal POWER: the squares sum to one, got {}",
            l * l + r * r
        );
        let (l, r) = mono_gains(-1.0);
        assert!(l > 0.999 && r < 1e-6, "hard left is all left: {l} vs {r}");
        let (l, r) = mono_gains(1.0);
        assert!(r > 0.999 && l < 1e-6, "hard right is all right: {l} vs {r}");
    }

    #[test]
    fn mono_pan_holds_its_loudness_across_a_sweep() {
        // A sweep that dips through the middle is the failure a linear law has.
        for i in 0..=20 {
            let pan = -1.0 + (i as f32) / 10.0;
            let (l, r) = mono_gains(pan);
            let power = l * l + r * r;
            assert!((power - 1.0).abs() < 1e-5, "at {pan}: power {power}");
        }
    }

    #[test]
    fn a_stereo_frame_narrows_rather_than_mutes() {
        // Centre: untouched. This is the case a mono law gets wrong, and it is the
        // common one — most music is stereo and most of it is never panned.
        let (l, r) = stereo_frame(0.7, -0.3, 0.0);
        assert!(
            (l - 0.7).abs() < 1e-6 && (r + 0.3).abs() < 1e-6,
            "a centred stereo frame passes through: {l}, {r}"
        );
        // Hard left: the right channel folds into the left, and nothing is lost.
        let (l, r) = stereo_frame(0.5, 0.25, -1.0);
        assert!((l - 0.75).abs() < 1e-6, "left carries both: {l}");
        assert!(r.abs() < 1e-6, "and the right is empty: {r}");
        // Hard right, mirrored.
        let (l, r) = stereo_frame(0.5, 0.25, 1.0);
        assert!(l.abs() < 1e-6, "left is empty: {l}");
        assert!((r - 0.75).abs() < 1e-6, "right carries both: {r}");
    }

    /// A source with a value that says WHERE in the clip a sample came from:
    /// `0.01, 0.02, …`. A test that only asks "is there sound?" cannot tell a loop
    /// from a long clip, or a seek from a restart; this can.
    fn ramp(len: usize, rate: u32) -> RawPcmSource {
        RawPcmSource {
            samples: (0..len).map(|i| (i + 1) as f32 / 100.0).collect(),
            pos: 0,
            sample_rate: rate,
        }
    }

    /// `Panned` turns a mono source into a stereo one, so every input sample
    /// appears twice. Take the LEFT of each pair back to the value it came from.
    fn left_channel(src: &mut dyn Iterator<Item = f32>, frames: usize) -> Vec<f32> {
        let mut out = Vec::new();
        for i in 0..frames * 2 {
            match src.next() {
                Some(v) if i % 2 == 0 => out.push(v / mono_gains(0.0).0),
                Some(_) => {}
                None => break,
            }
        }
        out
    }

    #[test]
    fn a_looping_source_runs_past_the_end_of_the_clip() {
        let pan = Arc::new(AtomicU32::new(0.0f32.to_bits()));
        let mut parts = playback_sources(ramp(4, 4), true, Duration::ZERO, &pan);
        assert_eq!(parts.len(), 1, "no offset, so one source");
        let got = left_channel(&mut parts[0], 10);
        assert_eq!(
            got.iter()
                .map(|v| (v * 100.0).round() as i32)
                .collect::<Vec<_>>(),
            vec![1, 2, 3, 4, 1, 2, 3, 4, 1, 2],
            "the clip repeats from ITS OWN start"
        );
    }

    #[test]
    fn a_clip_that_does_not_loop_ends() {
        let pan = Arc::new(AtomicU32::new(0.0f32.to_bits()));
        let mut parts = playback_sources(ramp(4, 4), false, Duration::ZERO, &pan);
        let got = left_channel(&mut parts[0], 10);
        assert_eq!(got.len(), 4, "four samples and then silence, got {got:?}");
    }

    #[test]
    fn a_late_start_skips_into_the_first_pass_only() {
        // Half a second at 4 Hz is two samples in.  The looping case is TWO
        // sources: the late first pass, then the whole clip forever.
        let pan = Arc::new(AtomicU32::new(0.0f32.to_bits()));
        let offset = Duration::from_millis(500);
        let mut parts = playback_sources(ramp(4, 4), true, offset, &pan);
        assert_eq!(parts.len(), 2, "a late loop is a first pass and a repeat");
        let first = left_channel(&mut parts[0], 4);
        assert_eq!(
            first
                .iter()
                .map(|v| (v * 100.0).round() as i32)
                .collect::<Vec<_>>(),
            vec![3, 4],
            "the first pass starts two samples in"
        );
        let rest = left_channel(&mut parts[1], 4);
        assert_eq!(
            rest.iter()
                .map(|v| (v * 100.0).round() as i32)
                .collect::<Vec<_>>(),
            vec![1, 2, 3, 4],
            "and the repeat starts at the top — the offset is not baked in"
        );

        let mut once = playback_sources(ramp(4, 4), false, offset, &pan);
        let got = left_channel(&mut once[0], 4);
        assert_eq!(
            got.iter()
                .map(|v| (v * 100.0).round() as i32)
                .collect::<Vec<_>>(),
            vec![3, 4],
            "and a one-shot simply starts late"
        );
    }

    #[test]
    fn a_pan_change_reaches_a_source_that_is_already_playing() {
        // The atomic is the whole point: a pan set at play time would be a
        // constructor argument, and `audio_set_pan` would have nothing to write.
        let pan = Arc::new(AtomicU32::new(0.0f32.to_bits()));
        let mut parts = playback_sources(ramp(4, 4), false, Duration::ZERO, &pan);
        let centre_l = parts[0].next().expect("a first sample");
        let _centre_r = parts[0].next();
        pan.store((-1.0f32).to_bits(), Ordering::Relaxed);
        let left_l = parts[0].next().expect("a third sample");
        let left_r = parts[0].next().expect("a fourth sample");
        assert!(
            left_l > centre_l * 1.3,
            "hard left is louder on the left than centre was: {left_l} vs {centre_l}"
        );
        assert!(left_r.abs() < 1e-6, "and silent on the right: {left_r}");
    }

    #[test]
    fn a_sink_id_carries_its_generation() {
        // The slot number alone is what let a stale id stop a later sound.
        let id = 3i64 | (7i64 << SLOT_BITS);
        assert_eq!(id & SLOT_MASK, 3);
        assert_eq!(id >> SLOT_BITS, 7);
        assert!(id > 0, "an id stays non-negative: {id}");
    }
}
