#include "NetworkCommands.h"

#include <esp_ping.h>
#include <ping/ping.h>
#include <ping/ping_sock.h>
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <unistd.h>
#include <esp_wifi.h>
#include <esp_crc.h>

#include "../device/fuji.h"
#include "fnWiFi.h"

#include "string_utils.h"
#include "../improv/improv.h"

// static const char *wlstatus2string(wl_status_t status)
// {
//     switch (status)
//     {
//     case WL_NO_SHIELD:
//         return "Not initialized";
//     case WL_CONNECT_FAILED:
//         return "Connection failed";
//     case WL_CONNECTED:
//         return "Connected";
//     case WL_CONNECTION_LOST:
//         return "Connection lost";
//     case WL_DISCONNECTED:
//         return "Disconnected";
//     case WL_IDLE_STATUS:
//         return "Idle status";
//     case WL_NO_SSID_AVAIL:
//         return "No SSID available";
//     case WL_SCAN_COMPLETED:
//         return "Scan completed";
//     default:
//         return "Unknown";
//     }
// }

const char* wlmode2string(wifi_mode_t mode)
{
    switch(mode) {
        case WIFI_MODE_NULL:
            return "Not initialized";
        case WIFI_MODE_AP:
            return "Accesspoint";
        case WIFI_MODE_STA:
            return "Station";
        case WIFI_MODE_APSTA:
            return "Station + Accesspoint";
        default:
            return "Unknown";
    }
}


// Ping Functions
static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    printf("%lu bytes from %s icmp_seq=%d ttl=%d time=%lu ms\r\n",
           recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time);
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    printf("From %s icmp_seq=%u timeout\r\n", inet_ntoa(target_addr.u_addr.ip4), seqno);
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    ip_addr_t target_addr;
	uint32_t transmitted;
	uint32_t received;
	uint32_t total_time_ms;
	esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
	esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
	uint32_t loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
	if (IP_IS_V4(&target_addr)) {
		printf("\n--- %s ping statistics ---", inet_ntoa(*ip_2_ip4(&target_addr)));
	} else {
		printf("\n--- %s ping statistics ---", inet6_ntoa(*ip_2_ip6(&target_addr)));
	}
	printf("%" PRIu32 " packets transmitted, %" PRIu32 " received, %" PRIu32 "%% packet loss, time %" PRIu32 "ms",
			 transmitted, received, loss, total_time_ms);
	// delete the ping sessions, so that we clean up all resources and can create a new ping session
	// we don't have to call delete function in the callback, instead we can call delete function from other tasks
	esp_ping_delete_session(hdl);
}

