/* Largely based on BluetoothSerial.cpp/h from Arduino-ESP
	2018 Evandro Luis Copercini
*/

#ifdef BLUETOOTH_SUPPORT

#include "fnBluetoothSPP.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <esp_log.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../include/debug.h"

#include "fnSystem.h"


const char *_spp_server_name = "ESP32SPP";

#define RX_QUEUE_SIZE 512
#define TX_QUEUE_SIZE 32

#define INQ_LEN 0x10
#define INQ_NUM_RSPS 20
#define READY_TIMEOUT (10 * 1000)
#define SCAN_TIMEOUT (INQ_LEN * 2 * 1000)

#define SPP_RUNNING 0x01
#define SPP_CONNECTED 0x02
#define SPP_CONGESTED 0x04
#define SPP_DISCONNECTED 0x08

static uint32_t _spp_client = 0;
static xQueueHandle _spp_rx_queue = nullptr;
static xQueueHandle _spp_tx_queue = nullptr;
static SemaphoreHandle_t _spp_tx_done = nullptr;
static TaskHandle_t _spp_task_handle = nullptr;
static EventGroupHandle_t _spp_event_group = nullptr;
static bool secondConnectionAttempt;
static esp_spp_cb_t *custom_spp_callback = nullptr;
static fnBluetoothDataCb custom_data_callback = nullptr;

const uint16_t SPP_TX_MAX = 330;
static uint8_t _spp_tx_buffer[SPP_TX_MAX];
static uint16_t _spp_tx_buffer_len = 0;

static esp_bd_addr_t _peer_bd_addr;
static char _remote_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
static bool _isRemoteAddressSet;
static bool _isServer;
static esp_bt_pin_code_t _pin_code;
static int _pin_len;
static bool _isPinSet;
static bool _enableSSP;


typedef struct
{
    size_t len;
    uint8_t data[];
} spp_packet_t;


bool _btStarted()
{
    return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
}

bool _btStart()
{
    // Return if we're already started
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
        return true;

    esp_err_t e;

    // Initialize if it hasn't already been
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE)
    {
        // Initial controller configuration
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if ((e = esp_bt_controller_init(&cfg)) != ESP_OK)
        {
            Debug_printf( "BT init failed (%d): %s\n", e, esp_err_to_name(e));
            return false;
        }
        // Wait until the controller switches to INITED state
        while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE)
        {
            Debug_println( "BT waiting for controller");
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }

    // Enable the controller once it's been initialized
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED)
    {
        if((e = esp_bt_controller_enable(esp_bt_mode_t::ESP_BT_MODE_CLASSIC_BT)) != ESP_OK)
        {
            Debug_printf( "BT enable failed (%d): %s\n", e, esp_err_to_name(e));
            return false;
        }
    }

    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
        return true;

    Debug_println( "BT start failed");
    return false;
}

bool _btStop()
{
    // Already done if controller is IDLE
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE)
        return true;

    esp_err_t e;
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
    {
        if ((e = esp_bt_controller_disable()) != ESP_OK)
        {
            Debug_printf( "BT disable failed (%d): %s\n", e, esp_err_to_name(e));
            return false;
        }
        // Wait until the controller switches to INITED state
        while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
        {
            Debug_println( "BT waiting for controller disable");
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }

    // De-initialize the controller
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED)
    {
        if ((e = esp_bt_controller_deinit()) != ESP_OK)
        {
            Debug_printf( "BT deint failed (%d): %s\n", e, esp_err_to_name(e));
            return false;
        }

        vTaskDelay(1);
        return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE;
    }

    Debug_println( "BT stop failed");
    return false;
}



#ifdef DEBUG
static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == nullptr || str == nullptr || size < 18)
    {
        return nullptr;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}
#endif

static bool _get_name_from_eir(uint8_t *eir, char *bdname, uint8_t *bdname_len)
{
    if (!eir || !bdname || !bdname_len)
        return false;

    uint8_t *rmt_bdname, rmt_bdname_len;
    *bdname = *bdname_len = rmt_bdname_len = 0;

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname)
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);

    if (rmt_bdname)
    {
        rmt_bdname_len = rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN ? ESP_BT_GAP_MAX_BDNAME_LEN : rmt_bdname_len;
        memcpy(bdname, rmt_bdname, rmt_bdname_len);
        bdname[rmt_bdname_len] = 0;
        *bdname_len = rmt_bdname_len;
        return true;
    }

    return false;
}

