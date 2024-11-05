// mock_server/mock_server.cpp

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <netinet/tcp.h>

// Function to set socket options for performance
bool set_socket_options(int sockfd)
{
    int opt = 1;
    // Disable Nagle's algorithm for low latency
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0)
    {
        std::cerr << "setsockopt TCP_NODELAY failed\n";
        return false;
    }
    // Increase send buffer size
    int send_buffer_size = 8 * 1024 * 1024; // 8MB
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0)
    {
        std::cerr << "setsockopt SO_SNDBUF failed\n";
        return false;
    }
    return true;
}

void mock_server(int port, int num_messages, int interval_us, const std::string& stop_message = "STOP")
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        std::cerr << "Socket failed\n";
        return;
    }

    // Attach socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        std::cerr << "setsockopt failed\n";
        close(server_fd);
        return;
    }

    // Initialize address struct
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    address.sin_port = htons(port);

    // Bind the socket
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        std::cerr << "Bind failed\n";
        close(server_fd);
        return;
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0)
    {
        std::cerr << "Listen failed\n";
        close(server_fd);
        return;
    }

    std::cout << "Mock server listening on port " << port << std::endl;

    // Accept a single connection
    if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0)
    {
        std::cerr << "Accept failed\n";
        close(server_fd);
        return;
    }

    std::cout << "Mock server accepted a connection\n";

    // Set socket options for performance
    if (!set_socket_options(new_socket))
    {
        std::cerr << "Failed to set socket options\n";
        close(new_socket);
        close(server_fd);
        return;
    }

    // Prepare messages
    std::string base_message = "Benchmark Message ";
    for (int i = 0; i < num_messages; ++i)
    {
        std::string msg = base_message + std::to_string(i) + "\n";
        ssize_t sent = send(new_socket, msg.c_str(), msg.size(), 0);
        if (sent != (ssize_t)msg.size())
        {
            std::cerr << "Failed to send message " << i << "\n";
            break;
        }
        // Sleep for interval_us microseconds
        if (interval_us > 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(interval_us));
        }
    }

    // Send STOP message
    std::string stop_msg = stop_message + "\n";
    ssize_t sent = send(new_socket, stop_msg.c_str(), stop_msg.size(), 0);
    if (sent != (ssize_t)stop_msg.size())
    {
        std::cerr << "Failed to send STOP message\n";
    }
    else
    {
        std::cout << "Mock server sent STOP message\n";
    }

    std::cout << "Mock server sent all messages and is closing connection\n";
    close(new_socket);
    close(server_fd);
}

int main(int argc, char* argv[])
{
    if (argc < 4)
    {
        std::cerr << "Usage: mock_server <port> <num_messages> <interval_us> [stop_message]\n";
        return -1;
    }

    int port = std::stoi(argv[1]);
    int num_messages = std::stoi(argv[2]);
    int interval_us = std::stoi(argv[3]);
    std::string stop_message = "STOP";

    if (argc >= 5)
    {
        stop_message = argv[4];
    }

    mock_server(port, num_messages, interval_us, stop_message);

    return 0;
}
