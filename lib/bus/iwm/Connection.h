#pragma once

#include <vector>
#include <atomic>
#include <map>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>

class Connection
{
public:
	virtual ~Connection() = default;
	virtual void send_data(const std::vector<uint8_t>& data) = 0;

	virtual void create_read_channel() = 0;

	bool is_connected() const { return is_connected_; }
	void set_is_connected(const bool is_connected) { is_connected_ = is_connected; }

	std::vector<uint8_t> wait_for_request();

	void join();

private:
	std::atomic<bool> is_connected_{false};

protected:
	std::map<uint8_t, std::vector<uint8_t>> responses_;
	std::thread reading_thread_;

	std::mutex request_mutex_;
	std::mutex responses_mutex_;
	std::condition_variable response_cv_;
};
