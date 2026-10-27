#include <string.h>
#include <math.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_hf_client_api.h"
#include "esp_hf_client_legacy_api.h"
#include "board_config.h"
#include "es8388.h"
#include "audio_io.h"
#include "audio_engine.h"

static const char *TAG = "audio";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static RingbufHandle_t music_ring, call_rx_ring, call_tx_ring;

typedef enum { SRC_MUSIC, SRC_CALL } source_t;
static struct {
    unsigned epoch, music_rate, music_channels, music_volume, music_queued;
    unsigned call_rate, call_volume, dac_profile, test_mode;
    source_t source;
    bool music_active, ring, diagnose;
    uint32_t music_drops, music_underruns, call_rx_drops, call_tx_drops, call_underruns, i2s_errors;
} st = {.music_rate = 44100, .music_channels = 2, .music_volume = MUSIC_BOOT_VOLUME,
        .call_rate = 8000, .call_volume = CALL_BOOT_VOLUME, .dac_profile = AUDIO_BOOT_DAC_PROFILE};
typedef struct { unsigned epoch, channels; uint32_t length; uint8_t data[]; } packet_t;

static const int16_t sine32[] = {
    0,1598,3135,4551,5793,6811,7568,8035,8192,8035,7568,6811,5793,4551,3135,1598,
    0,-1598,-3135,-4551,-5793,-6811,-7568,-8035,-8192,-8035,-7568,-6811,-5793,-4551,-3135,-1598
};

#define BUMP() do { portENTER_CRITICAL(&lock); ++st.epoch; portEXIT_CRITICAL(&lock); } while (0)

// ---------------------------------------------------------------- music API
void audio_engine_music_format(unsigned rate, unsigned channels)
{
    portENTER_CRITICAL(&lock);
    st.music_rate = rate; st.music_channels = channels; ++st.epoch;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Music PCM %u Hz, %u channel(s)", rate, channels);
}
void audio_engine_music_stream(bool active)
{
    // Idempotent: a repeated "stopped" must not reclock the bus again.
    portENTER_CRITICAL(&lock);
    if (st.music_active != active) { st.music_active = active; ++st.epoch; }
    portEXIT_CRITICAL(&lock);
}
void audio_engine_music_set_volume(unsigned percent)
{
    if (percent > 100) percent = 100;
    portENTER_CRITICAL(&lock); st.music_volume = percent; portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Music volume %u%%", percent);
}
unsigned audio_engine_music_get_volume(void)
{
    portENTER_CRITICAL(&lock); unsigned v = st.music_volume; portEXIT_CRITICAL(&lock);
    return v;
}
void audio_engine_music_push(const uint8_t *data, uint32_t length)
{
    unsigned epoch, channels; bool active; source_t source;
    portENTER_CRITICAL(&lock);
    active = st.music_active; epoch = st.epoch; channels = st.music_channels; source = st.source;
    portEXIT_CRITICAL(&lock);
    if (!active || source != SRC_MUSIC || !length || length % (channels * sizeof(int16_t))) return;
    packet_t *p;
    if (xRingbufferSendAcquire(music_ring, (void **)&p, sizeof(*p) + length, 0) != pdTRUE) {
        portENTER_CRITICAL(&lock); ++st.music_drops; portEXIT_CRITICAL(&lock); return;
    }
    p->epoch = epoch; p->channels = channels; p->length = length;
    memcpy(p->data, data, length);
    portENTER_CRITICAL(&lock); st.music_queued += length; portEXIT_CRITICAL(&lock);
    xRingbufferSendComplete(music_ring, p);
}

// ----------------------------------------------------------------- call API
void audio_engine_call_start(unsigned rate)
{
    portENTER_CRITICAL(&lock);
    st.source = SRC_CALL; st.call_rate = rate; st.ring = false; ++st.epoch;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "CALL audio start: %u Hz mono both ways", rate);
}
void audio_engine_call_stop(void)
{
    portENTER_CRITICAL(&lock);
    bool was_call = st.source == SRC_CALL;
    if (was_call) { st.source = SRC_MUSIC; ++st.epoch; }
    portEXIT_CRITICAL(&lock);
    if (was_call) ESP_LOGI(TAG, "CALL audio stop; back to music path");
}
bool audio_engine_call_active(void)
{
    portENTER_CRITICAL(&lock); bool a = st.source == SRC_CALL; portEXIT_CRITICAL(&lock);
    return a;
}
// Byte rings hand data back in two pieces at the wrap point. Read exactly
// `want` bytes (possibly in two steps) or nothing at all, so callers never see
// a short read that only looks like an underrun.
static size_t ring_read_exact(RingbufHandle_t ring, size_t capacity, void *dst, size_t want)
{
    if (capacity - xRingbufferGetCurFreeSize(ring) < want) return 0;
    size_t done = 0;
    while (done < want) {
        size_t got = 0;
        uint8_t *data = xRingbufferReceiveUpTo(ring, &got, 0, want - done);
        if (!data) break;
        memcpy((uint8_t *)dst + done, data, got);
        vRingbufferReturnItem(ring, data);
        done += got;
    }
    return done;
}

