#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "board_config.h"
#include "supply.h"

static const char *TAG = "supply";
static adc_oneshot_unit_handle_t adc;

esp_err_t supply_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1, .ulp_mode = ADC_ULP_MODE_DISABLE};
    esp_err_t err = adc_oneshot_new_unit(&unit, &adc);
    if (err != ESP_OK) return err;
    // 12 dB attenuation: the ADC saturates around 3.1 V, so a healthy 3.3 V rail
    // reads near full scale and any reading below that is a sag.
    adc_oneshot_chan_cfg_t chan = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    err = adc_oneshot_config_channel(adc, SUPPLY_SENSE_ADC_CHANNEL, &chan);
    if (err != ESP_OK) return err;
    int raw = supply_read_raw();
    ESP_LOGI(TAG, "Guard: sensing the 3.3 V rail on GPIO36 (REC line), raw=%d; sag = %d counts below the quiet baseline", raw, SUPPLY_SAG_COUNTS);
    return ESP_OK;
}

int supply_read_raw(void)
{
    if (!adc) return -1;
    int sum = 0, n = 0;
    for (int i = 0; i < 4; ++i) {
        int raw;
        if (adc_oneshot_read(adc, SUPPLY_SENSE_ADC_CHANNEL, &raw) == ESP_OK) { sum += raw; ++n; }
    }
    return n ? sum / n : -1;
}
