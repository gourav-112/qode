#!/bin/bash
# Run complete demo - starts both server and client

cd "$(dirname "$0")/.."

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}  NSE Market Data Feed Handler Demo${NC}"
echo -e "${GREEN}============================================${NC}"

# Build if needed
if [ ! -f "build/exchange_simulator" ] || [ ! -f "build/feed_handler" ]; then
    echo -e "${YELLOW}Building project...${NC}"
    ./scripts/build.sh
    echo ""
fi

# Start server in background, redirect output to log file
echo -e "${YELLOW}Starting Exchange Simulator...${NC}"
echo "Server output will be written to output.log"
./build/exchange_simulator -r 50000 > output.log 2>&1 &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Failed to start server${NC}"
    exit 1
fi

echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}"
echo ""

# Start client (foreground) with dump file
echo -e "${YELLOW}Starting Feed Handler...${NC}"
echo -e "${YELLOW}Press 'q' to quit, 'r' to reset stats${NC}"
echo -e "${YELLOW}Messages will be dumped to: dump${NC}"
echo ""
sleep 1

./build/feed_handler --dump dump

# Cleanup
echo ""
echo -e "${YELLOW}Stopping server...${NC}"
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo -e "${GREEN}Demo complete!${NC}"
