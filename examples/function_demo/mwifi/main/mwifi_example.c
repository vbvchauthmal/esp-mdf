/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "mdf_common.h"
#include "mwifi.h"

static int g_sockfd    = -1;
static const char *TAG = "mwifi_examples";

void tcp_client_write_task(void *arg)
{
    mdf_err_t ret = MDF_OK;
    char *data    = MDF_CALLOC(1, MWIFI_PAYLOAD_LEN);
    size_t size   = MWIFI_PAYLOAD_LEN;
    uint8_t src_addr[MWIFI_ADDR_LEN] = {0x0};
    mwifi_data_type_t data_type      = {0x0};

    MDF_LOGI("TCP client write task is running");

    while (mwifi_is_connected()) {
        if (g_sockfd == -1) {
            vTaskDelay(500 / portTICK_RATE_MS);
            continue;
        }

        size = MWIFI_PAYLOAD_LEN;
        ret = mwifi_root_read(src_addr, &data_type, data, &size, portMAX_DELAY);
        MDF_ERROR_CONTINUE(ret != MDF_OK, "<%s> mwifi_root_read", mdf_err_to_name(ret));

        char *json_data = NULL;
        int json_size   = asprintf(&json_data, "{\"addr\":\"" MACSTR "\",\"data\":%s}",
                                   MAC2STR(src_addr), data);

        MDF_LOGI("TCP write, size: %d, data: %s", json_size, json_data);
        ret = write(g_sockfd, json_data, json_size);
        MDF_FREE(json_data);
        MDF_ERROR_CONTINUE(ret <= 0, "<%s> TCP write", strerror(errno));
    }

    MDF_LOGI("TCP client write task is exit");

    close(g_sockfd);
    MDF_FREE(data);
    vTaskDelete(NULL);
}

/**
 * @brief Create a tcp client
 */
static int socket_tcp_client_create(const char *ip, uint16_t port)
{
    MDF_PARAM_CHECK(ip);

    MDF_LOGI("Create a tcp client, ip: %s, port: %d", ip, port);

    mdf_err_t ret = ESP_OK;
    int sockfd    = -1;
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr(ip),
    };

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    MDF_ERROR_GOTO(sockfd < 0, ERR_EXIT, "socket create, sockfd: %d", sockfd);

    ret = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
    MDF_ERROR_GOTO(ret < 0, ERR_EXIT, "socket connect, ret: %d, ip: %s, port: %d",
                   ret, ip, port);
    return sockfd;

ERR_EXIT:

    if (sockfd != -1) {
        close(sockfd);
    }

    return -1;
}

