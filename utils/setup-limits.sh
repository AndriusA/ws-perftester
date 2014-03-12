#!/bin/bash

inst=(

)

for i in "${inst[@]}";
do
  {
    listener="ubuntu@"$i
    echo $listener
    rsync limits.conf sysctl.conf $listener":."
    ssh $listener "sudo cp limits.conf /etc/security/limits.conf"
    ssh $listener "sudo cp sysctl.conf /etc/sysctl.conf"
    ssh $listener "sudo sysctl -p"
    ssh $listener "sudo apt-get install libzmq3-dev -y"
  }&
done

echo "DONE"