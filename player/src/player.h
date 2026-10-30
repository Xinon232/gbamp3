/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). */
#pragma once
#include <stdint.h>
struct PlayerStats {uint32_t magic,ready,fatal,decoded_frames,decode_cycles,conversion_cycles,irq_cycles,ui_cycles,output_samples,underruns,min_queue,max_queue,transitions,current_track,track_samples,paused,locked,seeks,controls,arena_peak,main_margin,irq_margin,clock_base,clock_rem,period_error,produced,consumed,loops,output_mode,mode_changes,generation,anchor_frame,anchor_track,browser_open,browser_order,browser_selection,browser_error,info_page,sound_mode,cache_audio,cache_sound_mode,cache_rate,decoder,decoder_swaps,boost,anchor_pos,update_cycles,cover_decodes,library_songs,mp3_quality,
 sleeps,halt_cycles,   /* v1.9: Sleep (Stop) entries; cycles the main loop spent halted while music played */
 conv_cycles,conv_frames,conv_result,   /* conversion: cycles, 32768 Hz frames written, last result */
 /* v2.2 Volume Boost from the stream anchor: the level there, the number of
  * changes since, and the first BOOST_LOG of them (ring position << 2 | level). */
 anchor_boost,boost_changes,boost_at[8];};
#define BOOST_LOG 8u
extern volatile struct PlayerStats stats;

/* Clean mono/stereo only; retain legacy clean telemetry values 1 and 3. */
static inline unsigned player_sound_next(unsigned mode){return mode==1u?3u:1u;}
static inline unsigned player_sound_mono(unsigned mode){return mode==1u;}
static inline const char *player_sound_label(unsigned mode){return mode==1u?"MONO":"STEREO";}
/* v1.1: MP3 plays at 32768 Hz in Stereo and Mono, one sample per 32.768 kHz
 * PWM cycle like 32768 Hz GAC. At 16384/21845 Hz (v0.9.9-v1.0) each sample
 * spanned 2-3 PWM cycles and, on hardware, the timer's random start point
 * within the PWM cycle made MP3 sound screechy after some starts. */
#define PLAYER_RATE 32768
/* Header 44151 is nominal: timer380 gives 44150.568421... Hz.
 * Elapsed/seek use header rate, a roughly 10ppm timebase mismatch. */
static inline unsigned player_cache_period(unsigned rate){return rate==44151?380u:16777216u/rate;}
static inline unsigned player_mode_edge(unsigned edge,unsigned locked,unsigned fatal){return !locked&&!fatal&&!!(edge&4);}
static inline unsigned player_output_rate(unsigned mono){(void)mono;return 32768;}
static inline unsigned player_output_den(unsigned mono){(void)mono;return 1;}
static inline unsigned player_output_seconds(unsigned samples,unsigned mono){unsigned rate=player_output_rate(mono),den=player_output_den(mono);return (samples/rate)*den+(samples%rate)*den/rate;}
static inline unsigned player_output_period(unsigned mono){(void)mono;return 512;}