static bool _btSetPin()
{
    esp_bt_pin_type_t pin_type;
   
    if (_isPinSet)
    {
        if (_pin_len)
        {
            Debug_println( "PIN set");
            pin_type = ESP_BT_PIN_TYPE_FIXED;
        }
        else
        {
            _isPinSet = false;
            Debug_println( "PIN reset");
            pin_type = ESP_BT_PIN_TYPE_VARIABLE; // pin_code would be ignored (default)
        }
        return (esp_bt_gap_set_pin(pin_type, _pin_len, _pin_code) == ESP_OK);
    }
    return false;
}

static esp_err_t _spp_queue_packet(uint8_t *data, size_t len)
{
    if (data == nullptr || len == 0)
        return ESP_OK;

    // OMF This memory seems to get freed in _spp_tx_task(), but it's not clear
    // how this happens since the xQueueSend function works by making a copy
    // of the data, not using a reference.
    spp_packet_t *packet = (spp_packet_t *)malloc(sizeof(spp_packet_t) + len);
    if (!packet)
    {
        Debug_println( "SPP TX packet malloc failed");
        return ESP_FAIL;
    }

    packet->len = len;
    memcpy(packet->data, data, len);
    if (xQueueSend(_spp_tx_queue, &packet, portMAX_DELAY) != pdPASS)
    {
        Debug_println( "SPP TX queue send failed");
        free(packet);
        return ESP_FAIL;
    }

    return ESP_OK;
}


static bool _spp_send_buffer()
{
    if ((xEventGroupWaitBits(_spp_event_group, SPP_CONGESTED, pdFALSE, pdTRUE, portMAX_DELAY) & SPP_CONGESTED) != 0)
    {
        esp_err_t e = esp_spp_write(_spp_client, _spp_tx_buffer_len, _spp_tx_buffer);
        if (e != ESP_OK)
        {
            Debug_printf( "SPP write failed (%d): %s\n", e, esp_err_to_name(e));
            return false;
        }

        _spp_tx_buffer_len = 0;
        if (xSemaphoreTake(_spp_tx_done, portMAX_DELAY) != pdTRUE)
        {
            Debug_println( "SPP ack failed");
            return false;
        }

        return true;
    }

    return false;
}

static void _spp_tx_task(void *arg)
{
    spp_packet_t *packet = nullptr;
    size_t len = 0, to_send = 0;
    uint8_t *data = nullptr;

    while(true)
    {
        if (_spp_tx_queue != nullptr && 
            xQueueReceive(_spp_tx_queue, &packet, portMAX_DELAY) == pdTRUE &&
             packet != nullptr)
        {
            if (packet->len <= (SPP_TX_MAX - _spp_tx_buffer_len))
            {
                memcpy(_spp_tx_buffer + _spp_tx_buffer_len, packet->data, packet->len);
                _spp_tx_buffer_len += packet->len;
                free(packet);
                packet = nullptr;
                if (SPP_TX_MAX == _spp_tx_buffer_len || uxQueueMessagesWaiting(_spp_tx_queue) == 0)
                    _spp_send_buffer();
            }
            else
            {
                len = packet->len;
                data = packet->data;
                to_send = SPP_TX_MAX - _spp_tx_buffer_len;
                memcpy(_spp_tx_buffer + _spp_tx_buffer_len, data, to_send);
                _spp_tx_buffer_len = SPP_TX_MAX;
                data += to_send;
                len -= to_send;
                _spp_send_buffer();

                while (len >= SPP_TX_MAX)
                {
                    memcpy(_spp_tx_buffer, data, SPP_TX_MAX);
                    _spp_tx_buffer_len = SPP_TX_MAX;
                    data += SPP_TX_MAX;
                    len -= SPP_TX_MAX;
                    _spp_send_buffer();
                }

                if (len)
                {
                    memcpy(_spp_tx_buffer, data, len);
                    _spp_tx_buffer_len += len;
                    if (uxQueueMessagesWaiting(_spp_tx_queue) == 0)
                        _spp_send_buffer();
                }

                free(packet);
                packet = nullptr;
            }
        }
        else
        {
            Debug_println( "_spp_tx_task quitting");
        }
    }

    _spp_task_handle = nullptr;
    vTaskDelete(nullptr);
}