static int ping(int argc, char **argv)
{
    //By default do 5 pings
    int number_of_pings = 5;

    int opt;
    while ((opt = getopt(argc, argv, "n:")) != -1) {
        switch(opt) {
            case 'n':
                number_of_pings = atoi(optarg);
                break;
            case '?':
                printf("Unknown option: %c\r\n", optopt);
                break;
            case ':':
                printf("Missing arg for %c\r\n", optopt);
                break;

            default:
                fprintf(stderr, "Usage: ping -n 5 [HOSTNAME]\r\n");
                fprintf(stderr, "-n: The number of pings. 0 means infinite. Can be aborted with Ctrl+D or Ctrl+C.");
                return 1;
        }
    }

    int argind = optind;

    //Get hostname
    if (argind >= argc) {
        fprintf(stderr, "You need to pass an hostname!\r\n");
        return EXIT_FAILURE;
    }

    char* hostname = argv[argind];

    /* convert hostname to IP address */
    ip_addr_t target_addr;
    struct addrinfo hint;
    struct addrinfo *res = NULL;
    memset(&hint, 0, sizeof(hint));
    memset(&target_addr, 0, sizeof(target_addr));
    auto result = getaddrinfo(hostname, NULL, &hint, &res);

    if (result) {
        fprintf(stderr, "Could not resolve hostname! (getaddrinfo returned %d)\r\n", result);
        return 1;
    }

    struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
    freeaddrinfo(res);

    //Configure ping session
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.task_stack_size = 4096;
    ping_config.target_addr = target_addr;          // target IP address
    ping_config.count = number_of_pings;   // 0 means infinite ping

    /* set callback functions */
    esp_ping_callbacks_t cbs;
    cbs.on_ping_success = on_ping_success;
    cbs.on_ping_timeout = on_ping_timeout;
    cbs.on_ping_end = on_ping_end;
    //Pass a variable as pointer so the sub tasks can decrease it
    //cbs.cb_args = &number_of_pings_remaining;

    esp_ping_handle_t ping;
    esp_ping_new_session(&ping_config, &cbs, &ping);

    esp_ping_start(ping);

    char c = 0;
    
    uint16_t seqno;
    esp_ping_get_profile(ping, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    
    //Make stdin input non blocking so we can query for input AND check ping seqno
    int flags = fcntl(fileno(stdin), F_GETFL, 0);
    fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK);

    //Wait for Ctrl+D or Ctr+C or that our task finishes
    //The async tasks decrease number_of_pings, so wait for it to get to 0
    while((number_of_pings == 0 || seqno <= number_of_pings) && c != 4 && c != 3) {
        esp_ping_get_profile(ping, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
        c = getc(stdin);
        sleep(1);
    }

    //Reset flags, so we dont end up destroying our terminal env later, when linenoise takes over again
    fcntl(fileno(stdin), F_SETFL, flags);

    esp_ping_stop(ping);

    //Print total statistics
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;
    esp_ping_get_profile(ping, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(ping, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(ping, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    printf("%lu packets transmitted, %lu received, time %lu ms\r\n", transmitted, received, total_time_ms);

    esp_ping_delete_session(ping);

    return EXIT_SUCCESS;
}


static void ipconfig_wlan()
{
    printf("==== WLAN ====\r\n");
    // auto status = fnWiFi.status();
    // printf("Mode: %s\r\n", wlmode2string(fnWiFi.getMode()));
    // printf("Status: %s\r\n", wlstatus2string(status));

    // if (status == WL_NO_SHIELD) {
    //     return;
    // }
    
    // printf("\r\n");
    // printf("SSID: %s\r\n", fnWiFi.get_current_ssid().c_str());
    // printf("BSSID: %s\r\n", fnWiFi.get_current_bssid_str().c_str());
    // //printf("Channel: %d\r\n", fnWiFi.channel());

    // printf("\r\n");
    // printf("IP: %s\r\n", fnWiFi.localIP().toString().c_str());
    // printf("Subnet Mask: %s (/%d)\r\n", WiFi.subnetMask().toString().c_str(), WiFi.subnetCIDR());
    // printf("Gateway: %s\r\n", WiFi.gatewayIP().toString().c_str());
    // printf("IPv6: %s\r\n", WiFi.localIPv6().toString().c_str());
    
    // printf("\r\n");
    // printf("Hostname: %s\r\n", WiFi.getHostname());
    // printf("DNS1: %s\r\n", WiFi.dnsIP(0).toString().c_str());
    // printf("DNS2: %s\r\n", WiFi.dnsIP(0).toString().c_str());
}

static int ipconfig(int argc, char **argv)
{
    ipconfig_wlan();
    return EXIT_SUCCESS;
}

static int scan(int argc, char **argv)
{
    fnWiFi.scan_networks();
    printf("Found following networks:\r\n");
    std::vector<std::string> network_names = fnWiFi.get_network_names();
    for (std::string _network_name: network_names)
    {
        uint8_t c_crc8 = esp_crc8_le(0, (uint8_t *)_network_name.c_str(), _network_name.length());
        printf("[%03d] - %s\r\n", c_crc8, _network_name.c_str());
    }
    return EXIT_SUCCESS;
}

static int connect(int argc, char **argv)
{
    if (argc == 3)
    {
        std::string network = argv[1];
        if (mstr::isNumeric(network)) {
            // Find SSID by CRC8 Number
            network = fnWiFi.get_network_name_by_crc8(std::stoi(argv[1]));
        }
        int e = ( fnWiFi.connect(network.c_str(), argv[2]) );

        if ( e == ESP_OK)
            fnWiFi.store_wifi(network, argv[2]);

        return e;
    }

    return fnWiFi.connect();
}



// *** Improv

std::vector<std::string> getLocalUrl() {
  return {
    // URL where user can finish onboarding or use device
    // Recommended to use website hosted by device
    std::string("http://meatloaf.local")
  };
}

void serial_write(std::vector<uint8_t> &data) { 
    // print buffer bytes
    for (int i = 0; i < data.size(); i++) {
        fprintf(stdout, "%c", data[i]);
    }
}


void set_state(improv::State state) {  
  
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
  data.resize(11);
  data[6] = improv::IMPROV_SERIAL_VERSION;
  data[7] = improv::TYPE_CURRENT_STATE;
  data[8] = 1;
  data[9] = state;

  uint8_t checksum = 0x00;
  for (uint8_t d : data)
    checksum += d;
  data[10] = checksum;

  serial_write(data);
}


void send_response(std::vector<uint8_t> &response) {
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
  data.resize(9);
  data[6] = improv::IMPROV_SERIAL_VERSION;
  data[7] = improv::TYPE_RPC_RESPONSE;
  data[8] = response.size();
  data.insert(data.end(), response.begin(), response.end());

  uint8_t checksum = 0x00;
  for (uint8_t d : data)
    checksum += d;
  data.push_back(checksum);

  serial_write(data);
}

void set_error(improv::Error error) {
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
  data.resize(11);
  data[6] = improv::IMPROV_SERIAL_VERSION;
  data[7] = improv::TYPE_ERROR_STATE;
  data[8] = 1;
  data[9] = error;

  uint8_t checksum = 0x00;
  for (uint8_t d : data)
    checksum += d;
  data[10] = checksum;

  serial_write(data);
}

void getAvailableWifiNetworks() {
//   int networkNum = WiFi.scanNetworks();

//   for (int id = 0; id < networkNum; ++id) { 
//     std::vector<uint8_t> data = improv::build_rpc_response(
//             improv::GET_WIFI_NETWORKS, {WiFi.SSID(id), String(WiFi.RSSI(id)), (WiFi.encryptionType(id) == WIFI_AUTH_OPEN ? "NO" : "YES")}, false);
//     send_response(data);
//     sleep(1);
//   }
//   //final response
//   std::vector<uint8_t> data =
//           improv::build_rpc_response(improv::GET_WIFI_NETWORKS, std::vector<std::string>{}, false);
//   send_response(data);
}

bool connectWifi(std::string ssid, std::string password) {
  uint8_t count = 0;

//   WiFi.begin(ssid.c_str(), password.c_str());

//   while (WiFi.status() != WL_CONNECTED) {
//     blink_led(500, 1);

//     if (count > MAX_ATTEMPTS_WIFI_CONNECTION) {
//       WiFi.disconnect();
//       return false;
//     }
//     count++;
//   }

  return true;
}

static int improv_c(int argc, char **argv)
{

    if (argc != 2)
    {
        fprintf(stderr, "Usage: improv {data}\n");
        return 1;
    }

    uint8_t version = argv[1][0];   // 6
    uint8_t type = argv[1][1];      // 7
    uint8_t data_len = argv[1][2];  // 8
    uint8_t data[data_len] = { 0 };
    memcpy(data, &argv[1][2], data_len);

    uint8_t checksum = 0xDD; // IMPROV
    checksum += version;
    checksum += type;
    checksum += data_len;

    for (size_t i = 0; i < (data_len + 1); i++)
        checksum += data[i];

    if (checksum != data[data_len + 1]) {
        fprintf(stderr, "bad checksum [%02x][%02x]\r\n", (data[data_len + 1]), checksum);
        return false;
    }

    if (type != improv::TYPE_RPC) {
        return false;
    }

    auto cmd = improv::parse_improv_data(data, data_len, false);
    // fprintf(stdout, "alldata[%s]", mstr::toHex(argv[1], strlen(argv[1])).c_str());
    // fprintf(stdout, "   data[%s]", mstr::toHex(argv[1][2], data_len).c_str());

    switch (cmd.command) {
        case improv::Command::GET_CURRENT_STATE:
        {
        //if ((WiFi.status() == WL_CONNECTED)) {
        if (0) {
            set_state(improv::State::STATE_PROVISIONED);
            std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_CURRENT_STATE, getLocalUrl(), false);
            send_response(data);

        } else {
            set_state(improv::State::STATE_AUTHORIZED);
        }
        
        break;
        }

        case improv::Command::WIFI_SETTINGS:
        {
        if (cmd.ssid.length() == 0) {
            set_error(improv::Error::ERROR_INVALID_RPC);
            break;
        }
        
        set_state(improv::STATE_PROVISIONING);
        
        if (connectWifi(cmd.ssid, cmd.password)) {

            //blink_led(100, 3);
            
            //TODO: Persist credentials here

            set_state(improv::STATE_PROVISIONED);        
            std::vector<uint8_t> data = improv::build_rpc_response(improv::WIFI_SETTINGS, getLocalUrl(), false);
            send_response(data);
        } else {
            set_state(improv::STATE_STOPPED);
            set_error(improv::Error::ERROR_UNABLE_TO_CONNECT);
        }
        
        break;
        }

        case improv::Command::GET_DEVICE_INFO:
        {
        std::vector<std::string> infos = {
            // Firmware name
            "meatloaf",
            // Firmware version
            "20250106.04",
            // Hardware chip/variant
            "ESP32",
            // Device name
            "Meatloaf!"
        };
        std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_DEVICE_INFO, infos, false);
        send_response(data);
        break;
        }

        case improv::Command::GET_WIFI_NETWORKS:
        {
        getAvailableWifiNetworks();
        break;
        }

        default: {
        set_error(improv::ERROR_UNKNOWN_RPC);
        return false;
        }
    }

    return EXIT_SUCCESS;
}

namespace ESP32Console::Commands
{
    const ConsoleCommand getPingCommand()
    {
        return ConsoleCommand("ping", &ping, "Ping host");
    }

    const ConsoleCommand getIpconfigCommand()
    {
        return ConsoleCommand("ipconfig", &ipconfig, "Show IP and connection informations");
    }

    const ConsoleCommand getScanCommand()
    {
        return ConsoleCommand("scan", &scan, "Scan for wifi networks");
    }

    const ConsoleCommand getConnectCommand()
    {
        return ConsoleCommand("connect", &connect, "Connect to wifi");
    }

    const ConsoleCommand getIMPROVCommand()
    {
        return ConsoleCommand("improv", &improv_c, "Wifi config via IMPROV protocol");
    }
}