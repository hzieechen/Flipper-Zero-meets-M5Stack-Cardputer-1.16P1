#include "mp3_decoder.h"
#include "mp3_player.h"
#include "mp3_i2s.h"

#include <string.h>

#include <storage/storage.h>
#include <esp_heap_caps.h>

#include "lib/helix/mp3dec.h"

#define TAG MP3_PLAYER_TAG

/* Raw input buffer holding bytes read from storage. Helix searches for the
 * sync word inside this window; 4 KB is comfortably larger than the largest
 * possible MP3 frame (1441 bytes for 320 kbps @ 32 kHz layer 3). */
#define READ_BUF_SIZE   8192
#define READ_CHUNK      4096
/* Largest Helix output frame: 1152 stereo samples = 4608 bytes. */
#define PCM_FRAMES_MAX  1152

typedef enum {
    TrackFormatMp3,
    TrackFormatWav,
} TrackFormat;

typedef struct {
    HMP3Decoder       hmp3;
    Storage*          storage;
    File*             file;

    uint8_t*          read_buf;     /* DRAM, fed from storage */
    uint8_t*          read_ptr;     /* current read position inside read_buf */
    int               read_left;    /* bytes still in read_buf at read_ptr */
    bool              eof;

    int16_t*          pcm;          /* internal RAM, holds one decoded frame */
    uint32_t          sample_rate;  /* current track sample rate */
    uint8_t           channels;

    TrackFormat       format;
    /* WAV-only: raw PCM, no decoder needed */
    uint32_t          wav_data_offset;
    uint32_t          wav_data_size;
    uint8_t           wav_bits;
    uint8_t           wav_block_align;

    uint64_t          file_size;
    uint64_t          file_pos;     /* bytes consumed from file */

    uint32_t          elapsed_ms;
    uint32_t          duration_ms;  /* 0 = unknown */

    FuriThread*       thread;
    volatile bool     playing;
    volatile bool     paused;
    volatile bool     pause_req;
    volatile bool     resume_req;

    Mp3DecoderEndedCallback ended_cb;
    void*                   ended_ctx;
    /* Set by mp3_decoder_stop() before joining the old thread so that an
     * intentional stop/switch does NOT fire the "track ended" callback.
     * Without this, switching tracks chained auto-advance all the way to the
     * last song: play() -> stop(old) -> old thread exit -> ended_cb -> play(next). */
    volatile bool     suppress_ended_cb;

    char              path[256];
} Mp3DecoderState;

static Mp3DecoderState* g_dec = NULL;

/* ------------- helpers ------------- */

static int decoder_refill(Mp3DecoderState* d) {
    /* Move remaining bytes to the start of the buffer, then read more. */
    if(d->read_left > 0 && d->read_ptr != d->read_buf) {
        memmove(d->read_buf, d->read_ptr, d->read_left);
    }
    d->read_ptr = d->read_buf;

    if(d->eof) return d->read_left;

    size_t want = READ_BUF_SIZE - d->read_left;
    if(want < READ_CHUNK) return d->read_left;

    uint16_t got = storage_file_read(d->file, d->read_buf + d->read_left, want);
    if(got == 0) {
        d->eof = true;
    } else {
        d->read_left += got;
        d->file_pos  += got;
    }
    return d->read_left;
}

/* Skip an ID3v2 header at the start of the file if present. ID3v2 layout:
 *   "ID3" + ver(2) + flags(1) + size(4, 7-bit-per-byte) + data...
 * Helix MP3FindSyncWord wouldn't find a frame inside the tag bytes anyway,
 * but for big tags (album art) the search would scan a lot of garbage. */
static void decoder_skip_id3v2(Mp3DecoderState* d) {
    if(d->read_left < 10) return;
    const uint8_t* b = d->read_ptr;
    if(b[0] != 'I' || b[1] != 'D' || b[2] != '3') return;
    uint32_t tag_size = ((uint32_t)(b[6] & 0x7F) << 21) |
                        ((uint32_t)(b[7] & 0x7F) << 14) |
                        ((uint32_t)(b[8] & 0x7F) <<  7) |
                        ((uint32_t)(b[9] & 0x7F));
    uint32_t total = tag_size + 10;
    if(total <= (uint32_t)d->read_left) {
        d->read_ptr  += total;
        d->read_left -= total;
    } else {
        uint32_t skip_in_buf = d->read_left;
        uint32_t skip_in_file = total - skip_in_buf;
        d->read_left = 0;
        d->read_ptr  = d->read_buf;
        storage_file_seek(d->file, skip_in_file, false);
        d->file_pos += skip_in_file;
    }
}