void tcp_client_read_task(void *arg)
{
    mdf_err_t ret = MDF_OK;
    char *data    = MDF_MALLOC(MWIFI_PAYLOAD_LEN);
    size_t size   = MWIFI_PAYLOAD_LEN;
    uint8_t dest_addr[MWIFI_ADDR_LEN] = {0x0};
    mwifi_data_type_t data_type       = {0x0};

    MDF_LOGI("TCP client read task is running");

    while (mwifi_is_connected()) {
        if (g_sockfd == -1) {
            g_sockfd = socket_tcp_client_create(CONFIG_SERVER_IP, CONFIG_SERVER_PORT);

            if (g_sockfd == -1) {
                vTaskDelay(500 / portTICK_RATE_MS);
                continue;
            }
        }

        memset(data, 0, MWIFI_PAYLOAD_LEN);
        ret = read(g_sockfd, data, size);
        MDF_LOGI("TCP read, %d, size: %d, data: %s", g_sockfd, size, data);

        if (ret <= 0) {
            MDF_LOGW("<%s> TCP read", strerror(errno));
            close(g_sockfd);
            g_sockfd = -1;
            continue;
        }

        cJSON *pJson = NULL;
        cJSON *pSub  = NULL;

        pJson = cJSON_Parse(data);
        MDF_ERROR_CONTINUE(!pJson, "cJSON_Parse, data format error");

        pSub = cJSON_GetObjectItem(pJson, "addr");

        if (!pSub) {
            MDF_LOGW("cJSON_GetObjectItem, Destination address not set");
            cJSON_Delete(pJson);
            continue;
        }

        /**
         * @brief  Convert mac from string format to binary
         */
        do {
            uint32_t mac_data[MWIFI_ADDR_LEN] = {0};
            sscanf(pSub->valuestring, MACSTR,
                   mac_data, mac_data + 1, mac_data + 2,
                   mac_data + 3, mac_data + 4, mac_data + 5);

            for (int i = 0; i < MWIFI_ADDR_LEN; i++) {
                dest_addr[i] = mac_data[i];
            }
        } while (0);

        pSub = cJSON_GetObjectItem(pJson, "data");

        if (!pSub) {
            MDF_LOGW("cJSON_GetObjectItem, Failed to get data");
            cJSON_Delete(pJson);
            continue;
        }

        char *json_data = cJSON_PrintUnformatted(pSub);

        ret = mwifi_root_write(dest_addr, 1, &data_type, json_data, strlen(json_data), true);
        MDF_ERROR_CONTINUE(ret != MDF_OK, "<%s> mwifi_root_write", mdf_err_to_name(ret));

        MDF_FREE(json_data);
        cJSON_Delete(pJson);
    }

    MDF_LOGI("TCP client read task is exit");

    close(g_sockfd);
    g_sockfd = -1;
    MDF_FREE(data);
    vTaskDelete(NULL);
}

static void node_read_task(void *arg)
{
    mdf_err_t ret = MDF_OK;
    cJSON *pJson = NULL;
    cJSON *pSub  = NULL;
    char *data    = MDF_MALLOC(MWIFI_PAYLOAD_LEN);
    size_t size   = MWIFI_PAYLOAD_LEN;
    mwifi_data_type_t data_type      = {0x0};
    uint8_t src_addr[MWIFI_ADDR_LEN] = {0x0};

    MDF_LOGI("Note read task is running");

    for (;;) {
        if (!mwifi_is_connected()) {
            vTaskDelay(500 / portTICK_RATE_MS);
            continue;
        }

        size = MWIFI_PAYLOAD_LEN;
        memset(data, 0, MWIFI_PAYLOAD_LEN);
        ret = mwifi_read(src_addr, &data_type, data, &size, portMAX_DELAY);
        MDF_ERROR_CONTINUE(ret != MDF_OK, "<%s> mwifi_read", mdf_err_to_name(ret));
        MDF_LOGD("Node receive: " MACSTR ", size: %d, data: %s", MAC2STR(src_addr), size, data);

        pJson = cJSON_Parse(data);
        MDF_ERROR_CONTINUE(!pJson, "cJSON_Parse, data format error, data: %s", data);

        pSub = cJSON_GetObjectItem(pJson, "status");

        if (!pSub) {
            MDF_LOGW("cJSON_GetObjectItem, Destination address not set");
            cJSON_Delete(pJson);
            continue;
        }

        gpio_set_level(CONFIG_LED_GPIO_NUM, pSub->valueint);

        cJSON_Delete(pJson);
    }

    MDF_LOGW("Note read task is exit");

    MDF_FREE(data);
    vTaskDelete(NULL);
}

static void node_write_task(void *arg)
{
    mdf_err_t ret = MDF_OK;
    int count     = 0;
    size_t size   = 0;
    char *data    = NULL;
    mwifi_data_type_t data_type      = {0x0};

    MDF_LOGI("NODE task is running");

    for (;;) {
        if (!mwifi_is_connected()) {
            vTaskDelay(500 / portTICK_RATE_MS);
            continue;
        }

        size = asprintf(&data, "{\"seq\":%d,\"layer\":%d,\"status\":%d}",
                        count++, esp_mesh_get_layer(), gpio_get_level(CONFIG_LED_GPIO_NUM));

        MDF_LOGD("Node send, size: %d, data: %s", size, data);
        ret = mwifi_write(NULL, &data_type, data, size, true);
        MDF_FREE(data);
        MDF_ERROR_CONTINUE(ret != MDF_OK, "<%s> mwifi_write", mdf_err_to_name(ret));

        vTaskDelay(3000 / portTICK_RATE_MS);
    }

    MDF_LOGW("NODE task is exit");

    vTaskDelete(NULL);
}

