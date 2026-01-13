#!/bin/bash
# Run Exchange Simulator

cd "$(dirname "$0")/.."

# Default settings
PORT=9876
SYMBOLS=100
RATE=100000
MARKET="neutral"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--port) PORT="$2"; shift 2 ;;
        -s|--symbols) SYMBOLS="$2"; shift 2 ;;
        -r|--rate) RATE="$2"; shift 2 ;;
        -m|--market) MARKET="$2"; shift 2 ;;
        -f|--fault) FAULT="-f"; shift ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "  -p, --port <port>      Server port (default: 9876)"
            echo "  -s, --symbols <count>  Number of symbols (default: 100)"
            echo "  -r, --rate <rate>      Tick rate/sec (default: 100000)"
            echo "  -m, --market <type>    neutral, bull, bear (default: neutral)"
            echo "  -f, --fault            Enable fault injection"
            exit 0
            ;;
        *) shift ;;
    esac
done

# Check if executable exists
if [ ! -f "build/exchange_simulator" ]; then
    echo "Error: exchange_simulator not found. Run ./scripts/build.sh first."
    exit 1
fi

# Run simulator
./build/exchange_simulator -p "$PORT" -s "$SYMBOLS" -r "$RATE" -m "$MARKET" $FAULT
