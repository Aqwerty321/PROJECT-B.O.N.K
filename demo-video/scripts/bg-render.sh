#!/usr/bin/env bash
# Render the demo video in the background.
# Usage: bash scripts/bg-render.sh
# Output: output/project.mp4
# Log:    /tmp/cascade-render.log

set -euo pipefail

export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"
nvm use 24 >/dev/null 2>&1

cd "$(dirname "$0")/.."
LOGFILE="/tmp/cascade-render.log"

echo "[$(date)] Starting render..." > "$LOGFILE"

# Kill any stale servers
pkill -f "vite.*9100" 2>/dev/null || true
sleep 1

# Remove old output
rm -f output/project.mp4

# Start Motion Canvas dev server
npx vite --host 127.0.0.1 --port 9100 >> "$LOGFILE" 2>&1 &
VITE_PID=$!
echo "[$(date)] Dev server PID: $VITE_PID" >> "$LOGFILE"

# Wait for server
for i in $(seq 1 60); do
  if curl -s -o /dev/null -w '' http://127.0.0.1:9100/ 2>/dev/null; then
    echo "[$(date)] Dev server ready" >> "$LOGFILE"
    break
  fi
  sleep 1
done

# Run the render script
node scripts/do-render.cjs >> "$LOGFILE" 2>&1
RENDER_EXIT=$?

echo "[$(date)] Render exited with code $RENDER_EXIT" >> "$LOGFILE"

# Check result
if [ -f output/project.mp4 ]; then
  SIZE=$(du -h output/project.mp4 | cut -f1)
  echo "[$(date)] Output: output/project.mp4 ($SIZE)" >> "$LOGFILE"
  
  # Try to get duration
  if command -v ffprobe &>/dev/null; then
    DUR=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 output/project.mp4 2>/dev/null || echo "unknown")
    echo "[$(date)] Duration: ${DUR}s" >> "$LOGFILE"
  fi
else
  echo "[$(date)] No output file produced!" >> "$LOGFILE"
fi

# Cleanup
kill $VITE_PID 2>/dev/null || true
echo "[$(date)] Done." >> "$LOGFILE"
