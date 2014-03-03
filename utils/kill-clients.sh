#!/bin/bash

inst=(
"ec2-SOMETHINGSOMETHING"
)

for i in "${inst[@]}";
do
  {
    listener="ubuntu@"$i
    ssh $listener "killall listener-websocket"
  }
done

echo "DONE"

