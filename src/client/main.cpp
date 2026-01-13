#include "feed_handler.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <getopt.h>

mdf::FeedHandler* g_handler = nullptr;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_handler) {
        g_handler->stop();
    }
}

void print_usage(const char* program) {
    std::cout << "NSE Market Data Feed Handler Client\n\n";
    std::cout << "Usage: " << program << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --host <host>      Server hostname (default: localhost)\n";
    std::cout << "  -p, --port <port>      Server port (default: 9876)\n";
    std::cout << "  -t, --timeout <ms>     Connection timeout (default: 5000)\n";
    std::cout << "  -n, --no-visual        Disable terminal visualization\n";
    std::cout << "  -r, --no-reconnect     Disable auto-reconnect\n";
    std::cout << "  --help                 Show this help message\n";
    std::cout << "\nDuring operation:\n";
    std::cout << "  Press 'q' to quit\n";
    std::cout << "  Press 'r' to reset statistics\n";
}

int main(int argc, char* argv[]) {
    mdf::FeedHandlerConfig config;
    
    static struct option long_options[] = {
        {"host", required_argument, nullptr, 'h'},
        {"port", required_argument, nullptr, 'p'},
        {"timeout", required_argument, nullptr, 't'},
        {"no-visual", no_argument, nullptr, 'n'},
        {"no-reconnect", no_argument, nullptr, 'r'},
        {"help", no_argument, nullptr, '?'},
        {nullptr, 0, nullptr, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:t:nr", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                config.host = optarg;
                break;
            case 'p':
                config.port = static_cast<uint16_t>(std::atoi(optarg));
                break;
            case 't':
                config.connect_timeout_ms = static_cast<uint32_t>(std::atoi(optarg));
                break;
            case 'n':
                config.enable_visualization = false;
                break;
            case 'r':
                config.auto_reconnect = false;
                break;
            case '?':
            default:
                print_usage(argv[0]);
                return opt == '?' ? 0 : 1;
        }
    }
    
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);
    
    // Create and configure handler
    mdf::FeedHandler handler;
    g_handler = &handler;
    handler.configure(config);
    
    if (!config.enable_visualization) {
        std::cout << "============================================\n";
        std::cout << "      NSE Market Data Feed Handler          \n";
        std::cout << "============================================\n";
        std::cout << "Server:        " << config.host << ":" << config.port << "\n";
        std::cout << "Timeout:       " << config.connect_timeout_ms << "ms\n";
        std::cout << "Auto-Reconnect: " << (config.auto_reconnect ? "Enabled" : "Disabled") << "\n";
        std::cout << "============================================\n";
        std::cout << "Press Ctrl+C to stop\n\n";
    }
    
    // Run handler
    handler.run();
    
    // Print final stats
    std::cout << "\n";
    std::cout << "Final Statistics:\n";
    std::cout << "  Messages received: " << handler.messages_received() << "\n";
    std::cout << "  Bytes received: " << handler.bytes_received() << "\n";
    std::cout << "  Sequence gaps: " << handler.sequence_gaps() << "\n";
    
    auto stats = handler.get_latency_stats();
    std::cout << "  Latency (ns): min=" << stats.min 
              << " p50=" << stats.p50 
              << " p99=" << stats.p99 
              << " max=" << stats.max << "\n";
    
    std::cout << "  Reconnect count: " << handler.is_connected() << "\n";
    
    return 0;
}
