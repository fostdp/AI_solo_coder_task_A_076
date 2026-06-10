#include "udp_receiver.h"

#include <cstring>
#include <iostream>

#ifdef _WIN32
bool UdpReceiver::wsa_initialized_ = false;
int UdpReceiver::wsa_init_count_ = 0;
#endif

UdpReceiver::UdpReceiver(uint16_t port, size_t buffer_size)
    : port_(port), buffer_size_(buffer_size), buffer_(buffer_size) {}

UdpReceiver::~UdpReceiver() {
    stop();
}

void UdpReceiver::setCallback(Callback cb) {
    callback_ = std::move(cb);
}

bool UdpReceiver::start() {
#ifdef _WIN32
    if (!wsa_initialized_) {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            std::cerr << "WSAStartup failed: " << result << std::endl;
            return false;
        }
        wsa_initialized_ = true;
    }
    wsa_init_count_++;

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "socket() failed: " << WSAGetLastError() << std::endl;
        wsa_init_count_--;
        if (wsa_init_count_ == 0) {
            WSACleanup();
            wsa_initialized_ = false;
        }
        return false;
    }
#else
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ < 0) {
        std::cerr << "socket() failed: " << strerror(errno) << std::endl;
        return false;
    }
#endif

    int reuse = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
#ifdef _WIN32
        std::cerr << "bind() failed: " << WSAGetLastError() << std::endl;
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        wsa_init_count_--;
        if (wsa_init_count_ == 0) {
            WSACleanup();
            wsa_initialized_ = false;
        }
#else
        std::cerr << "bind() failed: " << strerror(errno) << std::endl;
        close(socket_);
        socket_ = -1;
#endif
        return false;
    }

    running_ = true;
    recv_thread_ = std::thread(&UdpReceiver::receiveLoop, this);
    return true;
}

void UdpReceiver::stop() {
    if (!running_.exchange(false)) {
        return;
    }

#ifdef _WIN32
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
#else
    if (socket_ >= 0) {
        close(socket_);
        socket_ = -1;
    }
#endif

    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }

#ifdef _WIN32
    wsa_init_count_--;
    if (wsa_init_count_ == 0 && wsa_initialized_) {
        WSACleanup();
        wsa_initialized_ = false;
    }
#endif
}

bool UdpReceiver::isRunning() const {
    return running_;
}

void UdpReceiver::receiveLoop() {
    while (running_) {
        sockaddr_in sender_addr{};
#ifdef _WIN32
        int sender_len = sizeof(sender_addr);
#else
        socklen_t sender_len = sizeof(sender_addr);
#endif

        int recv_len = recvfrom(socket_,
                                reinterpret_cast<char*>(buffer_.data()),
                                static_cast<int>(buffer_.size()),
                                0,
                                reinterpret_cast<sockaddr*>(&sender_addr),
                                &sender_len);

        if (recv_len <= 0) {
            if (!running_) break;
            continue;
        }

        SensorData sensor_data;
        if (parsePXIPacket(buffer_.data(), static_cast<size_t>(recv_len), sensor_data)) {
            if (callback_) {
                callback_(sensor_data);
            }
        }
    }
}

bool UdpReceiver::parsePXIPacket(const uint8_t* data, size_t length, SensorData& out) {
    if (length < sizeof(PXIPacketHeader)) {
        return false;
    }

    PXIPacketHeader header;
    std::memcpy(&header, data, sizeof(PXIPacketHeader));

    if (header.magic != PXI_MAGIC) {
        return false;
    }

    size_t expected_size = sizeof(PXIPacketHeader) + header.sample_count * sizeof(float);
    if (length < expected_size) {
        return false;
    }

    if (header.sample_count > MAX_PAYLOAD_SAMPLES) {
        return false;
    }

    out.turbine_id = header.turbine_id;
    out.sensor_id = header.sensor_id;
    out.sensor_type = static_cast<SensorType>(header.sensor_type);
    out.location = static_cast<SensorLocation>(0);
    out.blade_id = 0;
    out.zone_id = 0;
    out.timestamp_ms = header.timestamp_ms;

    out.samples.resize(header.sample_count);
    std::memcpy(out.samples.data(),
                data + sizeof(PXIPacketHeader),
                header.sample_count * sizeof(float));

    return true;
}
