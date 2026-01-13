#include <iostream>
#include <cassert>
#include <cstring>
#include "../include/protocol.h"

using namespace mdf;

void test_message_sizes() {
    std::cout << "Testing message sizes... ";
    
    assert(sizeof(MessageHeader) == HEADER_SIZE);
    assert(sizeof(TradePayload) == TRADE_PAYLOAD_SIZE);
    assert(sizeof(QuotePayload) == QUOTE_PAYLOAD_SIZE);
    
    assert(TRADE_MSG_SIZE == HEADER_SIZE + TRADE_PAYLOAD_SIZE + CHECKSUM_SIZE);
    assert(QUOTE_MSG_SIZE == HEADER_SIZE + QUOTE_PAYLOAD_SIZE + CHECKSUM_SIZE);
    assert(HEARTBEAT_MSG_SIZE == HEADER_SIZE + CHECKSUM_SIZE);
    
    std::cout << "PASSED\n";
}

void test_checksum() {
    std::cout << "Testing checksum... ";
    
    // Test known data
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint32_t checksum1 = calculate_checksum(data, sizeof(data));
    uint32_t checksum2 = calculate_checksum(data, sizeof(data));
    
    assert(checksum1 == checksum2);  // Deterministic
    
    // Different data should produce different checksum
    data[0] = 0xFF;
    uint32_t checksum3 = calculate_checksum(data, sizeof(data));
    assert(checksum1 != checksum3);
    
    std::cout << "PASSED\n";
}

void test_message_type_sizes() {
    std::cout << "Testing get_message_size... ";
    
    assert(get_message_size(MessageType::TRADE) == TRADE_MSG_SIZE);
    assert(get_message_size(MessageType::QUOTE) == QUOTE_MSG_SIZE);
    assert(get_message_size(MessageType::HEARTBEAT) == HEARTBEAT_MSG_SIZE);
    assert(get_message_size(static_cast<MessageType>(0xFF)) == 0);
    
    std::cout << "PASSED\n";
}

void test_symbol_names() {
    std::cout << "Testing symbol names... ";
    
    // Known symbols
    assert(std::string(get_symbol_name(0)) == "RELIANCE");
    assert(std::string(get_symbol_name(1)) == "TCS");
    assert(std::string(get_symbol_name(2)) == "INFY");
    
    // Unknown symbol (should return SYMxxx format)
    const char* name = get_symbol_name(100);
    assert(name[0] == 'S' && name[1] == 'Y' && name[2] == 'M');
    
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Protocol Tests ===\n";
    
    test_message_sizes();
    test_checksum();
    test_message_type_sizes();
    test_symbol_names();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
