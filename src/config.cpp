// src/config.cpp

#include "ingestion/config.hpp"

// Implement configuration loading if needed
// For simplicity, we'll return hardcoded default values

IngestionConfig get_default_config()
{
    IngestionConfig config;
    config.ip = "127.0.0.1"; // Localhost for testing
    config.port = 5555;       // Example port
    return config;
}
