#!/bin/bash

inst=(
"ec2-SOMETHINGSOMETHING"
)

for i in "${inst[@]}";
do
  {
    listener="ubuntu@"$i
    echo $listener
    scp limits.conf $listener":."
    ssh $listener "sudo cp limits.conf /etc/security/limits.conf"
    ssh $listener "sudo apt-get install libzmq3-dev -y"
  }&
done

echo "DONE"
