#ifdef BUILD_APPLE

#ifdef SP_OVER_SLIP

#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstring>
#include <string>
#include <vector>
#include <iomanip>
#include <thread>
#include <stdexcept>

#include <string.h>

#ifdef _WIN32
 #include <winsock2.h>
 #pragma comment(lib, "ws2_32.lib")
 #include <ws2tcpip.h>
#else
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include <unistd.h>
#endif

#include "iwm_slip.h"
#include "iwm.h"
#include "TCPConnection.h"
#include "../slip/Request.h"
#include "fnConfig.h"
#include "fnDNS.h"

#define PHASE_IDLE   0b0000
#define PHASE_ENABLE 0b1010
#define PHASE_RESET  0b0101

sp_cmd_state_t sp_command_mode;

iwm_slip::~iwm_slip() {
  // stop listening for requests, and stop the connection.
  is_responding_ = false;
  connection_->set_is_connected(false);
  connection_->join();
  if (request_thread_.joinable()) {
    request_thread_.join();
  }
}

void iwm_slip::setup_gpio()
{
}

void iwm_slip::setup_spi() {
  // Create a listener for requests.
  std::cout << "iwm_slip::setup_spi - attempting to connect to SLIP server " << Config.get_boip_host() << ":" << Config.get_boip_port() << std::endl;
  bool connected = false;
  // There really isn't anything else for this SLIP version to do than try and get a connection to server, so keep trying. User can kill process themselves.
  while (!connected) {
    try {
      connected = connect_to_server(Config.get_boip_host(), Config.get_boip_port());
    } catch (const std::runtime_error& e) {

    }
    if (!connected) {
      std::cout << "Retrying in 5 seconds..." << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  }
  std::cout << "iwm_slip::setup_spi - connection to server successful" << std::endl;
}

bool iwm_slip::req_wait_for_falling_timeout(int t)
{
  return false;
}

bool iwm_slip::req_wait_for_rising_timeout(int t)
{
  return false;
}

uint8_t iwm_slip::iwm_phase_vector()
{
  // Lock the mutex before accessing the queue
  std::lock_guard<std::mutex> lock(queue_mutex_);

  // Check for a new Request Packet on the transport layer
  if (request_queue_.empty()) {
    sp_command_mode = sp_cmd_state_t::standby;
    return PHASE_IDLE;
  }

  // create a Request object from the data
  auto request_data = request_queue_.front();
  request_queue_.pop();
  current_request = Request::from_packet(request_data);

  std::fill(std::begin(IWM.command_packet.data), std::end(IWM.command_packet.data), 0);
  // The request data is the raw bytes of the request object, we're only really interested in the header part
  std::copy(request_data.begin(), request_data.begin() + 8, IWM.command_packet.data);

  // signal we have a command to process
  sp_command_mode = sp_cmd_state_t::command;
  return PHASE_ENABLE;
}

int iwm_slip::iwm_send_packet_spi()
{
  auto data = current_response->serialize();

  // Some debug to remove
  char *msg = util_hexdump(data.data(), data.size());
  printf("iwm_slip::iwm_send_packet_spi\nresponse data (not including SLIP):\n%s\n", msg);
  free(msg);

  // send the data
  try {
    connection_->send_data(data);
  } catch (const std::runtime_error& e) {
    std::cerr << "iwm_slip::iwm_send_packet_spi ERROR sending response: " << e.what() << std::endl;
  }

  return 0; // 0 is success
}

void iwm_slip::spi_end()
{
}

void iwm_slip::encode_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t* data, uint16_t num)
{
  std::cout << "\niwm_slip::encode_packet\nsource: " << static_cast<unsigned int>(source) << ", packet type: " << ipt2str(packet_type) << ", status: " << static_cast<unsigned int>(status) << ", num: " << static_cast<unsigned int>(num) << std::endl;
  if (num > 0) {
    char *msg = util_hexdump(data, num);
    printf("%s\n", msg);
    free(msg);
  }

  // Create response object from data being given
  current_response = current_request->create_response(source, status, data, num);
}

size_t iwm_slip::decode_data_packet(uint8_t* output_data)
{
  // Used to get the payload data into output_data.
  // this is Request specific, e.g. WriteBlock is 512 bytes, Control is the Control List data, etc
  current_request->copy_payload(output_data);

  auto payload_size = current_request->payload_size();
  std::cout << "\niwm_slip::decode_data_packet\nrequest payload size: " << payload_size << ", data:" << std::endl;
  if (payload_size > 0) {
    char *msg = util_hexdump(output_data, payload_size);
    printf("%s\n", msg);
    free(msg);
  }

  return current_request->payload_size();
}

size_t iwm_slip::decode_data_packet(uint8_t* input_data, uint8_t* output_data)
{
  // Used to create the initial "command" for the request into output_data.
  // We can ignore the input_data, we already have current_request, which can write the appropriate command data to output_data
  current_request->create_command(output_data);
  return 0; // unused
}

void iwm_slip::close_connection(int sock) {
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}

bool iwm_slip::connect_to_server(std::string host, int port)
{
  int sock;
#ifdef _WIN32
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
#else
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
#endif
    std::ostringstream msg;
    msg << "A socket could not be created.";
    throw std::runtime_error(msg.str());
  }

  auto host_ip = get_ip4_addr_by_name(host.c_str());
  if (host_ip == IPADDR_NONE) {
    close_connection(sock);
    std::ostringstream msg;
    msg << "The host value " << host << "could not be converted to an IP address";
    throw std::runtime_error(msg.str());
  }

  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = host_ip;

  if (connect(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
    close_connection(sock);
    return false;
  }

  std::shared_ptr<Connection> conn = std::make_shared<TCPConnection>(sock);
  conn->set_is_connected(true);
  conn->create_read_channel();

  connection_ = conn;

  is_responding_ = true;
  request_thread_ = std::thread(&iwm_slip::wait_for_requests, this);
  return true;
}

void iwm_slip::wait_for_requests() {
  while (is_responding_) {
    auto request_data = connection_->wait_for_request();
    if (!request_data.empty()) {
      char *msg = util_hexdump(request_data.data(), request_data.size());
      printf("\nNEW Request data:\n%s\n", msg);
      free(msg);

      std::lock_guard<std::mutex> lock(queue_mutex_);
      request_queue_.push(request_data);
    }
  }
}

iwm_slip smartport;

#endif // SP_OVER_SLIP

#endif // BUILD_APPLE
