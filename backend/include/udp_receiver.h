#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include "types.h"

class UdpReceiver {
public:
    using Callback = std::function<void(const SensorData&)>;

    UdpReceiver(uint16_t port, size_t buffer_size = UDP_BUFFER_SIZE);
    ~UdpReceiver();

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    void setCallback(Callback cb);
    bool start();
    void stop();
    bool isRunning() const;

private:
    void receiveLoop();
    bool parsePXIPacket(const uint8_t* data, size_t length, SensorData& out);

    uint16_t port_;
    size_t buffer_size_;
    std::vector<uint8_t> buffer_;
    Callback callback_;

    std::atomic<bool> running_{false};
    std::thread recv_thread_;

#ifdef _WIN32
    SOCKET socket_{INVALID_SOCKET};
    static bool wsa_initialized_;
    static int wsa_init_count_;
#else
    int socket_{-1};
#endif
};