static void decoder_estimate_duration(Mp3DecoderState* d, const MP3FrameInfo* fi) {
    /* Crude CBR-based estimate: file size / bitrate. VBR files will be off,
     * but accurate enough for a progress bar. We compute it once on the
     * first valid frame. */
    if(d->duration_ms != 0 || fi->bitrate <= 0) return;
    uint64_t bits   = (uint64_t)d->file_size * 8;
    uint64_t millis = (bits * 1000) / (uint64_t)fi->bitrate;
    d->duration_ms = (uint32_t)millis;
}

/* ------------- WAV (raw PCM) support ------------- */

static uint16_t read_le16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* Detect "RIFF....WAVE" and parse the fmt/data chunks. We only accept
 * 16-bit PCM (the format the recorder writes); 8/24-bit and float are
 * rejected so we never emit garbage audio. False means "not a WAV we can
 * play" — the caller falls back to the MP3 path. */
static bool decoder_parse_wav(Mp3DecoderState* d) {
    uint8_t hdr[12];
    storage_file_seek(d->file, 0, true);
    if(storage_file_read(d->file, hdr, sizeof(hdr)) != sizeof(hdr)) return false;
    if(memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;

    uint64_t pos = 12;
    bool have_fmt = false;
    while(pos + 8 <= d->file_size) {
        storage_file_seek(d->file, (uint32_t)pos, true);
        uint8_t chunk[8];
        if(storage_file_read(d->file, chunk, sizeof(chunk)) != sizeof(chunk)) return false;
        uint32_t csize = read_le32(chunk + 4);
        uint64_t data_pos = pos + 8;

        if(memcmp(chunk, "fmt ", 4) == 0 && csize >= 16) {
            uint8_t fmt[16];
            if(storage_file_read(d->file, fmt, sizeof(fmt)) != sizeof(fmt)) return false;
            uint16_t audio_format = read_le16(fmt + 0);
            uint16_t channels = read_le16(fmt + 2);
            uint32_t sample_rate = read_le32(fmt + 4);
            uint16_t bits = read_le16(fmt + 14);

            /* 1 = PCM integer. Only 16-bit, mono/stereo supported. */
            if(audio_format != 1 || bits != 16 || (channels != 1 && channels != 2)) {
                FURI_LOG_W(TAG, "WAV: unsupported format=%u bits=%u ch=%u", audio_format, bits, channels);
                return false;
            }
            d->sample_rate = sample_rate;
            d->channels = (uint8_t)channels;
            d->wav_bits = (uint8_t)bits;
            d->wav_block_align = (uint8_t)(channels * bits / 8);
            have_fmt = true;
        } else if(memcmp(chunk, "data", 4) == 0) {
            if(!have_fmt) return false; /* fmt must come before data */
            d->wav_data_offset = (uint32_t)data_pos;
            d->wav_data_size = csize;
            if(d->wav_block_align == 0 || d->sample_rate == 0) return false;
            d->duration_ms = (uint32_t)(((uint64_t)csize * 1000) /
                                        ((uint64_t)d->sample_rate * d->wav_block_align));
            FURI_LOG_I(TAG, "WAV: %lu Hz, %u ch, 16-bit, %lu ms",
                       (unsigned long)d->sample_rate, d->channels,
                       (unsigned long)d->duration_ms);
            return true;
        }

        /* Chunks are word-aligned; skip padding byte if size is odd. */
        pos = data_pos + csize + (csize & 1u);
    }
    return false;
}

/* WAV playback: read raw PCM in manageable batches, expand mono to stereo,
 * push to the same I2S ring buffer the MP3 path uses. Volume/pause/ended
 * behaviour is shared with MP3 by design. */
static int32_t decoder_task_wav(void* ctx) {
    Mp3DecoderState* d = ctx;
    FURI_LOG_I(TAG, "WAV worker started");
    const size_t max_frames = PCM_FRAMES_MAX;
    uint8_t* batch = d->read_buf; /* reuse the 8 KB read buffer for raw bytes */
    uint32_t done = 0;

    /* Parse the header here in the worker thread, NOT in play() on the UI
     * thread — file IO on the UI thread while holding the app mutex is a
     * known crash source on this port. */
    if(!decoder_parse_wav(d)) {
        FURI_LOG_E(TAG, "WAV header parse failed");
        d->playing = false;
        if(d->file) storage_file_close(d->file);
        mp3_i2s_flush();
        if(d->ended_cb && !d->suppress_ended_cb) d->ended_cb(d->ended_ctx);
        return 0;
    }

    mp3_i2s_set_sample_rate(d->sample_rate);
    storage_file_seek(d->file, d->wav_data_offset, true);

    while(d->playing && done < d->wav_data_size) {
        if(d->pause_req)  { d->paused = true;  d->pause_req  = false; }
        if(d->resume_req) { d->paused = false; d->resume_req = false; }
        if(d->paused) {
            furi_delay_ms(50);
            continue;
        }

        uint32_t remaining = d->wav_data_size - done;
        /* Read at most one push-worth of frames. Reading more would advance
         * the file pointer past bytes we don't push, dropping audio. */
        size_t max_bytes = max_frames * d->wav_block_align;
        size_t want = remaining < max_bytes ? (size_t)remaining : max_bytes;
        want -= want % d->wav_block_align; /* whole frames only */
        if(want == 0) break;

        uint16_t got = storage_file_read(d->file, batch, want);
        if(got == 0) break;
        got -= got % d->wav_block_align;
        if(got == 0) break;
        /* want <= max_bytes = max_frames * block_align, so no frame cap needed. */
        size_t frames = got / d->wav_block_align;

        if(d->channels == 1) {
            /* Mono int16 -> stereo: expand back-to-front into pcm[]. */
            int16_t* dst = d->pcm;
            const int16_t* src = (const int16_t*)batch;
            for(size_t i = frames; i > 0; i--) {
                int16_t s = src[i - 1];
                dst[(i - 1) * 2 + 0] = s;
                dst[(i - 1) * 2 + 1] = s;
            }
            mp3_i2s_push(dst, frames, 1000);
        } else {
            /* Already interleaved stereo int16. */
            memcpy(d->pcm, batch, got);
            mp3_i2s_push(d->pcm, frames, 1000);
        }

        done += got;
        d->file_pos += got;
        d->elapsed_ms += (uint32_t)(((uint64_t)frames * 1000) / d->sample_rate);
    }

    /* Let the I2S queue drain before signalling end-of-track. */
    while(mp3_i2s_has_pending() && d->playing) furi_delay_ms(50);

    d->playing = false;
    d->paused = false;
    if(d->file) storage_file_close(d->file);
    mp3_i2s_flush();
    if(d->ended_cb && !d->suppress_ended_cb) d->ended_cb(d->ended_ctx);
    return 0;
}

/* ------------- decoder thread ------------- */

static int32_t decoder_task(void* ctx) {
    Mp3DecoderState* d = ctx;
    bool first_frame = true;
    FURI_LOG_I(TAG, "MP3 worker started");

    /* Pre-read happens here (in the worker thread) so the UI thread doesn't
     * stall on SD-card IO while holding the app mutex. */
    decoder_refill(d);
    decoder_skip_id3v2(d);

    while(d->playing) {
        /* Handle pause/resume signals via volatile bools. */
        if(d->pause_req)  { d->paused = true;  d->pause_req  = false; }
        if(d->resume_req) { d->paused = false; d->resume_req = false; }
        if(d->paused) {
            furi_delay_ms(50);
            continue;
        }

        /* Make sure we have enough bytes for a full frame. */
        decoder_refill(d);
        if(d->read_left < 4) {
            /* True EOF — wait for the I2S queue to drain, then notify. */
            if(d->eof) {
                while(mp3_i2s_has_pending() && d->playing) {
                    furi_delay_ms(50);
                }
                break;
            }
            continue;
        }

        int sync = MP3FindSyncWord(d->read_ptr, d->read_left);
        if(sync < 0) {
            /* Discard everything we scanned and try again. */
            d->read_left = 0;
            d->read_ptr  = d->read_buf;
            continue;
        }
        d->read_ptr  += sync;
        d->read_left -= sync;

        unsigned char* in_ptr = d->read_ptr;
        int bytes_left = d->read_left;
        int err = MP3Decode(d->hmp3, &in_ptr, &bytes_left, d->pcm, 0);
        if(err != ERR_MP3_NONE) {
            if(err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
                /* Not enough bytes — refill and retry without advancing. */
                continue;
            }
            FURI_LOG_W(TAG, "MP3Decode err=%d, skipping byte", err);
            d->read_ptr++;
            d->read_left--;
            continue;
        }
        size_t consumed = (size_t)(in_ptr - d->read_ptr);
        d->read_ptr  = in_ptr;
        d->read_left = bytes_left;

        MP3FrameInfo fi;
        MP3GetLastFrameInfo(d->hmp3, &fi);

        if(first_frame) {
            d->sample_rate = fi.samprate;
            d->channels    = fi.nChans;
            mp3_i2s_set_sample_rate(fi.samprate);
            decoder_estimate_duration(d, &fi);
            first_frame = false;
            FURI_LOG_I(TAG, "first frame: %d Hz, %d ch, %d kbps",
                       fi.samprate, fi.nChans, fi.bitrate / 1000);
        } else if((uint32_t)fi.samprate != d->sample_rate) {
            d->sample_rate = fi.samprate;
            mp3_i2s_set_sample_rate(fi.samprate);
        }

        /* Convert / push to I2S. fi.outputSamps is total int16 samples in pcm,
         * i.e. nChans * frame_samples (1152 for layer 3). */
        size_t frames = (size_t)fi.outputSamps / (size_t)fi.nChans;
        if(fi.nChans == 1) {
            /* Helix wrote mono into pcm[0..frames). Expand to stereo in place
             * by walking back-to-front. */
            for(int i = (int)frames - 1; i >= 0; i--) {
                int16_t s = d->pcm[i];
                d->pcm[i * 2 + 0] = s;
                d->pcm[i * 2 + 1] = s;
            }
        }
        mp3_i2s_push(d->pcm, frames, 1000);

        /* Update progress. */
        d->elapsed_ms += (uint32_t)((uint64_t)frames * 1000 / fi.samprate);
        (void)consumed;
    }

    /* Cleanup of per-track state — release I2S queue + file. */
    d->playing = false;
    d->paused  = false;
    if(d->file) {
        storage_file_close(d->file);
    }
    mp3_i2s_flush();

    /* Only a natural end-of-track (or unrecoverable decode error) notifies the
     * app. A stop caused by switching tracks / leaving the app sets the flag
     * and must stay silent, otherwise it triggers playlist auto-advance. */
    if(d->ended_cb && !d->suppress_ended_cb) d->ended_cb(d->ended_ctx);
    return 0;
}

/* ------------- public API ------------- */

bool mp3_decoder_init(void) {
    if(g_dec) return true;

    Mp3DecoderState* d = heap_caps_calloc(1, sizeof(Mp3DecoderState), MALLOC_CAP_DEFAULT);
    if(!d) return false;

    d->read_buf = heap_caps_malloc(READ_BUF_SIZE, MALLOC_CAP_8BIT);  /* DRAM is fine */
    d->pcm      = heap_caps_malloc(PCM_FRAMES_MAX * 2 * sizeof(int16_t), MALLOC_CAP_DEFAULT);
    if(!d->read_buf || !d->pcm) {
        if(d->read_buf) heap_caps_free(d->read_buf);
        if(d->pcm)      heap_caps_free(d->pcm);
        heap_caps_free(d);
        return false;
    }

    d->hmp3 = MP3InitDecoder();
    if(!d->hmp3) {
        heap_caps_free(d->read_buf);
        heap_caps_free(d->pcm);
        heap_caps_free(d);
        return false;
    }

    d->storage = furi_record_open(RECORD_STORAGE);
    d->file    = storage_file_alloc(d->storage);

    /* Allocate the decoder thread ONCE, here in init, while the heap is still
     * contiguous. Allocating it inside play() required a fresh 8 KB contiguous
     * internal block on every track start; on this PSRAM-less board the heap
     * fragments and that allocation fails with a furi_check() abort. The
     * thread object is kept for the lifetime of the app and only started/
     * joined per track (see play()/stop()).
     *
     * furi_thread_alloc_ex() aborts via furi_check() if the stack cannot be
     * allocated, so size it from the largest contiguous internal block. Leave
     * ~1 KB headroom for the thread struct/name allocated immediately before
     * the stack inside alloc_ex(). */
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t stack_sz = (largest >= 8192 + 1024) ? 8192 : 4096;
    FURI_LOG_I(TAG, "decoder stack=%u largest=%u", (unsigned)stack_sz, (unsigned)largest);
    d->thread = furi_thread_alloc_ex("Mp3Decoder", stack_sz, decoder_task, d);
    if(!d->thread) {
        storage_file_free(d->file);
        furi_record_close(RECORD_STORAGE);
        MP3FreeDecoder(d->hmp3);
        heap_caps_free(d->read_buf);
        heap_caps_free(d->pcm);
        heap_caps_free(d);
        return false;
    }
    furi_thread_set_priority(d->thread, FuriThreadPriorityNormal);

    g_dec = d;
    return true;
}

void mp3_decoder_deinit(void) {
    if(!g_dec) return;
    mp3_decoder_stop();
    /* The thread (and its stack) lives as long as the app: release it here. */
    if(g_dec->thread) {
        furi_thread_free(g_dec->thread);
        g_dec->thread = NULL;
    }
    if(g_dec->file)    storage_file_free(g_dec->file);
    if(g_dec->storage) furi_record_close(RECORD_STORAGE);
    if(g_dec->hmp3)    MP3FreeDecoder(g_dec->hmp3);
    heap_caps_free(g_dec->read_buf);
    heap_caps_free(g_dec->pcm);
    heap_caps_free(g_dec);
    g_dec = NULL;
}

void mp3_decoder_set_ended_callback(Mp3DecoderEndedCallback cb, void* ctx) {
    if(!g_dec) return;
    g_dec->ended_cb  = cb;
    g_dec->ended_ctx = ctx;
}

bool mp3_decoder_play(const char* path) {
    if(!g_dec) return false;
    mp3_decoder_stop();

    strncpy(g_dec->path, path, sizeof(g_dec->path) - 1);
    g_dec->path[sizeof(g_dec->path) - 1] = '\0';

    if(!storage_file_open(g_dec->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_E(TAG, "open failed: %s", path);
        return false;
    }
    g_dec->file_size   = storage_file_size(g_dec->file);
    g_dec->file_pos    = 0;
    g_dec->read_left   = 0;
    g_dec->read_ptr    = g_dec->read_buf;
    g_dec->eof         = false;
    g_dec->elapsed_ms  = 0;
    g_dec->duration_ms = 0;
    g_dec->paused      = false;
    g_dec->pause_req   = false;
    g_dec->resume_req  = false;
    g_dec->channels    = 2;

    /* Choose the worker by filename extension only — no file IO here.
     * The WAV header is parsed inside decoder_task_wav, on the worker thread.
     * MP3 keeps the exact same path it has always used. */
    size_t plen = strlen(path);
    g_dec->format =
        (plen >= 4 && path[plen - 4] == '.' &&
         (path[plen - 3] == 'w' || path[plen - 3] == 'W') &&
         (path[plen - 2] == 'a' || path[plen - 2] == 'A') &&
         (path[plen - 1] == 'v' || path[plen - 1] == 'V'))
            ? TrackFormatWav
            : TrackFormatMp3;
    g_dec->file_pos = 0;

    /* Reuse the persistent thread allocated in init(): no heap allocation
     * happens on the play path any more. The worker callback is switched per
     * format while the thread is in Stopped state. */
    if(!g_dec->thread) return false;
    furi_thread_set_callback(
        g_dec->thread,
        g_dec->format == TrackFormatWav ? decoder_task_wav : decoder_task);

    /* Clear the suppression set by the stop() above: this track is real, so
     * its natural EOF must notify the app again. */
    g_dec->suppress_ended_cb = false;
    g_dec->playing = true;
    furi_thread_start(g_dec->thread);
    return true;
}

void mp3_decoder_pause(void) {
    if(!g_dec) return;
    g_dec->pause_req = true;
}

void mp3_decoder_resume(void) {
    if(!g_dec) return;
    g_dec->resume_req = true;
}

void mp3_decoder_stop(void) {
    if(!g_dec || !g_dec->thread) return;
    /* Tell the old thread to exit silently: an intentional stop (leave app /
     * switch track) is not a natural track-end and must not auto-advance. */
    g_dec->suppress_ended_cb = true;
    g_dec->playing = false;

    /* Never free the thread here — its 8 KB stack is the one allocated in
     * init() and is reused for every track. join() waits for Stopping->Stopped
     * via the scrub thread; furi_thread_set_callback/start in play() require
     * state == Stopped, so make sure we actually reach it before returning. */
    if(furi_thread_get_state(g_dec->thread) != FuriThreadStateStopped) {
        furi_thread_join(g_dec->thread);
        while(furi_thread_get_state(g_dec->thread) != FuriThreadStateStopped) {
            furi_delay_ms(2);
        }
    }
}

bool mp3_decoder_is_playing(void) {
    return g_dec && g_dec->playing && !g_dec->paused;
}

bool mp3_decoder_is_paused(void) {
    return g_dec && g_dec->paused;
}

void mp3_decoder_get_progress(uint32_t* out_elapsed_ms, uint32_t* out_duration_ms) {
    if(!g_dec) {
        if(out_elapsed_ms)  *out_elapsed_ms  = 0;
        if(out_duration_ms) *out_duration_ms = 0;
        return;
    }
    if(out_elapsed_ms)  *out_elapsed_ms  = g_dec->elapsed_ms;
    if(out_duration_ms) *out_duration_ms = g_dec->duration_ms;
}
