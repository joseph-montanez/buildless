#!/bin/bash

while true; do
    # Attempt to netcat to your server's port; replace 8080 with your port
    nc -z localhost 8080
    if [ $? -ne 0 ]; then
        echo "Server down! Restarting..."
        nohup ./server &  # Adjust this command to how you run your server
    fi
    sleep 10  # Check every 10 seconds, adjust this as needed
done