static void _esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event)
    {
    case ESP_SPP_INIT_EVT:
        Debug_println( "ESP_SPP_INIT_EVT");
        // OMF changed from: esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
        esp_bt_gap_set_scan_mode(esp_bt_connection_mode_t::ESP_BT_CONNECTABLE, esp_bt_discovery_mode_t::ESP_BT_GENERAL_DISCOVERABLE);
        if (!_isServer)
        {
            Debug_println( "ESP_SPP_INIT_EVT: client: start");
            esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, _spp_server_name);
        }
        xEventGroupSetBits(_spp_event_group, SPP_RUNNING);
        break;

    case ESP_SPP_SRV_OPEN_EVT: //Server connection open
        Debug_println( "ESP_SPP_SRV_OPEN_EVT");
        if (!_spp_client)
        {
            _spp_client = param->open.handle;
        }
        else
        {
            secondConnectionAttempt = true;
            esp_spp_disconnect(param->open.handle);
        }
        xEventGroupClearBits(_spp_event_group, SPP_DISCONNECTED);
        xEventGroupSetBits(_spp_event_group, SPP_CONNECTED);
        break;

    case ESP_SPP_CLOSE_EVT: //Client connection closed
        Debug_println( "ESP_SPP_CLOSE_EVT");
        if (secondConnectionAttempt)
        {
            secondConnectionAttempt = false;
        }
        else
        {
            _spp_client = 0;
            xEventGroupSetBits(_spp_event_group, SPP_DISCONNECTED);
        }
        xEventGroupClearBits(_spp_event_group, SPP_CONNECTED);
        break;

    case ESP_SPP_CONG_EVT: //connection congestion status changed
        if (param->cong.cong)
            xEventGroupClearBits(_spp_event_group, SPP_CONGESTED);
        else
            xEventGroupSetBits(_spp_event_group, SPP_CONGESTED);

        Debug_printf( "ESP_SPP_CONG_EVT: %s\n", param->cong.cong ? "CONGESTED" : "FREE");
        break;

    case ESP_SPP_WRITE_EVT: //write operation completed
        if (param->write.cong)
            xEventGroupClearBits(_spp_event_group, SPP_CONGESTED);

        xSemaphoreGive(_spp_tx_done); //we can try to send another packet
        Debug_printf( "ESP_SPP_WRITE_EVT: %u %s\n", param->write.len, param->write.cong ? "CONGESTED" : "FREE");
        break;

    case ESP_SPP_DATA_IND_EVT: //connection received data
        Debug_printf( "ESP_SPP_DATA_IND_EVT len=%d handle=%d\n", param->data_ind.len, param->data_ind.handle);
        //esp_log_buffer_hex("",param->data_ind.data,param->data_ind.len); //for low level debug
        //ets_printf("r:%u\n", param->data_ind.len);

        if (custom_data_callback)
        {
            custom_data_callback(param->data_ind.data, param->data_ind.len);
        }
        else if (_spp_rx_queue != nullptr)
        {
            for (int i = 0; i < param->data_ind.len; i++)
            {
                if (xQueueSend(_spp_rx_queue, param->data_ind.data + i, (TickType_t)0) != pdTRUE)
                {
                    Debug_printf( "RX Full! Discarding %u bytes\n", param->data_ind.len - i);
                    break;
                }
            }
        }
        break;

    case ESP_SPP_DISCOVERY_COMP_EVT: //discovery complete
        Debug_println( "ESP_SPP_DISCOVERY_COMP_EVT");
        if (param->disc_comp.status == ESP_SPP_SUCCESS)
        {
            Debug_printf( "ESP_SPP_DISCOVERY_COMP_EVT: spp connect to remote");
            esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER, param->disc_comp.scn[0], _peer_bd_addr);
        }
        break;

    case ESP_SPP_OPEN_EVT: //Client connection open
        Debug_println( "ESP_SPP_OPEN_EVT");
        if (!_spp_client)
        {
            _spp_client = param->open.handle;
        }
        else
        {
            secondConnectionAttempt = true;
            esp_spp_disconnect(param->open.handle);
        }
        xEventGroupClearBits(_spp_event_group, SPP_DISCONNECTED);
        xEventGroupSetBits(_spp_event_group, SPP_CONNECTED);
        break;

    case ESP_SPP_START_EVT: //server started
        Debug_println( "ESP_SPP_START_EVT");
        break;

    case ESP_SPP_CL_INIT_EVT: //client initiated a connection
        Debug_println( "ESP_SPP_CL_INIT_EVT");
        break;

    default:
        break;
    }

    if (custom_spp_callback != nullptr)
        (*custom_spp_callback)(event, param);
}

