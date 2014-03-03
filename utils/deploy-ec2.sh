#!/bin/bash

server="ec2-107-20-116-215.compute-1.amazonaws.com"
coordinator="ec2-23-23-26-156.compute-1.amazonaws.com"

inst=(
"ec2-54-237-79-240.compute-1.amazonaws.com"
#"ec2-54-237-102-219.compute-1.amazonaws.com"
#"ec2-107-21-91-56.compute-1.amazonaws.com"
#"ec2-23-20-110-107.compute-1.amazonaws.com"
#"ec2-54-80-110-133.compute-1.amazonaws.com"
#"ec2-75-101-191-148.compute-1.amazonaws.com"
#"ec2-54-197-118-97.compute-1.amazonaws.com"
#"ec2-107-22-20-202.compute-1.amazonaws.com"
#"ec2-50-19-56-66.compute-1.amazonaws.com"
#"ec2-54-211-75-78.compute-1.amazonaws.com"
#"ec2-54-80-78-24.compute-1.amazonaws.com"
#"ec2-174-129-88-33.compute-1.amazonaws.com"
#"ec2-54-211-21-204.compute-1.amazonaws.com"
#"ec2-54-198-210-56.compute-1.amazonaws.com"
#"ec2-54-198-252-221.compute-1.amazonaws.com"
#"ec2-54-234-188-73.compute-1.amazonaws.com"
)

rsync -avz ~/mpWS/websocketpp/build/release/echo_server $server":."
rsync -avz ~/mpWS/websocketpp/build/release/echo_server_tls $server":."
rsync -avz .build/controller $coordinator":."

ssh $coordinator "killall controller screen"
ssh $server "killall echo_server screen";

echo "Starting server";
launchServer='screen -S server -p 0 -X stuff "~/echo_server/echo_server | tee ~/echo_server/connections.txt $(printf \\r) & stud --config=stud/stud.conf "'
ssh $server "screen -dmS server sh";
ssh $server $launchServer 

lastInst=${inst[${#inst[@]} - 1]}

for i in "${inst[@]}";
do
  listener=$i;
  launchListener='screen -S listener -p 0 -X stuff "./listener '$coordinator' $(printf \\r)"';
  {
    rsync -avz .build/listener .build/listener-websocket $listener":." &
    ssh $listener "killall listener listener-websocket screen";
    ssh $listener "screen -dmS listener";
    ssh $listener $launchListener;
  }
done


echo "Launching controller"
launchController='screen -S controller -p 0 -X stuff "./controller | tee controller.txt $(printf \\r)"'
ssh $coordinator "screen -dmS controller"
ssh $coordinator $launchController

echo "DONE"