/**
 * @brief Timed printing system information
 */
static void show_sysinfo_timercb(void *timer)
{
    uint8_t primary                 = 0;
    wifi_second_chan_t second       = 0;
    mesh_addr_t parent_bssid        = {0};
    uint8_t sta_mac[MWIFI_ADDR_LEN] = {0};
    mesh_assoc_t *mesh_assoc        = MDF_MALLOC(sizeof(mesh_assoc_t));
    wifi_sta_list_t *wifi_sta_list  = MDF_MALLOC(sizeof(wifi_sta_list_t));

    MDF_ERROR_ASSERT(esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac));
    MDF_ERROR_ASSERT(esp_wifi_ap_get_sta_list(wifi_sta_list));
    MDF_ERROR_ASSERT(esp_wifi_vnd_mesh_get(mesh_assoc));
    MDF_ERROR_ASSERT(esp_wifi_get_channel(&primary, &second));
    esp_mesh_get_parent_bssid(&parent_bssid);


    MDF_LOGI("System information, channel: %d, layer: %d, self mac: " MACSTR ", parent bssid: " MACSTR
             ", parent rssi: %d, node num: %d, free heap: %u", primary,
             esp_mesh_get_layer(), MAC2STR(sta_mac), MAC2STR(parent_bssid.addr),
             mesh_assoc->rssi, esp_mesh_get_total_node_num(), esp_get_free_heap_size());

    for (int i = 0; i < wifi_sta_list->num; i++) {
        MDF_LOGI("Child mac: " MACSTR, MAC2STR(wifi_sta_list->sta[i].mac));
    }

    MDF_FREE(mesh_assoc);
    MDF_FREE(wifi_sta_list);
}

