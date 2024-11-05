// benchmarks/ingestion_benchmark.cpp

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
    // Usage: ./ingestion_benchmark [mock_server_core] [ingestion_thread_core1,ingestion_thread_core2,...]
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
    int num_messages = 100000; // Adjust as needed for benchmarking
    int interval_us = 1;        // Microseconds between messages

    // Start mock server in a separate thread
    std::thread server_thread(simulate_server, config.port, num_messages, interval_us, mock_server_core);

    // Give the server a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Start Data Ingestion
    DataIngestion ingestion(config.ip, config.port, ingestion_thread_cores);
    ingestion.start();

    // Capture start time right before sending messages
    auto start_time = std::chrono::high_resolution_clock::now();

    // Wait for the ingestion module to process all messages
    std::shared_ptr<DataRecord> record;
    while (ingestion.get_data(record))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Stop ingestion
    ingestion.stop();

    // Capture end time after processing
    auto end_time = std::chrono::high_resolution_clock::now();

    // Calculate duration
    std::chrono::duration<double> duration = end_time - start_time;

    // Retrieve ingested data
    std::vector<std::shared_ptr<DataRecord>> data;
    while (ingestion.get_data(record))
    {
        data.push_back(record);
    }

    // Calculate and display messages per second
    double msgs_per_sec = data.size() / duration.count();
    std::cout << "Benchmark Results:\n";
    std::cout << "Total Messages Ingested: " << data.size() << std::endl;
    std::cout << "Time Taken: " << duration.count() << " seconds" << std::endl;
    std::cout << "Throughput: " << msgs_per_sec << " messages/second" << std::endl;

    // Join server thread
    if (server_thread.joinable())
    {
        server_thread.join();
    }

    return 0;
}
