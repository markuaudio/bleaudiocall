#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board_config.h"
#include "bt_core.h"
#include "bt_music.h"
#include "bt_call.h"

static const char *TAG = "bt";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool a2dp_ready, hfp_ready, a2dp_up, hfp_up, discoverable, scan_set;
    esp_bd_addr_t peer; bool have_peer;            // phone we talk to / just paired with
    bool a2dp_tried, hfp_tried;                    // our own profile attempts for this peer
    int64_t followup_at;                           // when to open the missing profile ourselves
    esp_bd_addr_t target; bool have_target;        // reconnect target (bonded phone)
    unsigned attempts; int64_t next_attempt_at; bool user_stopped;
    bool acl_ok;                                   // last radio-link attempt reached the phone
} s;

const char *bt_core_bda_str(const uint8_t *b, char out[18])
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
    return out;
}

bool bt_core_connected(void)
{
    portENTER_CRITICAL(&lock); bool c = s.a2dp_up || s.hfp_up; portEXIT_CRITICAL(&lock);
    return c;
}

void bt_core_profile_ready(bt_profile_t profile)
{
    portENTER_CRITICAL(&lock);
    if (profile == PROFILE_A2DP) s.a2dp_ready = true; else s.hfp_ready = true;
    bool both = s.a2dp_ready && s.hfp_ready;
    portEXIT_CRITICAL(&lock);
    if (both) ESP_LOGI(TAG, "READY: music (A2DP) + calls (HFP). Select '%s' in the phone's Bluetooth list", BT_DEVICE_NAME);
}

void bt_core_profile_link(bt_profile_t profile, bool up, const uint8_t *bda)
{
    char str[18];
    portENTER_CRITICAL(&lock);
    if (profile == PROFILE_A2DP) s.a2dp_up = up; else s.hfp_up = up;
    bool any = s.a2dp_up || s.hfp_up;
    if (up) {
        memcpy(s.peer, bda, sizeof(esp_bd_addr_t)); s.have_peer = true;
        memcpy(s.target, bda, sizeof(esp_bd_addr_t)); s.have_target = true;
        s.attempts = 0; s.user_stopped = false;
        s.followup_at = esp_timer_get_time() + LINK_PROFILE_FOLLOWUP_S * 1000000LL; // open the other profile too
    } else if (!any) {
        s.a2dp_tried = s.hfp_tried = false; s.followup_at = 0;
        if (!s.user_stopped) { s.attempts = 0; s.next_attempt_at = esp_timer_get_time() + 5000000LL; }
    }
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "%s %s %s", profile == PROFILE_A2DP ? "Music (A2DP)" : "Calls (HFP)",
             up ? "connected to" : "disconnected from", bt_core_bda_str(bda, str));
}

void bt_core_profile_failed(bt_profile_t profile)
{
    // One of our own connect attempts ended without a link. Only when the
    // phone answered the radio (ACL ok) is it worth opening the other profile
    // right away; a page timeout means it is out of range, so just wait for
    // the next reconnect period. Never touches the reconnect attempt counter.
    portENTER_CRITICAL(&lock);
    bool any = s.a2dp_up || s.hfp_up, reachable = s.acl_ok;
    bool other_untried = profile == PROFILE_A2DP ? !s.hfp_tried : !s.a2dp_tried;
    s.followup_at = (!any && reachable && s.have_peer && other_untried) ? esp_timer_get_time() : 0;
    bool followup = s.followup_at != 0;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "%s attempt failed (%s)%s", profile == PROFILE_A2DP ? "Music (A2DP)" : "Calls (HFP)",
             reachable ? "phone reachable" : "phone not reachable", followup ? "; trying the other profile" : "");
}

static void try_connect(const uint8_t *bda, bool a2dp, bool hfp)
{
    char str[18]; bt_core_bda_str(bda, str);
    if (a2dp) {
        ESP_LOGI(TAG, "Opening music (A2DP) to %s ourselves", str);
        esp_err_t err = bt_music_connect(bda);
        if (err != ESP_OK) ESP_LOGW(TAG, "A2DP connect request: %s", esp_err_to_name(err));
    }
    if (hfp) {
        ESP_LOGI(TAG, "Opening calls (HFP) to %s ourselves", str);
        esp_err_t err = bt_call_connect(bda);
        if (err != ESP_OK) ESP_LOGW(TAG, "HFP connect request: %s", esp_err_to_name(err));
    }
}