static mdf_err_t wifi_init()
{
    mdf_err_t ret          = nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        MDF_ERROR_ASSERT(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    MDF_ERROR_ASSERT(ret);

    tcpip_adapter_init();
    MDF_ERROR_ASSERT(esp_event_loop_init(NULL, NULL));
    MDF_ERROR_ASSERT(esp_wifi_init(&cfg));
    MDF_ERROR_ASSERT(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    MDF_ERROR_ASSERT(esp_wifi_set_mode(WIFI_MODE_STA));
    MDF_ERROR_ASSERT(esp_wifi_set_ps(WIFI_PS_NONE));
    MDF_ERROR_ASSERT(esp_mesh_set_6m_rate(false));
    MDF_ERROR_ASSERT(esp_wifi_start());

    return MDF_OK;
}

/**
 * @brief All module events will be sent to this task in esp-mdf
 *
 * @Note:
 *     1. Do not block or lengthy operations in the callback function.
 *     2. Do not consume a lot of memory in the callback function.
 *        The task memory of the callback function is only 4KB.
 */
static mdf_err_t event_loop_cb(mdf_event_loop_t event, void *ctx)
{
    MDF_LOGI("event_loop_cb, event: %d", event);

    switch (event) {
        case MDF_EVENT_MWIFI_STARTED:
            MDF_LOGI("MESH is started");
            break;

        case MDF_EVENT_MWIFI_PARENT_CONNECTED:
            MDF_LOGI("Parent is connected on station interface");
            break;

        case MDF_EVENT_MWIFI_PARENT_DISCONNECTED:
            MDF_LOGI("Parent is disconnected on station interface");
            break;

        case MDF_EVENT_MWIFI_ROUTING_TABLE_ADD:
        case MDF_EVENT_MWIFI_ROUTING_TABLE_REMOVE:
            MDF_LOGI("total_num: %d", esp_mesh_get_total_node_num());
            break;

        case MDF_EVENT_MWIFI_CHANNEL_NO_FOUND:
        case MDF_EVENT_MWIFI_NO_PARENT_FOUND:
            MDF_LOGI("No parent found");
            mwifi_config_t config      = {0x0};
            wifi_ap_record_t ap_record = {0};

            MDF_LOGW("Scan the channel and bssid of the router or mesh device");

            MDF_ERROR_ASSERT(mwifi_get_config(&config));

            for (int i = 0; i < 3; ++i) {
                mdf_err_t ret = mwifi_scan(&config, &ap_record);
                MDF_ERROR_CONTINUE(ret != MDF_OK, "Scanning the channel and bssid of the router or mesh device failed");

                MDF_LOGI("AP, ssid: %s, bssid: " MACSTR ", channel: %u, rssi: %d",
                         ap_record.ssid, MAC2STR(ap_record.bssid), ap_record.primary, ap_record.rssi);

                config.channel = ap_record.primary;
                memcpy(config.router_bssid, ap_record.bssid, sizeof(config.router_bssid));
                MDF_ERROR_ASSERT(mwifi_set_config(&config));
                MDF_ERROR_ASSERT(mwifi_restart());
                break;
            }

            break;

        case MDF_EVENT_MWIFI_ROOT_GOT_IP: {
            MDF_LOGI("Root obtains the IP address. It is posted by LwIP stack automatically");
            xTaskCreate(tcp_client_write_task, "tcp_client_write_task", 4 * 1024,
                        NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
            xTaskCreate(tcp_client_read_task, "tcp_server_read", 4 * 1024,
                        NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
            break;
        }

        default:
            break;
    }

    return MDF_OK;
}

void app_main()
{
    esp_chip_info_t chip_info = {0};
    mwifi_init_config_t cfg   = MWIFI_INIT_CONFIG_DEFAULT();
    mwifi_config_t config     = {
        .router_ssid     = CONFIG_ROUTER_SSID,
        .router_password = CONFIG_ROUTER_PASSWORD,
        .channel         = CONFIG_ROUTER_CHANNEL,
        .mesh_id         = CONFIG_MESH_ID,
        .mesh_password   = CONFIG_MESH_PASSWORD,
    };

    /**
     * @brief Print system information.
     */
    esp_chip_info(&chip_info);
    MDF_LOGI("******************* SYSTEM INFO *******************");
    MDF_LOGI("idf version      : %s", esp_get_idf_version());
    MDF_LOGI("mdf version      : %s", mdf_get_version());
    MDF_LOGI("compile time     : %s %s", __DATE__, __TIME__);
    MDF_LOGI("free heap        : %d Byte", esp_get_free_heap_size());
    MDF_LOGI("CPU cores        : %d", chip_info.cores);
    MDF_LOGI("function         : WiFi%s%s",
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    MDF_LOGI("silicon revision : %d", chip_info.revision);
    MDF_LOGI("flash            : %d MB %s", spi_flash_get_chip_size() / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    MDF_LOGI("***************************************************");

    /**
     * @brief Set the log level for serial port printing.
     */
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    gpio_pad_select_gpio(CONFIG_LED_GPIO_NUM);
    gpio_set_direction(CONFIG_LED_GPIO_NUM, GPIO_MODE_INPUT_OUTPUT);

    /**
     * @brief Initialize wifi mesh.
     */
    MDF_ERROR_ASSERT(mdf_event_loop_init(event_loop_cb));
    MDF_ERROR_ASSERT(wifi_init());
    MDF_ERROR_ASSERT(mwifi_init(&cfg));
    MDF_ERROR_ASSERT(mwifi_set_config(&config));
    MDF_ERROR_ASSERT(mwifi_start());

    /**
     * @breif Create handler
     */
    TimerHandle_t timer = xTimerCreate("show_sysinfo_timercb", 10000 / portTICK_RATE_MS,
                                       true, NULL, show_sysinfo_timercb);
    xTimerStart(timer, 0);

    xTaskCreate(node_write_task, "node_write_task", 4 * 1024,
                NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
    xTaskCreate(node_read_task, "node_read_task", 4 * 1024,
                NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
}
