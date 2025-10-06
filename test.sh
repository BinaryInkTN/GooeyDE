#!/bin/bash

# Kill any existing Xephyr instances
pkill Xephyr

# Set a unique display number for Xephyr
DISPLAY_NUM=99
DISPLAY=":${DISPLAY_NUM}"

# Set the DISPLAY for Xephyr (it's not connected to host X server, so no need to set host DISPLAY)
export DISPLAY="${DISPLAY}"

echo "Starting Xephyr on display ${DISPLAY}"

# Start Xephyr with explicit parameters to avoid interference with the host's display
Xephyr -ac -br -noreset -screen 1024x768 "${DISPLAY}" &
XEPHYR_PID=$!

# Wait for Xephyr to start
sleep 2

# Set the DISPLAY for the nested session to the Xephyr display
export DISPLAY="${DISPLAY}"

# Now you can start the window manager or application inside Xephyr
echo "Starting GooeyShell..."
./gooey_shell &
WM_PID=$!

echo "Xephyr and GooeyShell started. Xephyr PID: ${XEPHYR_PID}, WM PID: ${WM_PID}"
