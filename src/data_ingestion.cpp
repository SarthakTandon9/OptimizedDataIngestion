// src/data_ingestion.cpp

#include "ingestion/data_ingestion.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <iostream>
#include <chrono>
#include <pthread.h>
#include <sched.h>

// Helper function to set a socket to non-blocking mode
bool set_nonblocking(int fd)
{
    int flags;
    if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
    {
        flags = 0;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

DataIngestion::DataIngestion(const std::string& ip, int port, const std::vector<int>& ingestion_thread_cores)
    : ip_(ip), port_(port), ingestion_thread_cores_(ingestion_thread_cores),
      running_(false), epoll_fd_(-1), sock_fd_(-1), memory_pool_(10000)
{
}

DataIngestion::~DataIngestion()
{
    stop();
}

void DataIngestion::start()
{
    running_.store(true, std::memory_order_release);
    for (const auto& core : ingestion_thread_cores_)
    {
        ingest_threads_.emplace_back(&DataIngestion::ingest, this, core);
    }
}

void DataIngestion::stop()
{
    if (running_.load(std::memory_order_acquire))
    {
        running_.store(false, std::memory_order_release);
        for (auto& thread : ingest_threads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        ingest_threads_.clear();
    }

    if (sock_fd_ != -1)
    {
        close(sock_fd_);
        sock_fd_ = -1;
    }

    if (epoll_fd_ != -1)
    {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool DataIngestion::get_data(std::shared_ptr<DataRecord>& record)
{
    return data_queue_.dequeue(record);
}

void DataIngestion::ingest(int cpu_core)
{
    // Set thread affinity to specified CPU core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(8, &cpuset); // Pin to specified CPU core
    pthread_t thread = pthread_self();
    int rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0)
    {
        std::cerr << "Error setting thread affinity for CPU " << cpu_core << ": " << rc << "\n";
    }
    else
    {
        std::cout << "Ingestion thread pinned to CPU " << cpu_core << "\n";
    }

    // Create socket
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0)
    {
        std::cerr << "Socket creation failed\n";
        return;
    }

    // Set socket to non-blocking
    if (!set_nonblocking(sock_fd_))
    {
        std::cerr << "Failed to set socket to non-blocking\n";
        close(sock_fd_);
        sock_fd_ = -1;
        return;
    }

    // Increase socket receive buffer size
    int recv_buffer_size = 8 * 1024 * 1024; // 8MB
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0)
    {
        std::cerr << "Failed to set SO_RCVBUF\n";
    }

    // Prepare server address
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, ip_.c_str(), &serv_addr.sin_addr) <= 0)
    {
        std::cerr << "Invalid address/ Address not supported \n";
        close(sock_fd_);
        sock_fd_ = -1;
        return;
    }

    // Connect to server
    struct epoll_event event;
    int res = connect(sock_fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (res == 0) {
        // Connection established immediately
        std::cout << "Connected to server immediately\n";
        // Register for EPOLLIN events to start reading data
        event.events = EPOLLIN | EPOLLET;
    } else if (res < 0 && errno == EINPROGRESS) {
        // Connection is in progress
        std::cout << "Connection in progress\n";
        // Register for EPOLLOUT events to detect when the socket becomes writable
        event.events = EPOLLOUT | EPOLLET;
    } else {
        // An error occurred
        std::cerr << "Connection Failed: " << strerror(errno) << "\n";
        close(sock_fd_);
        sock_fd_ = -1;
        return;
    }

    // Update epoll with the initial event settings
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_fd_, &event) == -1)
    {
        std::cerr << "epoll_ctl failed\n";
        close(sock_fd_);
        sock_fd_ = -1;
        close(epoll_fd_);
        epoll_fd_ = -1;
        return;
    }

    std::cout << "Data Ingestion Module Started. Waiting to ingest data...\n";

    // Event loop
    const int MAX_EVENTS = 10000;
    struct epoll_event events[MAX_EVENTS];

    while (running_.load(std::memory_order_acquire))
    {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1000); // 1 second timeout
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue; // Interrupted by signal
            }
            std::cerr << "epoll_wait error\n";
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            if (events[i].data.fd == sock_fd_)
            {
                if (events[i].events & EPOLLOUT) 
                {
                    // Finalize the non-blocking connect
                    int err = 0;
                    socklen_t len = sizeof(err);
                    if (getsockopt(sock_fd_, SOL_SOCKET, SO_ERROR, &err, &len) < 0) 
                    {
                        std::cerr << "getsockopt failed: " << strerror(errno) << "\n";
                        running_.store(false, std::memory_order_release);
                        break;
                    }
                    if (err != 0) 
                    {
                        std::cerr << "Connect failed with error: " << strerror(err) << "\n";
                        running_.store(false, std::memory_order_release);
                        break;
                    }
                    // Connection is established
                    std::cout << "Connected to server\n";
                    // Modify the events to listen for EPOLLIN
                    struct epoll_event new_event;
                    new_event.events = EPOLLIN | EPOLLET;
                    new_event.data.fd = sock_fd_;
                    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_fd_, &new_event) == -1) 
                    {
                        std::cerr << "epoll_ctl modify failed: " << strerror(errno) << "\n";
                        running_.store(false, std::memory_order_release);
                        break;
                    }
                }
                
                if (events[i].events & EPOLLIN)
                {
                    // std::cout << "EPOLLIN event received\n"; // Optional: comment out to reduce verbosity
                    while (true)
                    {
                        ssize_t count = recv(sock_fd_, buffer_, BUFFER_SIZE, 0);
                        if (count == -1)
                        {
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                            {
                                // All data read
                                break;
                            }
                            else
                            {
                                std::cerr << "recv error\n";
                                running_.store(false, std::memory_order_release);
                                break;
                            }
                        }
                        else if (count == 0)
                        {
                            // Connection closed
                            std::cerr << "Server closed connection\n";
                            running_.store(false, std::memory_order_release);
                            break;
                        }
                        else
                        {
                            // Process received data
                            size_t start = 0;
                            std::vector<std::shared_ptr<DataRecord>> batch_records;
                            for (ssize_t j = 0; j < count; ++j)
                            {
                                if (buffer_[j] == '\n')
                                {
                                    std::string_view msg_view(&buffer_[start], j - start);
                                    if (msg_view == "STOP")
                                    {
                                        std::cout << "Received STOP message. Terminating ingestion.\n";
                                        running_.store(false, std::memory_order_release);
                                        break;
                                    }

                                    auto record = memory_pool_.acquire();
                                    if (record)
                                    {
                                        record->timestamp = static_cast<uint64_t>(
                                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch()
                                            ).count()
                                        );
                                        record->message = std::string(msg_view);
                                        batch_records.push_back(record);
                                    }
                                    start = j + 1;
                                }
                            }

                            // Enqueue all records in the batch
                            for (auto& rec : batch_records)
                            {
                                data_queue_.enqueue(rec);
                            }

                            // Check if running flag was set to false by STOP message
                            if (!running_.load(std::memory_order_acquire))
                            {
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    std::cout << "Data Ingestion Module Stopped.\n";

    // Cleanup
    close(sock_fd_);
    sock_fd_ = -1;
    close(epoll_fd_);
    epoll_fd_ = -1;
}
