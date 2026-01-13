#!/bin/bash
# Run Feed Handler Client

cd "$(dirname "$0")/.."

# Default settings
HOST="localhost"
PORT=9876
TIMEOUT=5000

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--host) HOST="$2"; shift 2 ;;
        -p|--port) PORT="$2"; shift 2 ;;
        -t|--timeout) TIMEOUT="$2"; shift 2 ;;
        -n|--no-visual) NOVISUAL="-n"; shift ;;
        -r|--no-reconnect) NORECONNECT="-r"; shift ;;
        --help)
            echo "Usage: $0 [options]"
            echo "  -h, --host <host>      Server hostname (default: localhost)"
            echo "  -p, --port <port>      Server port (default: 9876)"
            echo "  -t, --timeout <ms>     Connection timeout (default: 5000)"
            echo "  -n, --no-visual        Disable visualization"
            echo "  -r, --no-reconnect     Disable auto-reconnect"
            exit 0
            ;;
        *) shift ;;
    esac
done

# Check if executable exists
if [ ! -f "build/feed_handler" ]; then
    echo "Error: feed_handler not found. Run ./scripts/build.sh first."
    exit 1
fi

# Run client
./build/feed_handler -h "$HOST" -p "$PORT" -t "$TIMEOUT" $NOVISUAL $NORECONNECT
