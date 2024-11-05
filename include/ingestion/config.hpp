// include/ingestion/config.hpp

#pragma once

#include <string>

// Ingestion Configuration Structure
struct IngestionConfig
{
    std::string ip;
    int port;
    // Add more configuration parameters as needed
};

// Function to retrieve default configuration
IngestionConfig get_default_config();
