#pragma once
#ifdef DEV_RELAY_SLIP

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

class Connection
{
public:
	virtual ~Connection() = default;
	virtual void send_data(const std::vector<uint8_t> &data) = 0;

	virtual void create_read_channel() = 0;
	virtual void close_connection() = 0;

	bool is_connected() const { return is_connected_; }
	void set_is_connected(const bool is_connected) { is_connected_ = is_connected; }

	std::vector<uint8_t> wait_for_response(uint8_t request_id, std::chrono::seconds timeout);
	std::vector<uint8_t> wait_for_request();

	void join();

private:
	std::atomic<bool> is_connected_{false};

protected:
	std::map<uint8_t, std::vector<uint8_t>> data_map_;
	std::thread reading_thread_;

	std::mutex data_mutex_;
	std::condition_variable data_cv_;
};

#endif