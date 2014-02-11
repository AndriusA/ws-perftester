#!/bin/bash
cd .build/
echo "Starting subscribers..."
for ((a=0; a<10; a++)); do
    ./listener localhost &
done
echo "Starting publisher..."
./controller
