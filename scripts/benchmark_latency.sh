#!/bin/bash
# Benchmark latency script

cd "$(dirname "$0")/.."

DURATION=${1:-30}
RATE=${2:-100000}
PORT=9877  # Use different port for benchmark

echo "============================================"
echo "  Latency Benchmark"
echo "  Duration: ${DURATION}s"
echo "  Rate: ${RATE} msgs/sec"
echo "============================================"

# Build if needed
if [ ! -f "build/exchange_simulator" ] || [ ! -f "build/feed_handler" ]; then
    echo "Building project..."
    ./scripts/build.sh Release
fi

# Start server with specified rate
echo "Starting server..."
./build/exchange_simulator -p $PORT -r $RATE &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Failed to start server"
    exit 1
fi

# Run client without visualization for accurate timing
echo "Running benchmark for ${DURATION} seconds..."
timeout $DURATION ./build/feed_handler -p $PORT -n -r 2>&1 | tee benchmark_output.txt

# Stop server
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "Benchmark complete!"
echo "Results saved to benchmark_output.txt"
