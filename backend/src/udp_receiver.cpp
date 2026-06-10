#include "udp_receiver.h"

#include <cstring>
#include <iostream>

#ifdef _WIN32
bool UdpReceiver::wsa_initialized_ = false;
int UdpReceiver::wsa_init_count_ = 0;
#endif

SPSCRingBuffer::SPSCRingBuffer(size_t capacity) {
    size_t pow2 = 1;
    while (pow2 < capacity) pow2 <<= 1;
    buffer_.resize(pow2);
    mask_ = pow2 - 1;
}

bool SPSCRingBuffer::tryPush(const SensorData& item) {
    size_t head = head_.value.load(std::memory_order_relaxed);
    size_t tail = tail_.value.load(std::memory_order_acquire);
    if (head - tail >= buffer_.size()) return false;
    buffer_[head & mask_] = item;
    head_.value.store(head + 1, std::memory_order_release);
    return true;
}

bool SPSCRingBuffer::tryPop(SensorData& item) {
    size_t tail = tail_.value.load(std::memory_order_relaxed);
    size_t head = head_.value.load(std::memory_order_acquire);
    if (tail >= head) return false;
    item = buffer_[tail & mask_];
    tail_.value.store(tail + 1, std::memory_order_release);
    return true;
}

size_t SPSCRingBuffer::size() const {
    size_t head = head_.value.load(std::memory_order_relaxed);
    size_t tail = tail_.value.load(std::memory_order_relaxed);
    return head - tail;
}

bool SPSCRingBuffer::empty() const {
    return size() == 0;
}

UdpReceiver::UdpReceiver(uint16_t port, size_t buffer_size)
    : port_(port), buffer_size_(buffer_size), ring_buffer_(RING_CAPACITY) {
    batch_buffer_.resize(BATCH_SIZE * 2048);
    parsed_batch_.resize(BATCH_SIZE);

#ifdef __linux__
    for (size_t i = 0; i < BATCH_SIZE; i++) {
        packet_bufs_[i].resize(PKT_BUF_SIZE);
    }
#endif
}

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
        if (wsa_init_count_ == 0) { WSACleanup(); wsa_initialized_ = false; }
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

    int rcvbuf = KERNEL_RCVBUF_MB * 1024 * 1024;
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf)) < 0) {
        std::cerr << "Warning: failed to set SO_RCVBUF to "
                  << KERNEL_RCVBUF_MB << "MB" << std::endl;
    }

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
        if (wsa_init_count_ == 0) { WSACleanup(); wsa_initialized_ = false; }
#else
        std::cerr << "bind() failed: " << strerror(errno) << std::endl;
        close(socket_);
        socket_ = -1;
#endif
        return false;
    }

#ifdef __linux__
    for (size_t i = 0; i < BATCH_SIZE; i++) {
        memset(&msg_iov_[i], 0, sizeof(iovec));
        msg_iov_[i].iov_base = packet_bufs_[i].data();
        msg_iov_[i].iov_len = PKT_BUF_SIZE;

        memset(&msg_addrs_[i], 0, sizeof(sockaddr_in));

        memset(&msg_vec_[i], 0, sizeof(mmsghdr));
        msg_vec_[i].msg_hdr.msg_iov = &msg_iov_[i];
        msg_vec_[i].msg_hdr.msg_iovlen = 1;
        msg_vec_[i].msg_hdr.msg_name = &msg_addrs_[i];
        msg_vec_[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
    }
#endif

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

size_t UdpReceiver::getDroppedPackets() const {
    return dropped_packets_.load(std::memory_order_relaxed);
}

size_t UdpReceiver::getReceivedPackets() const {
    return received_packets_.load(std::memory_order_relaxed);
}

void UdpReceiver::receiveLoop() {
#ifdef __linux__
    receiveBatchLinux();
#elif defined(_WIN32)
    receiveBatchWindows();
#else
    while (running_) {
        sockaddr_in sender_addr{};
        socklen_t sender_len = sizeof(sender_addr);

        ssize_t recv_len = recvfrom(socket_,
                                     batch_buffer_.data(),
                                     static_cast<int>(batch_buffer_.size()),
                                     0,
                                     reinterpret_cast<sockaddr*>(&sender_addr),
                                     &sender_len);

        if (recv_len <= 0) {
            if (!running_) break;
            continue;
        }

        SensorData sensor_data;
        if (parsePXIPacket(batch_buffer_.data(), static_cast<size_t>(recv_len), sensor_data)) {
            received_packets_.fetch_add(1, std::memory_order_relaxed);
            if (!ring_buffer_.tryPush(sensor_data)) {
                dropped_packets_.fetch_add(1, std::memory_order_relaxed);
            } else if (callback_) {
                SensorData popped;
                if (ring_buffer_.tryPop(popped)) {
                    callback_(popped);
                }
            }
        }
    }
#endif
}

#ifdef __linux__
void UdpReceiver::receiveBatchLinux() {
    while (running_) {
        int nrecv = recvmmsg(socket_, msg_vec_, BATCH_SIZE,
                             MSG_DONTWAIT, nullptr);

        if (nrecv < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct timespec ts{0, 100000};
                nanosleep(&ts, nullptr);
                continue;
            }
            if (!running_) break;
            continue;
        }

        if (nrecv == 0) continue;

        size_t valid_count = 0;
        for (int i = 0; i < nrecv; i++) {
            size_t pkt_len = static_cast<size_t>(msg_vec_[i].msg_len);
            if (pkt_len == 0) continue;

            if (valid_count < parsed_batch_.size() &&
                parsePXIPacket(packet_bufs_[i].data(), pkt_len, parsed_batch_[valid_count])) {
                valid_count++;
            }
        }

        received_packets_.fetch_add(valid_count, std::memory_order_relaxed);
        processBatch(valid_count);
    }
}
#endif