void audio_engine_call_push(const uint8_t *pcm, uint32_t length)
{
    // Bluetooth callback (phone voice). Byte ring: partial frames are fine.
    if (!length) return;
    if (xRingbufferSend(call_rx_ring, pcm, length, 0) != pdTRUE) {
        portENTER_CRITICAL(&lock); ++st.call_rx_drops; portEXIT_CRITICAL(&lock);
    }
}
uint32_t audio_engine_call_pull(uint8_t *pcm, uint32_t length)
{
    // Bluetooth asks for exactly `length` bytes of microphone PCM; the stack
    // discards partial reads, so hand out full chunks only.
    if (length > CALL_TX_BUFFER_BYTES) return 0;
    return ring_read_exact(call_tx_ring, CALL_TX_BUFFER_BYTES, pcm, length);
}
void audio_engine_call_set_volume(unsigned level)
{
    if (level > 15) level = 15;
    portENTER_CRITICAL(&lock); st.call_volume = level; portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Call speaker volume %u/15", level);
}
unsigned audio_engine_call_get_volume(void)
{
    portENTER_CRITICAL(&lock); unsigned v = st.call_volume; portEXIT_CRITICAL(&lock);
    return v;
}
void audio_engine_ring(bool on)
{
    portENTER_CRITICAL(&lock);
    bool changed = st.ring != on; st.ring = on; if (changed) ++st.epoch;
    portEXIT_CRITICAL(&lock);
    if (changed) ESP_LOGI(TAG, "Local ring tone %s", on ? "ON" : "OFF");
}

// ----------------------------------------------------------- diagnostics API
void audio_engine_test_mode(unsigned mode)
{
    if (mode > 3) return;
    portENTER_CRITICAL(&lock); st.test_mode = mode; ++st.epoch; portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Test mode %u: 0=Bluetooth, 1=left 400 Hz, 2=right 800 Hz, 3=silence", mode);
}
void audio_engine_dac_step(int delta)
{
    portENTER_CRITICAL(&lock); int p = (int)st.dac_profile; portEXIT_CRITICAL(&lock);
    p += delta;
    if (p < 0) p = 0;
    if (p > (int)es8388_dac_profile_max()) p = es8388_dac_profile_max();
    portENTER_CRITICAL(&lock); st.dac_profile = p; ++st.epoch; portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "DAC profile P%d requested; reboot returns to P%u", p, (unsigned)AUDIO_BOOT_DAC_PROFILE);
}
void audio_engine_dac_reset(void)
{
    portENTER_CRITICAL(&lock); st.dac_profile = AUDIO_BOOT_DAC_PROFILE; ++st.epoch; portEXIT_CRITICAL(&lock);
}
void audio_engine_log_status(void)
{
    portENTER_CRITICAL(&lock); st.diagnose = true; portEXIT_CRITICAL(&lock);
}

// ------------------------------------------------------------- DSP helpers
// One limiter gain for both channels keeps the stereo image; fast attack
// prevents overflow, ~150 ms release avoids pumping.
static bool scale_frame(int32_t left, int32_t right, float gain, float max_gain,
                        float *ceiling, float release_step, int16_t *out)
{
    uint32_t peak = left < 0 ? -left : left;
    uint32_t right_peak = right < 0 ? -right : right;
    if (right_peak > peak) peak = right_peak;
    float allowed = peak ? MUSIC_PCM_PEAK_LIMIT / (float)peak : max_gain;
    if (*ceiling > allowed) *ceiling = allowed;
    bool limited = gain > *ceiling;
    float applied = limited ? *ceiling : gain;
    out[0] = (int16_t)(left * applied);
    out[1] = (int16_t)(right * applied);
    *ceiling += release_step;
    if (*ceiling > max_gain) *ceiling = max_gain;
    return limited;
}