static void _esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BT_GAP_DISC_RES_EVT:
        Debug_println( "ESP_BT_GAP_DISC_RES_EVT");
#ifdef DEBUG
        char bda_str[18];
        Debug_printf( "Scanned device: %s\n", bda2str(param->disc_res.bda, bda_str, 18));
#endif
        for (int i = 0; i < param->disc_res.num_prop; i++)
        {
            uint8_t peer_bdname_len;
            char peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
            switch (param->disc_res.prop[i].type)
            {
            case ESP_BT_GAP_DEV_PROP_EIR:
                if (_get_name_from_eir((uint8_t *)param->disc_res.prop[i].val, peer_bdname, &peer_bdname_len))
                {
                    Debug_printf( "ESP_BT_GAP_DISC_RES_EVT : EIR : %s : %d", peer_bdname, peer_bdname_len);
                    if (strlen(_remote_name) == peer_bdname_len && strncmp(peer_bdname, _remote_name, peer_bdname_len) == 0)
                    {
                        Debug_printf( "ESP_BT_GAP_DISC_RES_EVT : SPP_START_DISCOVERY_EIR : %s", peer_bdname); //, peer_bdname_len);
                        _isRemoteAddressSet = true;
                        memcpy(_peer_bd_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);
                        esp_bt_gap_cancel_discovery();
                        esp_spp_start_discovery(_peer_bd_addr);
                    }
                }
                break;

            case ESP_BT_GAP_DEV_PROP_BDNAME:
                peer_bdname_len = param->disc_res.prop[i].len;
                memcpy(peer_bdname, param->disc_res.prop[i].val, peer_bdname_len);
                peer_bdname_len--; // len includes 0 terminator
                Debug_printf( "ESP_BT_GAP_DISC_RES_EVT : BDNAME :  %s : %d", peer_bdname, peer_bdname_len);
                if (strlen(_remote_name) == peer_bdname_len && strncmp(peer_bdname, _remote_name, peer_bdname_len) == 0)
                {
                    Debug_printf( "ESP_BT_GAP_DISC_RES_EVT : SPP_START_DISCOVERY_BDNAME : %s", peer_bdname);
                    _isRemoteAddressSet = true;
                    memcpy(_peer_bd_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);
                    esp_bt_gap_cancel_discovery();
                    esp_spp_start_discovery(_peer_bd_addr);
                }
                break;

            case ESP_BT_GAP_DEV_PROP_COD:
                Debug_printf( "ESP_BT_GAP_DEV_PROP_COD");
                break;

            case ESP_BT_GAP_DEV_PROP_RSSI:
                Debug_printf( "ESP_BT_GAP_DEV_PROP_RSSI");
                break;

            default:
                break;
            }
            if (_isRemoteAddressSet)
                break;
        }
        break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        Debug_printf( "ESP_BT_GAP_DISC_STATE_CHANGED_EVT");
        break;

    case ESP_BT_GAP_RMT_SRVCS_EVT:
        Debug_printf( "ESP_BT_GAP_RMT_SRVCS_EVT");
        break;

    case ESP_BT_GAP_RMT_SRVC_REC_EVT:
        Debug_printf( "ESP_BT_GAP_RMT_SRVC_REC_EVT");
        break;

    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
        {
            Debug_printf( "authentication success: %s", param->auth_cmpl.device_name);
        }
        else
        {
            Debug_printf( "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;

    case ESP_BT_GAP_PIN_REQ_EVT:
        // default pairing pins
        Debug_printf( "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit)
        {
            Debug_printf( "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code;
            memset(pin_code, '0', ESP_BT_PIN_CODE_LEN);
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        }
        else
        {
            Debug_printf( "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            memcpy(pin_code, "1234", 4);
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;

    case ESP_BT_GAP_CFM_REQ_EVT:
        Debug_printf( "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        Debug_printf( "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
        break;

    case ESP_BT_GAP_KEY_REQ_EVT:
        Debug_printf( "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;

    default:
        break;
    }
}

static bool _init_bt(const char *deviceName)
{
    if (!_spp_event_group)
    {
        _spp_event_group = xEventGroupCreate();
        if (!_spp_event_group)
        {
            Debug_printf( "SPP Event Group Create Failed");
            return false;
        }
        xEventGroupClearBits(_spp_event_group, 0xFFFFFF);
        xEventGroupSetBits(_spp_event_group, SPP_CONGESTED);
        xEventGroupSetBits(_spp_event_group, SPP_DISCONNECTED);
    }

    if (_spp_rx_queue == nullptr)
    {
        _spp_rx_queue = xQueueCreate(RX_QUEUE_SIZE, sizeof(uint8_t)); //initialize the queue
        if (_spp_rx_queue == nullptr)
        {
            Debug_printf( "RX Queue Create Failed");
            return false;
        }
    }

    if (_spp_tx_queue == nullptr)
    {
        _spp_tx_queue = xQueueCreate(TX_QUEUE_SIZE, sizeof(spp_packet_t *)); //initialize the queue
        if (_spp_tx_queue == nullptr)
        {
            Debug_printf( "TX Queue Create Failed");
            return false;
        }
    }

    if (_spp_tx_done == nullptr)
    {
        _spp_tx_done = xSemaphoreCreateBinary();
        if (_spp_tx_done == nullptr)
        {
            Debug_printf( "TX Semaphore Create Failed");
            return false;
        }
        xSemaphoreTake(_spp_tx_done, 0);
    }

    if (_spp_task_handle == nullptr)
    {
        xTaskCreatePinnedToCore(_spp_tx_task, "spp_tx", 4096, nullptr, 2, &_spp_task_handle, 0);
        if (_spp_task_handle == nullptr)
        {
            Debug_printf( "Network Event Task Start Failed");
            return false;
        }
    }

    // Make sure the controller is started/initialized
    if (!_btStarted() && !_btStart())
    {
        Debug_printf( "Initialize controller failed");
        return false;
    }

    esp_err_t e;

    // Check on Bluedroid state
    esp_bluedroid_status_t bt_state = esp_bluedroid_get_status();
    if (bt_state == ESP_BLUEDROID_STATUS_UNINITIALIZED)
    {
        if ((e = esp_bluedroid_init()) != ESP_OK)
        {
            Debug_printf( "Initialize Bluedroid failed (%d): %s", e, esp_err_to_name(e));
            return false;
        }
    }

    if (bt_state != ESP_BLUEDROID_STATUS_ENABLED)
    {
        if ((e = esp_bluedroid_enable()) != ESP_OK)
        {
            Debug_printf( "Enable Bluedroid failed (%d): %s", e, esp_err_to_name(e));
            return false;
        }
    }

    // Register GAP callback if we're a server
    if (_isServer && (e = esp_bt_gap_register_callback(_esp_bt_gap_cb)) != ESP_OK)
    {
        Debug_printf( "GAP register failed (%d): %s", e, esp_err_to_name(e));
        return false;
    }

    // Register SPP callback
    if ((e = esp_spp_register_callback(_esp_spp_cb)) != ESP_OK)
    {
        Debug_printf( "SPP register failed (%d): %s", e, esp_err_to_name(e));
        return false;
    }

    if ((e = esp_spp_init(ESP_SPP_MODE_CB)) != ESP_OK)
    {
        Debug_printf( "SPP init failed (%d): %s", e, esp_err_to_name(e));
        return false;
    }

    if ((e = esp_bt_sleep_disable()) != ESP_OK)
    {
        Debug_printf( "esp_bt_sleep_disable failed (%d): %s", e, esp_err_to_name(e));
    }

    esp_bt_dev_set_device_name(deviceName);

    if (_isPinSet)
        _btSetPin();

    if (_enableSSP)
    {
        Debug_println( "Enabling Simple Secure Pairing");
        esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
        esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
        esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
    }

    // The default BTA_DM_COD_LOUDSPEAKER does not work with the MacOS BT stack
    esp_bt_cod_t cod;
    cod.major = 0b00001;
    cod.minor = 0b000100;
    cod.service = 0b00000010110;
    if ((e = esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD)) != ESP_OK)
    {
        Debug_printf( "Set COD failed (%d): %s", e, esp_err_to_name(e));
        return false;
    }

    return true;
}

static bool _stop_bt()
{
    if (_btStarted())
    {
        if (_spp_client)
            esp_spp_disconnect(_spp_client);

        esp_spp_deinit();
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        _btStop();
    }
    _spp_client = 0;
    if (_spp_task_handle)
    {
        vTaskDelete(_spp_task_handle);
        _spp_task_handle = nullptr;
    }
    if (_spp_event_group)
    {
        vEventGroupDelete(_spp_event_group);
        _spp_event_group = nullptr;
    }
    if (_spp_rx_queue)
    {
        vQueueDelete(_spp_rx_queue);
        //ToDo: clear RX queue when in packet mode
        _spp_rx_queue = nullptr;
    }
    if (_spp_tx_queue)
    {
        spp_packet_t *packet = nullptr;
        while (xQueueReceive(_spp_tx_queue, &packet, 0) == pdTRUE)
        {
            free(packet);
        }
        vQueueDelete(_spp_tx_queue);
        _spp_tx_queue = nullptr;
    }
    if (_spp_tx_done)
    {
        vSemaphoreDelete(_spp_tx_done);
        _spp_tx_done = nullptr;
    }
    return true;
}

static bool waitForConnect(int timeout)
{
    TickType_t xTicksToWait = timeout / portTICK_PERIOD_MS;
    return (xEventGroupWaitBits(_spp_event_group, SPP_CONNECTED, pdFALSE, pdTRUE, xTicksToWait) & SPP_CONNECTED) != 0;
}


/*
*
* Class methods
*
*/

fnBluetoothSPP::fnBluetoothSPP()
{
    local_name = "ESP32"; //default bluetooth name

    // Release BLE resources to the heap
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
}

fnBluetoothSPP::~fnBluetoothSPP()
{
    _stop_bt();
}

void fnBluetoothSPP::onData(fnBluetoothDataCb cb)
{
    custom_data_callback = cb;
}

bool fnBluetoothSPP::begin(string localName, bool isServer)
{
    _isServer = isServer;

    if (localName.length())
        local_name = localName;

    return _init_bt(local_name.c_str());
}

bool fnBluetoothSPP::hasClient(void)
{
    return _spp_client > 0;
}

int fnBluetoothSPP::available(void)
{
    if (_spp_rx_queue == nullptr)
    {
        return 0;
    }
    return uxQueueMessagesWaiting(_spp_rx_queue);
}

int fnBluetoothSPP::peek(void)
{
    uint8_t c;
    if (_spp_rx_queue && xQueuePeek(_spp_rx_queue, &c, 0))
        return c;
        
    return -1;
}

int fnBluetoothSPP::read(void)
{

    uint8_t c = 0;
    if (_spp_rx_queue && xQueueReceive(_spp_rx_queue, &c, 0))
        return c;

    return -1;
}


size_t fnBluetoothSPP::write(const uint8_t *buffer, size_t size)
{
    if (_spp_client == 0)
        return 0;

    return (_spp_queue_packet((uint8_t *)buffer, size) == ESP_OK) ? size : 0;
}

size_t fnBluetoothSPP::write(uint8_t c)
{
    return write(&c, 1);
}

void fnBluetoothSPP::flush()
{
    if (_spp_tx_queue != nullptr)
    {
        while (uxQueueMessagesWaiting(_spp_tx_queue) > 0)
            fnSystem.delay(5);
    }
}

void fnBluetoothSPP::end()
{
    _stop_bt();
}

esp_err_t fnBluetoothSPP::register_callback(esp_spp_cb_t *callback)
{
    custom_spp_callback = callback;
    return ESP_OK;
}

//Simple Secure Pairing
void fnBluetoothSPP::enableSSP()
{
    _enableSSP = true;
}
/*
     * Set default parameters for Legacy Pairing
     * Use fixed pin code
*/
bool fnBluetoothSPP::setPin(const char *pin)
{
    bool isEmpty = !(pin && *pin);
    if (isEmpty && !_isPinSet)
    {
        return true; // nothing to do
    }
    else if (!isEmpty)
    {
        _pin_len = strlen(pin);
        memcpy(_pin_code, pin, _pin_len);
    }
    else
    {
        _pin_len = 0; // resetting pin to none (default)
    }
    _pin_code[_pin_len] = 0;
    _isPinSet = true;
    if (isReady(false, READY_TIMEOUT))
    {
        _btSetPin();
    }
    return true;
}

bool fnBluetoothSPP::connect(string remoteName)
{
    if (!isReady(true, READY_TIMEOUT))
        return false;
    if (remoteName.empty() || remoteName.length() < 1)
    {
        Debug_printf( "No remote name is provided");
        return false;
    }
    disconnect();
    _isRemoteAddressSet = false;
    strlcpy(_remote_name, remoteName.c_str(), ESP_BT_GAP_MAX_BDNAME_LEN);
    _remote_name[ESP_BT_GAP_MAX_BDNAME_LEN] = 0;
    Debug_println( "server : remoteName");
    // will first resolve name to address
    // OMF changed from: esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
    esp_bt_gap_set_scan_mode(esp_bt_connection_mode_t::ESP_BT_CONNECTABLE, esp_bt_discovery_mode_t::ESP_BT_GENERAL_DISCOVERABLE);
    if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, INQ_LEN, INQ_NUM_RSPS) == ESP_OK)
        return waitForConnect(SCAN_TIMEOUT);

    return false;
}

bool fnBluetoothSPP::connect(uint8_t remoteAddress[])
{
    if (!isReady(true, READY_TIMEOUT))
        return false;

    if (!remoteAddress)
    {
        Debug_println( "No remote address is provided");
        return false;
    }

    disconnect();
    _remote_name[0] = 0;
    _isRemoteAddressSet = true;
    memcpy(_peer_bd_addr, remoteAddress, ESP_BD_ADDR_LEN);
    Debug_println( "server : remoteAddress");

    if (esp_spp_start_discovery(_peer_bd_addr) == ESP_OK)
        return waitForConnect(READY_TIMEOUT);

    return false;
}

bool fnBluetoothSPP::connect()
{
    if (!isReady(true, READY_TIMEOUT))
        return false;

    if (_isRemoteAddressSet)
    {
        disconnect();
        // use resolved or set address first
        Debug_println( "server : remoteAddress");
        if (esp_spp_start_discovery(_peer_bd_addr) == ESP_OK)
            return waitForConnect(READY_TIMEOUT);

        return false;
    }
    else if (_remote_name[0])
    {
        disconnect();
        Debug_println( "server : remoteName");
        // will resolve name to address first - it may take a while
        // OMF changed from: esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
        esp_bt_gap_set_scan_mode(esp_bt_connection_mode_t::ESP_BT_CONNECTABLE, esp_bt_discovery_mode_t::ESP_BT_GENERAL_DISCOVERABLE);
        if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, INQ_LEN, INQ_NUM_RSPS) == ESP_OK)
        {
            return waitForConnect(SCAN_TIMEOUT);
        }
        return false;
    }

    Debug_println( "Neither remote name nor address were provided");
    return false;
}

bool fnBluetoothSPP::disconnect()
{
    if (_spp_client)
    {
        flush();
        Debug_printf( "disconnecting");
        if (esp_spp_disconnect(_spp_client) == ESP_OK)
        {
            TickType_t xTicksToWait = READY_TIMEOUT / portTICK_PERIOD_MS;
            return (xEventGroupWaitBits(_spp_event_group, SPP_DISCONNECTED, pdFALSE, pdTRUE, xTicksToWait) & SPP_DISCONNECTED) != 0;
        }
    }
    return false;
}

bool fnBluetoothSPP::unpairDevice(uint8_t remoteAddress[])
{
    if (isReady(false, READY_TIMEOUT))
    {
        Debug_printf( "Removing bonded device");
        return (esp_bt_gap_remove_bond_device(remoteAddress) == ESP_OK);
    }
    return false;
}

bool fnBluetoothSPP::connected(int timeout)
{
    return waitForConnect(timeout);
}

bool fnBluetoothSPP::isReady(bool checkServer, int timeout)
{
    if (checkServer && !_isServer)
    {
        Debug_println( "Server mode is not active. Call begin(localName, true) to enable server mode");
        return false;
    }
    if (!_btStarted())
    {
        Debug_println( "BT is not initialized. Call begin() first");
        return false;
    }
    TickType_t xTicksToWait = timeout / portTICK_PERIOD_MS;
    return (xEventGroupWaitBits(_spp_event_group, SPP_RUNNING, pdFALSE, pdTRUE, xTicksToWait) & SPP_RUNNING) != 0;
}

#endif