static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *p)
{
    char str[18];
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (p->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Pairing successful with '%s' [%s], link key type %d", p->auth_cmpl.device_name,
                     bt_core_bda_str(p->auth_cmpl.bda, str), p->auth_cmpl.lk_type);
            portENTER_CRITICAL(&lock);
            memcpy(s.peer, p->auth_cmpl.bda, sizeof(esp_bd_addr_t)); s.have_peer = true;
            memcpy(s.target, p->auth_cmpl.bda, sizeof(esp_bd_addr_t)); s.have_target = true;
            s.a2dp_tried = s.hfp_tried = false; s.user_stopped = false; s.attempts = 0;
            s.followup_at = esp_timer_get_time() + LINK_PROFILE_FOLLOWUP_S * 1000000LL;
            portEXIT_CRITICAL(&lock);
        } else {
            ESP_LOGW(TAG, "Pairing failed, status %d [%s]. Forget the device on the phone and pair again",
                     p->auth_cmpl.stat, bt_core_bda_str(p->auth_cmpl.bda, str));
        }
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        // Legacy pairing (Bluetooth 2.0 and older phones): fixed PIN.
        esp_bt_pin_code_t code = {0};
        unsigned len = p->pin_req.min_16_digit ? 16 : 4;
        memcpy(code, p->pin_req.min_16_digit ? "0000000000000000" : BT_LEGACY_PIN, len);
        ESP_LOGI(TAG, "Old-style PIN pairing from %s: replying %.*s (type this on the phone)",
                 bt_core_bda_str(p->pin_req.bda, str), (int)len, (const char *)code);
        esp_bt_gap_pin_reply(p->pin_req.bda, true, len, code);
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        // Secure Simple Pairing numeric comparison: a speaker has no display, accept.
        ESP_LOGI(TAG, "SSP confirm %06"PRIu32" from %s: accepted", p->cfm_req.num_val, bt_core_bda_str(p->cfm_req.bda, str));
        esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "SSP passkey %06"PRIu32": enter this number on the phone", p->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGW(TAG, "Phone asks us to type a passkey (no keyboard here); replying 000000");
        esp_bt_gap_ssp_passkey_reply(p->key_req.bda, true, 0);
        break;
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT: {
        bool ok = p->acl_conn_cmpl_stat.stat == ESP_BT_STATUS_SUCCESS;
        portENTER_CRITICAL(&lock); s.acl_ok = ok; portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "Radio link with %s: status 0x%x%s", bt_core_bda_str(p->acl_conn_cmpl_stat.bda, str),
                 p->acl_conn_cmpl_stat.stat, ok ? "" : " (0x104 = page timeout: phone off / out of range)");
        break;
    }
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        ESP_LOGI(TAG, "Radio link closed by %s: reason 0x%x (0x13 phone, 0x16 us, 0x08 timeout/out of range)",
                 bt_core_bda_str(p->acl_disconn_cmpl_stat.bda, str), p->acl_disconn_cmpl_stat.reason);
        break;
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGD(TAG, "Power mode %d", p->mode_chg.mode);
        break;
    case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT:
        ESP_LOGI(TAG, "Bond removed [%s]: %s", bt_core_bda_str(p->remove_bond_dev_cmpl.bda, str),
                 p->remove_bond_dev_cmpl.status == ESP_BT_STATUS_SUCCESS ? "ok" : "failed");
        break;
    default:
        ESP_LOGD(TAG, "GAP event %d", event);
        break;
    }
}

