// src/main.cpp

#include "ingestion/data_ingestion.hpp"
#include "ingestion/config.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <cstdlib>
#include <vector>
#include <unistd.h> // for sysconf

// Function to simulate a server sending test messages with CPU pinning
void simulate_server(int port, int num_messages, int interval_us, int cpu_core)
{
    // Build the command to run the mock server with CPU affinity
    std::string command = "taskset -c " + std::to_string(cpu_core) + " ./mock_server " +
                          std::to_string(port) + " " +
                          std::to_string(num_messages) + " " +
                          std::to_string(interval_us) + " STOP";
    int ret = system(command.c_str());
    if (ret != 0)
    {
        std::cerr << "Failed to start mock server with command: " << command << "\n";
    }
}

int main(int argc, char* argv[])
{
    // Determine the number of available CPU cores
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores < 2)
    {
        std::cerr << "Not enough CPU cores for pinning.\n";
        return -1;
    }

    // Default CPU cores
    int mock_server_core = 1;
    std::vector<int> ingestion_thread_cores = {2};

    // Parse command-line arguments
    // Usage: ./data_ingestion [mock_server_core] [ingestion_thread_core1,ingestion_thread_core2,...]
    if (argc >= 2)
    {
        mock_server_core = std::stoi(argv[1]);
        if (mock_server_core < 0 || mock_server_core >= num_cores)
        {
            std::cerr << "Invalid mock_server_core. Must be between 0 and " << num_cores - 1 << ".\n";
            return -1;
        }
    }
    if (argc >= 3)
    {
        // Assume comma-separated core numbers
        std::string cores_str = argv[2];
        ingestion_thread_cores.clear();
        size_t start = 0;
        size_t end = cores_str.find(',');
        while (end != std::string::npos)
        {
            int core = std::stoi(cores_str.substr(start, end - start));
            if (core >= 0 && core < num_cores)
            {
                ingestion_thread_cores.push_back(core);
            }
            else
            {
                std::cerr << "Invalid ingestion_thread_core: " << core << ". Must be between 0 and " << num_cores - 1 << ".\n";
                return -1;
            }
            start = end + 1;
            end = cores_str.find(',', start);
        }
        // Last core
        int core = std::stoi(cores_str.substr(start));
        if (core >= 0 && core < num_cores)
        {
            ingestion_thread_cores.push_back(core);
        }
        else
        {
            std::cerr << "Invalid ingestion_thread_core: " << core << ". Must be between 0 and " << num_cores - 1 << ".\n";
            return -1;
        }
    }

    // Configuration
    IngestionConfig config = get_default_config();

    // Test parameters
    int num_messages = 100000; // Adjust as needed for testing
    int interval_us = 10;      // Microseconds between messages

    // Start mock server in a separate thread
    std::thread server_thread(simulate_server, config.port, num_messages, interval_us, mock_server_core);

    // Give the server a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Start Data Ingestion
    DataIngestion ingestion(config.ip, config.port, ingestion_thread_cores);
    ingestion.start();

    // Wait for the ingestion module to process all messages
    // This is determined by the "STOP" message from the server
    // Wait until the ingestion module stops running
    std::shared_ptr<DataRecord> record;
    while (ingestion.get_data(record))
    {
        // Optionally, process the record
        // Example:
        // std::cout << "Timestamp: " << record->timestamp << ", Message: " << record->message << std::endl;
    }

    // Stop ingestion
    ingestion.stop();

    // Join server thread
    if (server_thread.joinable())
    {
        server_thread.join();
    }

    // Retrieve ingested data
    std::vector<std::shared_ptr<DataRecord>> data;
    while (ingestion.get_data(record))
    {
        data.push_back(record);
    }

    // Calculate and display messages per second
    // Since we no longer have fixed timing, we'll need to measure time differently
    // For simplicity, we can assume the server has sent all messages by now
    // Alternatively, integrate timing within the ingestion module

    // For now, display the total messages ingested
    std::cout << "Total Messages Ingested: " << data.size() << std::endl;

    // Optionally, process or analyze the ingested data here

    return 0;
}
