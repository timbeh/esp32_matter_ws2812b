#include "matter_driver.h"

#include <inttypes.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_matter.h>

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "matter_driver";
static constexpr uint16_t LIGHT_ENDPOINT_ID = 1;

static void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kWiFiConnectivityChange:
        if (event->WiFiConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established) {
            wifi_ap_record_t ap_info = {};
            esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Wi-Fi connected: RSSI=%d dBm, channel=%u", ap_info.rssi, ap_info.primary);
            } else {
                ESP_LOGW(TAG, "Wi-Fi connected; AP diagnostics unavailable: %s", esp_err_to_name(err));
            }
        } else if (event->WiFiConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Lost) {
            ESP_LOGW(TAG, "Wi-Fi connection lost");
        }
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == attribute::PRE_UPDATE) {
        return matter_driver_attribute_update(nullptr, endpoint_id, cluster_id, attribute_id, val);
    }
    return ESP_OK;
}

static esp_err_t update_attribute(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                  esp_matter_attr_val_t value)
{
    cluster_t *cluster = cluster::get(endpoint_id, cluster_id);
    if (!cluster) {
        ESP_LOGE(TAG, "Matter cluster 0x%" PRIx32 " is missing on endpoint %u", cluster_id, endpoint_id);
        return ESP_ERR_NOT_FOUND;
    }
    if (!attribute::get(cluster, attribute_id)) {
        ESP_LOGE(TAG, "Matter attribute 0x%" PRIx32 " is missing from cluster 0x%" PRIx32,
                 attribute_id, cluster_id);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = attribute::update(endpoint_id, cluster_id, attribute_id, &value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Updating Matter attribute path 0x%04" PRIx16 "/0x%08" PRIx32 "/0x%08" PRIx32 " failed: %s",
                 endpoint_id, cluster_id, attribute_id, esp_err_to_name(err));
    }
    return err;
}

esp_err_t matter_driver_attribute_update(void *driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    if (!val) {
        return ESP_ERR_INVALID_ARG;
    }
    if (endpoint_id != LIGHT_ENDPOINT_ID) {
        return ESP_OK;
    }

    if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            ESP_LOGI(TAG, "New State: %d", val->val.b);
            state_update_onoff(val->val.b);
        }
    } else if (cluster_id == LevelControl::Id) {
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            ESP_LOGI(TAG, "New Level: %d", val->val.u8);
            state_update_level(val->val.u8);
        }
    } else if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            ESP_LOGI(TAG, "New Hue: %d", val->val.u8);
            state_update_hue(val->val.u8);
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            ESP_LOGI(TAG, "New Sat: %d", val->val.u8);
            state_update_saturation(val->val.u8);
        } else if (attribute_id == ColorControl::Attributes::EnhancedCurrentHue::Id) {
            ESP_LOGI(TAG, "New Enhanced Hue: %d", val->val.u16);
            state_update_enhanced_hue(val->val.u16);
        } else if (attribute_id == ColorControl::Attributes::CurrentX::Id) {
            ESP_LOGI(TAG, "New X: %d", val->val.u16);
            state_update_xy(val->val.u16, state_get().y);
        } else if (attribute_id == ColorControl::Attributes::CurrentY::Id) {
            ESP_LOGI(TAG, "New Y: %d", val->val.u16);
            state_update_xy(state_get().x, val->val.u16);
        } else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            ESP_LOGI(TAG, "New CT Mireds: %d", val->val.u16);
            state_update_ct(val->val.u16);
        }
    }
    return ESP_OK;
}

esp_err_t matter_driver_sync_attributes(uint16_t endpoint_id, const LedState& current_state)
{
    if (current_state.color_mode > led_state_limits::kMaxColorMode) {
        ESP_LOGE(TAG, "Stored color mode %u is unsupported", current_state.color_mode);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = update_attribute(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id,
                                     esp_matter_bool(current_state.on));
    if (err != ESP_OK) return err;

    err = update_attribute(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id,
                           esp_matter_nullable_uint8(nullable<uint8_t>(current_state.brightness)));
    if (err != ESP_OK) return err;

    err = update_attribute(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id,
                           esp_matter_enum8(current_state.color_mode));
    if (err != ESP_OK) return err;

    if (current_state.color_mode == 0) {
        err = update_attribute(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id,
                               esp_matter_uint8(current_state.hue));
        if (err != ESP_OK) return err;
        return update_attribute(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id,
                                esp_matter_uint8(current_state.saturation));
    }
    if (current_state.color_mode == 1) {
        err = update_attribute(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id,
                               esp_matter_uint16(current_state.x));
        if (err != ESP_OK) return err;
        return update_attribute(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id,
                                esp_matter_uint16(current_state.y));
    }
    return update_attribute(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id,
                            esp_matter_uint16(current_state.mireds));
}

esp_err_t matter_driver_init()
{
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Creating Matter node failed");
        return ESP_ERR_NO_MEM;
    }

    endpoint::extended_color_light::config_t light_config;
    light_config.level_control_lighting.min_level = led_state_limits::kMinBrightness;
    light_config.level_control_lighting.max_level = led_state_limits::kMaxBrightness;
    light_config.color_control.color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    light_config.color_control.enhanced_color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    light_config.color_control_color_temperature.color_temp_physical_min_mireds =
        led_state_limits::kMinColorTemperatureMireds;
    light_config.color_control_color_temperature.color_temp_physical_max_mireds =
        led_state_limits::kMaxColorTemperatureMireds;

    endpoint_t *endpoint = endpoint::extended_color_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        ESP_LOGE(TAG, "Creating Matter extended color light endpoint failed");
        return ESP_ERR_NO_MEM;
    }

    cluster_t *color_control_cluster = cluster::get(endpoint, ColorControl::Id);
    if (!color_control_cluster) {
        ESP_LOGE(TAG, "Matter color control cluster is missing");
        return ESP_ERR_NOT_FOUND;
    }

    cluster::color_control::feature::hue_saturation::config_t hue_sat_config;
    esp_err_t err = cluster::color_control::feature::hue_saturation::add(color_control_cluster, &hue_sat_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Adding Matter hue/saturation feature failed: %s", esp_err_to_name(err));
        return err;
    }

    cluster::color_control::feature::enhanced_hue::config_t enhanced_hue_config;
    err = cluster::color_control::feature::enhanced_hue::add(color_control_cluster, &enhanced_hue_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Adding Matter enhanced hue feature failed: %s", esp_err_to_name(err));
        return err;
    }

    err = matter_driver_sync_attributes(LIGHT_ENDPOINT_ID, state_get());
    if (err != ESP_OK) {
        return err;
    }

    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Starting ESP-Matter failed: %s", esp_err_to_name(err));
    }
    return err;
}
