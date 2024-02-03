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
#include <unordered_map>

#include "../../utils/std_extensions.hpp"

#ifdef WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  #define CLOSE_SOCKET closesocket
  #define SHUTDOWN_SOCKET(s) shutdown(s, SD_SEND)
  #define SOCKET_ERROR_CODE WSAGetLastError()
#else
  #include <arpa/inet.h>
  #include <errno.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define CLOSE_SOCKET close
  #define SHUTDOWN_SOCKET(s) shutdown(s, SHUT_WR)
  #define SOCKET_ERROR_CODE errno
  #define INVALID_SOCKET -1
  #define SOCKET_ERROR -1
#endif

#include "iwm_slip.h"
#include "iwm.h"
#include "TCPConnection.h"
#include "../slip/Request.h"
#include "fnConfig.h"
#include "fnDNS.h"
#include "fnSystem.h"

#define PHASE_IDLE   0b0000
#define PHASE_ENABLE 0b1010
#define PHASE_RESET  0b0101

sp_cmd_state_t sp_command_mode;

void iwm_slip::end_request_thread() {
  std::cout << "Ending request thread" << std::endl;
  // stop listening for requests, and stop the connection.
  is_responding_ = false;
  connection_->set_is_connected(false);
  connection_->join();
  close_connection(connection_sock, true);
  connection_sock = 0;
  if (request_thread_.joinable()) {
    request_thread_.join();
  }
}

iwm_slip::~iwm_slip() {
  end_request_thread();
}

void iwm_slip::setup_gpio()
{
}

void iwm_slip::setup_spi() {
#ifdef WIN32
  int iteration_mod = 10;
#else
  int iteration_mod = 15;
#endif
  // Create a listener for requests.
  std::cout << "iwm_slip::setup_spi - attempting to connect to SLIP server " << Config.get_boip_host() << ":" << Config.get_boip_port() << std::endl;
  bool connected = false;

  in_addr_t host_ip = get_ip4_addr_by_name(Config.get_boip_host().c_str());
  if (host_ip == IPADDR_NONE) {
    std::ostringstream msg;
    msg << "The host value " << Config.get_boip_host() << " could not be converted to an IP address";
    throw std::runtime_error(msg.str());
  }
  // There really isn't anything else for this SLIP version to do than try and get a connection to server, so keep trying. User can kill process themselves.
  int iteration_count = 0;
  while (!connected) {
    if (fnSystem.check_for_shutdown()) {
      return; // get out, shutdown requested
    }
    try {
      connected = connect_to_server(host_ip, Config.get_boip_port());
    } catch (const std::runtime_error& e) {

    }
    if (!connected) {
      iteration_count++;
      if (iteration_count % iteration_mod == 0) {
        std::cout << "." << std::flush;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));  // pause for 0.1s every loop around. keeps it slightly less busy.
    }
  }
  std::cout << std::endl << "iwm_slip::setup_spi - connection to server successful" << std::endl;
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
  if (!connection_->is_connected()) {
    // the responder thread has finished, so reset by going into connect mode again
    restart();
    return PHASE_RESET;
  }

  // Lock the mutex before accessing the queue
  std::lock_guard<std::mutex> lock(queue_mutex_);

  // Check for a new Request Packet on the transport layer
  if (request_queue_.empty()) {
    sp_command_mode = sp_cmd_state_t::standby;
    return PHASE_IDLE;
  }

  // create a Request object from the data
  std::vector<uint8_t> request_data = request_queue_.front();
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
    int chars_to_print = num;
    if (chars_to_print > 32) chars_to_print = 32;
    char *msg = util_hexdump(data, chars_to_print);
    printf("%s", msg);
    free(msg);
    if (chars_to_print < num) {
      printf("... truncated\n");
    }
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
    int chars_to_print = payload_size;
    if (chars_to_print > 32) chars_to_print = 32;
    char *msg = util_hexdump(output_data, chars_to_print);
    printf("%s\n", msg);
    free(msg);
    if (chars_to_print < payload_size) {
      printf("... truncated\n");
    }
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

void iwm_slip::close_connection(int sock, bool report_error) {
  if (sock == 0) return;

  if (SHUTDOWN_SOCKET(sock) == SOCKET_ERROR && report_error)
  {
    std::cerr << "Error shutting down socket, error code: " << SOCKET_ERROR_CODE << std::endl;
  }

  if (CLOSE_SOCKET(sock) == SOCKET_ERROR && report_error)
  {
    std::cerr << "Error closing socket, error code: " << SOCKET_ERROR_CODE << std::endl;
  }

#ifdef _WIN32
  WSACleanup();
#endif
}

bool iwm_slip::connect_to_server(in_addr_t host, int port)
{
#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    std::ostringstream msg;
    msg << "WSAStartup failed with code " << WSAGetLastError() << std::endl;
    throw std::runtime_error(msg.str());
  }
#endif
  int sock;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
    std::ostringstream msg;
    msg << "A socket could not be created.";
    throw std::runtime_error(msg.str());
  }

  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = host;

#ifdef _WIN32
  u_long mode = 1;  // 1 to enable non-blocking socket
  ioctlsocket(sock, FIONBIO, &mode);
#endif

  bool did_connect = false;
  if (connect(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
#ifdef _WIN32
    if (WSAGetLastError() == WSAEWOULDBLOCK) {
      // The connection attempt is in progress.
      fd_set writefds;
      FD_ZERO(&writefds);
      FD_SET(sock, &writefds);
      timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 50000;  // Wait up to 50 milliseconds for the connection to complete.
      if (select(0, NULL, &writefds, NULL, &tv) == 1) {
        did_connect = true;
      }
    }
    mode = 0;  // 0 to disable non-blocking socket
    ioctlsocket(sock, FIONBIO, &mode);
#endif
    if (!did_connect) {
      close_connection(sock, false);
      return false;
    }
  }
  connection_sock = sock;

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
      int chars_to_print = request_data.size();
      if (chars_to_print > 32) chars_to_print = 32;
      char *msg = util_hexdump(request_data.data(), chars_to_print);
      printf("\nNEW Request data:\n%s\n", msg);
      free(msg);
      if (chars_to_print != request_data.size()) {
        printf("... truncated\n");
      }

      std::lock_guard<std::mutex> lock(queue_mutex_);
      request_queue_.push(request_data);
    }
  }
}

void iwm_slip::restart() {
  std::cout << "iwm_slip::restarting" << std::endl;
  end_request_thread();
  setup_spi();
}

iwm_slip smartport;

#endif // SP_OVER_SLIP

#endif // BUILD_APPLE