static void set_scan_mode(bool discoverable)
{
    // Always connectable: phones open HFP seconds after A2DP (or the other way
    // round), so the radio must keep answering pages while one profile is up.
    esp_err_t err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                             discoverable ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
    if (err != ESP_OK) { ESP_LOGW(TAG, "Scan mode: %s", esp_err_to_name(err)); return; }
    portENTER_CRITICAL(&lock); s.discoverable = discoverable; s.scan_set = true; portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Radio: connectable, %s", discoverable ? "DISCOVERABLE (visible in the phone's list)" : "hidden (already in use)");
}

static void link_task(void *arg)
{
    bool led = false;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&lock);
        bool connected = s.a2dp_up || s.hfp_up, ready = s.a2dp_ready && s.hfp_ready;
        bool disc = s.discoverable, scan_set = s.scan_set;
        bool have_peer = s.have_peer, a2dp_up = s.a2dp_up, hfp_up = s.hfp_up;
        bool a2dp_tried = s.a2dp_tried, hfp_tried = s.hfp_tried;
        int64_t followup_at = s.followup_at, next_attempt_at = s.next_attempt_at;
        bool have_target = s.have_target, user_stopped = s.user_stopped; unsigned attempts = s.attempts;
        esp_bd_addr_t peer, target; memcpy(peer, s.peer, 6); memcpy(target, s.target, 6);
        portEXIT_CRITICAL(&lock);
        if (!ready) continue;

        // LED: steady when a phone is connected, slow blink while waiting.
        led = connected ? true : !led;
        gpio_set_level(BOARD_LED, led);

        if (!scan_set || disc == connected) set_scan_mode(!connected);

        // Follow-up: one profile (or pairing) is up, the other is not; open it ourselves.
        // The timer is re-armed by bt_core_profile_link(up) or bt_core_profile_failed()
        // once the outcome is known, so two page attempts never overlap.
        if (have_peer && followup_at && now >= followup_at) {
            if (!a2dp_up && !a2dp_tried) {
                try_connect(peer, true, false);
                portENTER_CRITICAL(&lock); s.a2dp_tried = true; s.followup_at = 0; portEXIT_CRITICAL(&lock);
            } else if (!hfp_up && !hfp_tried) {
                try_connect(peer, false, true);
                portENTER_CRITICAL(&lock); s.hfp_tried = true; s.followup_at = 0; portEXIT_CRITICAL(&lock);
            } else {
                portENTER_CRITICAL(&lock); s.followup_at = 0; portEXIT_CRITICAL(&lock);
            }
            continue;
        }

        // Reconnect to the bonded phone (boot, or after it dropped the link).
        if (!connected && have_target && !user_stopped && attempts < LINK_RECONNECT_ATTEMPTS && now >= next_attempt_at) {
            char str[18];
            ESP_LOGI(TAG, "Reconnect attempt %u/%u to bonded phone %s", attempts + 1, (unsigned)LINK_RECONNECT_ATTEMPTS, bt_core_bda_str(target, str));
            try_connect(target, true, false);
            portENTER_CRITICAL(&lock);
            memcpy(s.peer, target, 6); s.have_peer = true; s.a2dp_tried = true; s.hfp_tried = false;
            s.followup_at = 0; // HFP follows only after this attempt succeeds or fails
            s.attempts = attempts + 1; s.next_attempt_at = now + LINK_RECONNECT_PERIOD_S * 1000000LL;
            portEXIT_CRITICAL(&lock);
            if (attempts + 1 == LINK_RECONNECT_ATTEMPTS)
                ESP_LOGI(TAG, "Last reconnect attempt; afterwards waiting for the phone (or 'c' / reboot)");
        }
    }
}

void bt_core_disconnect(void)
{
    portENTER_CRITICAL(&lock); s.user_stopped = true; s.followup_at = 0; portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Disconnect requested: dropping music + calls, bond kept, no auto-reconnect until reboot or 'c'");
    bt_call_disconnect();
    bt_music_disconnect();
}

void bt_core_connect_last(void)
{
    int num = esp_bt_gap_get_bond_device_num();
    if (num <= 0) { ESP_LOGW(TAG, "No bonded phone yet; pair first"); return; }
    esp_bd_addr_t list[8]; int n = num > 8 ? 8 : num;
    if (esp_bt_gap_get_bond_device_list(&n, list) != ESP_OK || n <= 0) { ESP_LOGW(TAG, "Bond list read failed"); return; }
    portENTER_CRITICAL(&lock);
    memcpy(s.target, list[n - 1], 6); s.have_target = true;
    s.attempts = 0; s.next_attempt_at = 0; s.user_stopped = false;
    portEXIT_CRITICAL(&lock);
    char str[18];
    ESP_LOGI(TAG, "Will connect to bonded phone %s now", bt_core_bda_str(list[n - 1], str));
}

