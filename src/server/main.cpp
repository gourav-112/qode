#include "exchange_simulator.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <getopt.h>

mdf::ExchangeSimulator* g_simulator = nullptr;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_simulator) {
        g_simulator->stop();
    }
}

void print_usage(const char* program) {
    std::cout << "NSE Market Data Exchange Simulator\n\n";
    std::cout << "Usage: " << program << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -p, --port <port>      Server port (default: 9876)\n";
    std::cout << "  -s, --symbols <count>  Number of symbols (default: 100)\n";
    std::cout << "  -r, --rate <rate>      Tick rate per second (default: 100000)\n";
    std::cout << "  -m, --market <type>    Market condition: neutral, bull, bear (default: neutral)\n";
    std::cout << "  -f, --fault            Enable fault injection (1% sequence gaps)\n";
    std::cout << "  -h, --help             Show this help message\n";
}

int main(int argc, char* argv[]) {
    uint16_t port = mdf::DEFAULT_PORT;
    size_t num_symbols = 100;
    uint32_t tick_rate = 100000;
    mdf::TickGenerator::MarketCondition market = mdf::TickGenerator::MarketCondition::NEUTRAL;
    bool fault_injection = false;
    
    static struct option long_options[] = {
        {"port", required_argument, nullptr, 'p'},
        {"symbols", required_argument, nullptr, 's'},
        {"rate", required_argument, nullptr, 'r'},
        {"market", required_argument, nullptr, 'm'},
        {"fault", no_argument, nullptr, 'f'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "p:s:r:m:fh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'p':
                port = static_cast<uint16_t>(std::atoi(optarg));
                break;
            case 's':
                num_symbols = static_cast<size_t>(std::atoi(optarg));
                break;
            case 'r':
                tick_rate = static_cast<uint32_t>(std::atoi(optarg));
                break;
            case 'm':
                if (std::string(optarg) == "bull") {
                    market = mdf::TickGenerator::MarketCondition::BULLISH;
                } else if (std::string(optarg) == "bear") {
                    market = mdf::TickGenerator::MarketCondition::BEARISH;
                }
                break;
            case 'f':
                fault_injection = true;
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }
    
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
    
    // Create and configure simulator
    mdf::ExchangeSimulator simulator(port, num_symbols);
    g_simulator = &simulator;
    
    simulator.set_tick_rate(tick_rate);
    simulator.set_market_condition(market);
    simulator.enable_fault_injection(fault_injection);
    
    // Set disconnect callback
    simulator.set_disconnect_callback([](int fd, const std::string& reason) {
        std::cout << "Client fd=" << fd << " disconnected: " << reason << std::endl;
    });
    
    std::cout << "============================================\n";
    std::cout << "      NSE Market Data Exchange Simulator    \n";
    std::cout << "============================================\n";
    std::cout << "Port:          " << port << "\n";
    std::cout << "Symbols:       " << num_symbols << "\n";
    std::cout << "Tick Rate:     " << tick_rate << " msgs/sec\n";
    std::cout << "Market:        " << (market == mdf::TickGenerator::MarketCondition::BULLISH ? "Bullish" :
                                       market == mdf::TickGenerator::MarketCondition::BEARISH ? "Bearish" : "Neutral") << "\n";
    std::cout << "Fault Inject:  " << (fault_injection ? "Enabled" : "Disabled") << "\n";
    std::cout << "============================================\n";
    std::cout << "Press Ctrl+C to stop\n\n";
    
    // Start and run
    simulator.start();
    simulator.run();
    
    std::cout << "Simulator stopped.\n";
    std::cout << "Total messages sent: " << simulator.messages_sent() << "\n";
    std::cout << "Total bytes sent: " << simulator.total_bytes_sent() << "\n";
    
    return 0;
}