static inline int16_t clip16(int32_t v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
}

static void pa(bool on, bool *pa_on)
{
    if (on == *pa_on) return;
    if (on) es8388_dac_mute(false);
    gpio_set_level(BOARD_PA_ENABLE, on);
    if (!on) es8388_dac_mute(true);
    *pa_on = on;
}

// --------------------------------------------------------------- the task
static void audio_task(void *arg)
{
    int16_t block[AUDIO_IO_MAX_FRAMES * 2] = {0};
    int16_t mic[AUDIO_IO_MAX_FRAMES * 2] = {0};
    int16_t voice[AUDIO_IO_MAX_FRAMES] = {0}; // one 10 ms block of phone PCM (mono)
    unsigned last_epoch = UINT32_MAX;
    bool primed = false, pa_on = false, call_primed = false;
    float gain = 0.0f, ceiling = 1.0f;
    int16_t last_l = 0, last_r = 0;
    uint32_t phase = 0, phase2 = 0, in_peak = 0, out_peak = 0, mic_peak = 0, limited = 0;
    uint64_t frames_out = 0;
    int64_t prefill_since = esp_timer_get_time(), last_report = prefill_since, ring_since = prefill_since;
    while (1) {
        unsigned epoch, mode, profile, rate, music_volume, call_volume, queued;
        bool music_active, ring, diagnose; source_t source;
        portENTER_CRITICAL(&lock);
        epoch = st.epoch; mode = st.test_mode; profile = st.dac_profile; source = st.source;
        music_active = st.music_active; ring = st.ring; music_volume = st.music_volume;
        call_volume = st.call_volume; queued = st.music_queued;
        rate = source == SRC_CALL ? st.call_rate : st.music_rate;
        diagnose = st.diagnose; st.diagnose = false;
        portEXIT_CRITICAL(&lock);
        if (diagnose) { es8388_log_readback(); audio_io_log_clocks(); }

        if (epoch != last_epoch) {
            // Any source/rate/profile change: quiet the PA, reclock the bus, flush stale audio.
            pa(false, &pa_on);
            es8388_apply_dac_profile(profile);
            bool call = source == SRC_CALL;
            es8388_adc_power(call);
            ESP_ERROR_CHECK(audio_io_set_rate(rate, call));
            size_t n; void *stale;
            while ((stale = xRingbufferReceive(music_ring, &n, 0))) {
                portENTER_CRITICAL(&lock); st.music_queued -= ((packet_t *)stale)->length; portEXIT_CRITICAL(&lock);
                vRingbufferReturnItem(music_ring, stale);
            }
            while ((stale = xRingbufferReceiveUpTo(call_rx_ring, &n, 0, CALL_RX_BUFFER_BYTES))) vRingbufferReturnItem(call_rx_ring, stale);
            while ((stale = xRingbufferReceiveUpTo(call_tx_ring, &n, 0, CALL_TX_BUFFER_BYTES))) vRingbufferReturnItem(call_tx_ring, stale);
            gain = 0; last_l = last_r = 0; primed = call_primed = false; ceiling = 1.0f;
            prefill_since = ring_since = esp_timer_get_time(); last_epoch = epoch;
            ESP_LOGI(TAG, "Path: %s at %u Hz%s%s", call ? "CALL" : "MUSIC", rate,
                     ring ? " + ring tone" : "", mode ? " (test tone)" : "");
        }

        // ---------------- CALL: phone voice out, microphones in ----------------
        if (source == SRC_CALL) {
            size_t frames = rate / 100; // 10 ms blocks: 80 @ 8 kHz, 160 @ 16 kHz
            size_t want = frames * sizeof(int16_t), got = 0;
            if (!call_primed) {
                size_t waiting = CALL_RX_BUFFER_BYTES - xRingbufferGetCurFreeSize(call_rx_ring);
                if (waiting >= CALL_PREFILL_BYTES || esp_timer_get_time() - prefill_since > 300000) call_primed = true;
            }
            if (call_primed) got = ring_read_exact(call_rx_ring, CALL_RX_BUFFER_BYTES, voice, want);
            size_t have = got / sizeof(int16_t);
            if (call_primed && have < frames) { portENTER_CRITICAL(&lock); ++st.call_underruns; portEXIT_CRITICAL(&lock); }
            float level = call_volume / 15.0f;
            for (size_t i = 0; i < frames; ++i) {
                int32_t s = i < have ? voice[i] : 0;
                if (i >= have) s = last_l * (int32_t)(frames - i) / (int32_t)frames; // fade to silence
                int16_t v = clip16((int32_t)(s * level));
                block[2*i] = block[2*i+1] = v;
                last_l = i < have ? v : 0;
                uint32_t pk = v < 0 ? -(int32_t)v : v; if (pk > out_peak) out_peak = pk;
            }
            pa(true, &pa_on);
            if (!audio_io_write(block, frames)) { portENTER_CRITICAL(&lock); ++st.i2s_errors; portEXIT_CRITICAL(&lock); }
            // Microphones: same frame count keeps both directions in lock step.
            if (audio_io_read(mic, frames)) {
                int16_t mono[AUDIO_IO_MAX_FRAMES];
                for (size_t i = 0; i < frames; ++i) {
                    int32_t m = ((int32_t)mic[2*i] + mic[2*i+1]) / 2; // both onboard mics
                    mono[i] = clip16((int32_t)(m * MIC_DIGITAL_GAIN));
                    uint32_t pk = mono[i] < 0 ? -(int32_t)mono[i] : mono[i]; if (pk > mic_peak) mic_peak = pk;
                }
                if (xRingbufferSend(call_tx_ring, mono, frames * sizeof(int16_t), 0) != pdTRUE) {
                    portENTER_CRITICAL(&lock); ++st.call_tx_drops; portEXIT_CRITICAL(&lock);
                }
                esp_hf_client_outgoing_data_ready();
            } else {
                portENTER_CRITICAL(&lock); ++st.i2s_errors; portEXIT_CRITICAL(&lock);
            }
            frames_out += frames;
            goto report;
        }

        // ---------------- test tones / local ring tone ----------------
        bool ringing = ring && !music_active && !mode;
        if (mode || ringing) {
            size_t n; void *stale = xRingbufferReceive(music_ring, &n, 0);
            if (stale) {
                portENTER_CRITICAL(&lock); st.music_queued -= ((packet_t *)stale)->length; portEXIT_CRITICAL(&lock);
                vRingbufferReturnItem(music_ring, stale);
            }
            int64_t t = (esp_timer_get_time() - ring_since) / 1000 % (CALL_RING_ON_MS + CALL_RING_OFF_MS);
            bool burst = !ringing || t < CALL_RING_ON_MS;
            float level = music_volume / 100.0f, target = level * level;
            float step = 1.0f / (rate * 0.02f);
            uint32_t d1 = ((uint64_t)(mode == 2 ? 800 : mode == 1 ? 400 : 425) << 32) / rate;
            uint32_t d2 = ((uint64_t)480 << 32) / rate; // ring: 425 + 480 Hz dual tone
            for (int i = 0; i < 256; ++i) {
                float want = burst && mode != 3 ? target : 0.0f;
                if (gain < want) { gain += step; if (gain > want) gain = want; }
                else if (gain > want) { gain -= step; if (gain < want) gain = want; }
                int32_t s = sine32[phase >> 27];
                if (ringing) s = (s + sine32[phase2 >> 27]) / 2;
                phase += d1; phase2 += d2;
                int16_t v = (int16_t)(s * gain);
                block[2*i] = mode == 2 ? 0 : v;
                block[2*i+1] = mode == 1 ? 0 : v;
            }
            pa(true, &pa_on);
            if (!audio_io_write(block, 256)) { portENTER_CRITICAL(&lock); ++st.i2s_errors; portEXIT_CRITICAL(&lock); }
            goto report;
        }

        // ---------------- MUSIC ----------------
        bool consume = !music_active || primed || queued >= MUSIC_PREFILL_BYTES ||
                       esp_timer_get_time() - prefill_since > 150000;
        size_t item_size = 0;
        packet_t *p = consume ? xRingbufferReceive(music_ring, &item_size, 0) : NULL;
        if (p) {
            portENTER_CRITICAL(&lock); st.music_queued -= p->length; portEXIT_CRITICAL(&lock);
            if (!music_active || p->epoch != epoch) { vRingbufferReturnItem(music_ring, p); continue; }
            primed = true;
            pa(true, &pa_on);
            const int16_t *in = (const int16_t *)p->data;
            size_t frames = p->length / (p->channels * sizeof(int16_t));
            float level = music_volume / 100.0f, target = level * level; // digital gain 1x at 100%
            float step = 1.0f / (rate * 0.04f), release_step = 1.0f / (rate * 0.15f);
            for (size_t pos = 0; pos < frames;) {
                size_t n = frames - pos; if (n > 256) n = 256;
                for (size_t i = 0; i < n; ++i) {
                    if (gain < target) { gain += step; if (gain > target) gain = target; }
                    else if (gain > target) { gain -= step; if (gain < target) gain = target; }
                    size_t src = (pos + i) * p->channels;
                    int32_t il = in[src], ir = in[src + (p->channels == 2)];
                    uint32_t pk = il < 0 ? -il : il; if (pk > in_peak) in_peak = pk;
                    pk = ir < 0 ? -ir : ir; if (pk > in_peak) in_peak = pk;
                    if (scale_frame(il, ir, gain, 1.0f, &ceiling, release_step, &block[2*i])) ++limited;
                    last_l = block[2*i]; last_r = block[2*i+1];
                    pk = last_l < 0 ? -(int32_t)last_l : last_l; if (pk > out_peak) out_peak = pk;
                }
                if (!audio_io_write(block, n)) { portENTER_CRITICAL(&lock); ++st.i2s_errors; portEXIT_CRITICAL(&lock); break; }
                pos += n; frames_out += n;
            }
            vRingbufferReturnItem(music_ring, p);
        } else {
            if (primed && music_active) { portENTER_CRITICAL(&lock); ++st.music_underruns; portEXIT_CRITICAL(&lock); }
            for (int i = 0; i < 256; ++i) { // fade from the last sample, then rebuffer
                block[2*i] = (int32_t)last_l * (255-i) / 256;
                block[2*i+1] = (int32_t)last_r * (255-i) / 256;
            }
            audio_io_write(block, 256);
            last_l = last_r = 0; gain = 0;
            if (primed) { primed = false; prefill_since = esp_timer_get_time(); }
            if (!music_active) pa(false, &pa_on);
        }

report:
        if (esp_timer_get_time() - last_report >= 5000000) {
            portENTER_CRITICAL(&lock);
            uint32_t md = st.music_drops, mu = st.music_underruns, crd = st.call_rx_drops, ctd = st.call_tx_drops,
                     cu = st.call_underruns, ie = st.i2s_errors; unsigned q = st.music_queued;
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(TAG, "%s %u Hz PA=%d in_peak=%"PRIu32" out_peak=%"PRIu32" mic_peak=%"PRIu32" frames=%"PRIu64" limited=%"PRIu32" P%u",
                     source == SRC_CALL ? "CALL" : "MUSIC", rate, pa_on, in_peak, out_peak, mic_peak, frames_out, limited, profile);
            ESP_LOGI(TAG, "music buffer=%u B drops=%"PRIu32" underruns=%"PRIu32" | call rx_drops=%"PRIu32" tx_drops=%"PRIu32" underruns=%"PRIu32" | i2s_errors=%"PRIu32,
                     q, md, mu, crd, ctd, cu, ie);
            in_peak = out_peak = mic_peak = limited = 0; frames_out = 0;
            last_report = esp_timer_get_time();
        }
    }
}

esp_err_t audio_engine_init(void)
{
    gpio_config_t pins = {.pin_bit_mask = 1ULL << BOARD_PA_ENABLE, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&pins));
    gpio_set_level(BOARD_PA_ENABLE, 0);
    ESP_ERROR_CHECK(es8388_init());
    music_ring = xRingbufferCreate(MUSIC_BUFFER_BYTES, RINGBUF_TYPE_NOSPLIT);
    call_rx_ring = xRingbufferCreate(CALL_RX_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
    call_tx_ring = xRingbufferCreate(CALL_TX_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (!music_ring || !call_rx_ring || !call_tx_ring) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(audio_io_init(44100));
    ESP_LOGI(TAG, "Engine: music 1x gain + limiter, DAC P%u, call volume %u/15, mic gain %.1fx",
             (unsigned)AUDIO_BOOT_DAC_PROFILE, (unsigned)CALL_BOOT_VOLUME, (double)MIC_DIGITAL_GAIN);
    if (xTaskCreatePinnedToCore(audio_task, "audio", 8192, NULL, 6, NULL, 1) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