void bt_core_list_bonds(void)
{
    int num = esp_bt_gap_get_bond_device_num();
    esp_bd_addr_t list[8]; int n = num > 8 ? 8 : num;
    if (n > 0 && esp_bt_gap_get_bond_device_list(&n, list) != ESP_OK) n = 0;
    ESP_LOGI(TAG, "%d bonded phone(s)", num);
    for (int i = 0; i < n; ++i) { char str[18]; ESP_LOGI(TAG, "  bond %d: %s", i, bt_core_bda_str(list[i], str)); }
}

void bt_core_forget_all(void)
{
    int num = esp_bt_gap_get_bond_device_num();
    esp_bd_addr_t list[8]; int n = num > 8 ? 8 : num;
    if (n > 0 && esp_bt_gap_get_bond_device_list(&n, list) != ESP_OK) n = 0;
    bt_core_disconnect();
    for (int i = 0; i < n; ++i) esp_bt_gap_remove_bond_device(list[i]);
    portENTER_CRITICAL(&lock); s.have_target = false; s.have_peer = false; portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Removed %d bond(s). Also forget the device on the phone, then pair again", n);
}

void bt_core_log_status(void)
{
    char str[18];
    portENTER_CRITICAL(&lock);
    bool a = s.a2dp_up, h = s.hfp_up, d = s.discoverable, hp = s.have_peer; unsigned att = s.attempts;
    esp_bd_addr_t peer; memcpy(peer, s.peer, 6);
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Status: music %s, calls %s, %s, peer %s, reconnect attempts %u, bonds %d, own address %s",
             a ? "UP" : "down", h ? "UP" : "down", d ? "discoverable" : "hidden",
             hp ? bt_core_bda_str(peer, str) : "-", att, esp_bt_gap_get_bond_device_num(),
             bt_core_bda_str(esp_bt_dev_get_address(), str));
}

esp_err_t bt_core_radio_init(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth radio before codec/I2S/touch initialization");
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    esp_bluedroid_config_t host = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    host.ssp_en = true; // Secure Simple Pairing for 2.1+ phones; legacy PIN pairing still answered
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&host));
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    char str[18];
    ESP_LOGI(TAG, "Radio and host enabled, own address %s", bt_core_bda_str(esp_bt_dev_get_address(), str));
    return ESP_OK;
}

esp_err_t bt_core_start(void)
{
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name(BT_DEVICE_NAME));
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_callback));

    // Class of device: Audio/Video major, "Hands-free" minor, Audio + Rendering
    // services. Old phones filter their device list by this; "hands-free" tells
    // them calls are welcome, the SDP records then advertise A2DP + HFP.
    esp_bt_cod_t cod = {0};
    cod.major = ESP_BT_COD_MAJOR_DEV_AV;
    cod.minor = 0x02; // Hands-free device (0x01 wearable headset, 0x05 loudspeaker)
    cod.service = ESP_BT_COD_SRVC_AUDIO | ESP_BT_COD_SRVC_RENDERING;
    ESP_ERROR_CHECK(esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD));

    // Pairing: "Just Works" SSP (no display/keyboard here) + fixed legacy PIN.
    esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability)));
    esp_bt_pin_code_t pin; memcpy(pin, BT_LEGACY_PIN, 4);
    ESP_ERROR_CHECK(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin));

    // Give slow / sleeping phones 10 s to answer our own connection attempts.
    esp_bt_gap_set_page_timeout(0x4000);

    ESP_ERROR_CHECK(bt_music_init());
    ESP_ERROR_CHECK(bt_call_init());

    // Remember the newest bonded phone and try to reach it shortly after boot.
    int num = esp_bt_gap_get_bond_device_num();
    if (num > 0) {
        esp_bd_addr_t list[8]; int n = num > 8 ? 8 : num;
        if (esp_bt_gap_get_bond_device_list(&n, list) == ESP_OK && n > 0) {
            char str[18];
            portENTER_CRITICAL(&lock);
            memcpy(s.target, list[n - 1], 6); s.have_target = true;
            s.next_attempt_at = esp_timer_get_time() + 3000000LL;
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(TAG, "%d bonded phone(s); will try %s in 3 s", num, bt_core_bda_str(list[n - 1], str));
        }
    } else {
        ESP_LOGI(TAG, "No bonded phone yet: pair from the phone (PIN %s if asked)", BT_LEGACY_PIN);
    }
    if (xTaskCreate(link_task, "bt_link", 4096, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