#ifdef _WIN32
void UdpReceiver::receiveBatchWindows() {
    memset(&overlapped_, 0, sizeof(WSAOVERLAPPED));
    wsa_buf_.buf = wsa_recv_buf_;
    wsa_buf_.len = sizeof(wsa_recv_buf_);

    DWORD flags = 0;
    BOOL pending = WSARecv(socket_, &wsa_buf_, 1, nullptr, &flags,
                           &overlapped_, nullptr);

    if (pending == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            std::cerr << "WSARecv failed: " << err << std::endl;
            return;
        }
    }

    while (running_) {
        DWORD bytes_transferred = 0;
        DWORD recv_flags = 0;
        BOOL ok = WSAGetOverlappedResult(socket_, &overlapped_,
                                          &bytes_transferred, TRUE, &recv_flags);

        if (!ok || bytes_transferred == 0) {
            if (!running_) break;
            memset(&overlapped_, 0, sizeof(WSAOVERLAPPED));
            wsa_buf_.buf = wsa_recv_buf_;
            wsa_buf_.len = sizeof(wsa_recv_buf_);
            flags = 0;
            WSARecv(socket_, &wsa_buf_, 1, nullptr, &flags, &overlapped_, nullptr);
            continue;
        }

        size_t offset = 0;
        size_t valid_count = 0;
        while (offset + sizeof(PXIPacketHeader) <= bytes_transferred && valid_count < parsed_batch_.size()) {
            const uint8_t* pkt = reinterpret_cast<const uint8_t*>(wsa_recv_buf_ + offset);
            PXIPacketHeader hdr;
            memcpy(&hdr, pkt, sizeof(PXIPacketHeader));

            if (hdr.magic != PXI_MAGIC) {
                offset++;
                continue;
            }

            size_t expected = sizeof(PXIPacketHeader) + hdr.sample_count * sizeof(float);
            if (offset + expected > bytes_transferred) break;

            if (parsePXIPacket(pkt, expected, parsed_batch_[valid_count])) {
                valid_count++;
            }
            offset += expected;
        }

        received_packets_.fetch_add(valid_count, std::memory_order_relaxed);
        processBatch(valid_count);

        memset(&overlapped_, 0, sizeof(WSAOVERLAPPED));
        wsa_buf_.buf = wsa_recv_buf_;
        wsa_buf_.len = sizeof(wsa_recv_buf_);
        flags = 0;
        WSARecv(socket_, &wsa_buf_, 1, nullptr, &flags, &overlapped_, nullptr);
    }
}
#endif

void UdpReceiver::processBatch(size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!ring_buffer_.tryPush(parsed_batch_[i])) {
            dropped_packets_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (callback_) {
        SensorData item;
        size_t processed = 0;
        while (ring_buffer_.tryPop(item) && processed < BATCH_SIZE) {
            callback_(item);
            processed++;
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
