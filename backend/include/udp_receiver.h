#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/mman.h>
#endif
#endif

#include "common.h"

class SPSCRingBuffer {
public:
    explicit SPSCRingBuffer(size_t capacity);
    ~SPSCRingBuffer() = default;

    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

    bool tryPush(const SensorData& item);
    bool tryPop(SensorData& item);
    size_t size() const;
    bool empty() const;

private:
    struct alignas(64) PaddedAtomic {
        std::atomic<size_t> value{0};
    };

    std::vector<SensorData> buffer_;
    size_t mask_;
    PaddedAtomic head_;
    PaddedAtomic tail_;
};

class UdpReceiver {
public:
    using Callback = std::function<void(const SensorData&)>;

    static constexpr size_t BATCH_SIZE = 64;
    static constexpr size_t RING_CAPACITY = 8192;
    static constexpr int KERNEL_RCVBUF_MB = 32;

    UdpReceiver(uint16_t port, size_t buffer_size = UDP_BUFFER_SIZE);
    ~UdpReceiver();

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    void setCallback(Callback cb);
    bool start();
    void stop();
    bool isRunning() const;

    size_t getDroppedPackets() const;
    size_t getReceivedPackets() const;

private:
    void receiveLoop();
    void receiveBatchLinux();
    void receiveBatchWindows();
    void processBatch(size_t count);
    bool parsePXIPacket(const uint8_t* data, size_t length, SensorData& out);

    uint16_t port_;
    size_t buffer_size_;
    Callback callback_;

    std::atomic<bool> running_{false};
    std::thread recv_thread_;

    SPSCRingBuffer ring_buffer_;

    std::vector<uint8_t> batch_buffer_;
    std::vector<SensorData> parsed_batch_;

    std::atomic<size_t> dropped_packets_{0};
    std::atomic<size_t> received_packets_{0};

#ifdef _WIN32
    SOCKET socket_{INVALID_SOCKET};
    static bool wsa_initialized_;
    static int wsa_init_count_;
    WSAOVERLAPPED overlapped_;
    WSABUF wsa_buf_;
    char wsa_recv_buf_[65536];
#else
    int socket_{-1};
#ifdef __linux__
    struct mmsghdr msg_vec_[BATCH_SIZE];
    struct iovec msg_iov_[BATCH_SIZE];
    struct sockaddr_in msg_addrs_[BATCH_SIZE];
    std::vector<uint8_t> packet_bufs_[BATCH_SIZE];
    static constexpr size_t PKT_BUF_SIZE = 2048;
#endif
#endif
};
