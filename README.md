WS-Perftester is a tool for stress-testing WebSocket servers.

The main features that I didn't find elsewhere include:
- Long-lived websocket connections
- Sending messages at periodic and randomized intervals over the connections
- Sending messages with different size (random) payloads
- Managing a distributed set of WebSocket clients

On the immediate TODO list:
- Dealing better with failures (at the moment expect everything to shut down nicely)
- Replaying message sequences (i.e. a log with timing and message size information)


Usage
--------

Configuration is rather manual at the moment: set up the different configurations as well as the address of the receiving server in `controller.cpp`.

Configuration parameters are:
- *machines* the number of machines in the testbed you want to launch connections from. You have to start listeners on each of them yourself (or modifying the *start.sh* script)
- *processes* the number of processes each of the machines starts in parallel to run websocket clients
- *instances* the number of websocket client instances in each process (think of a single client using multiple connections or a single system process reused by all processes, e.g. firefox)
- *send_messages* whether to send messages on the client or close immediately after setup
- *randomized_interval* whether to randomize interval between subsequent messages. Currently responsibility of the client to take this parameter into account, check *telemetry-client.hpp* for an example
- *interval* average interval between messages
- *messages* number of messages to send before closing the connection
- *message_length* length of the random payload

Launch at least the configured number of listeners, ideally on different machines although the coordination framework does not care whether they are on the same machine or not. Then launch the controller and sit back - depending on your configuration the tests could take a while.

You should not need to modify listener.cpp and only change listener-websocket.cpp to use the desired implementation of websocket client (through header files). Adaptation of the client will be more involved to benefit from the features, which I will describe later...

Coordination
------------

Using ZMQ to coordinate the different entities. Diagram to come...

Basically, there is one central controller, which waits until enough listeners join and tells them to launch the configured number of processes. It then waits for the processes to spin up and join and once a sufficient number of them do, publishes another request to start websocket connections with the desired configuration. Finally, the sink part of the controller waits for results and outputs aggregated statistics over all connections.

Dependencies
------------

- WebsocketPP
- ZMQ c++
- zmq helpers (https://github.com/imatix/zguide/blob/master/examples/C%2B%2B/zhelpers.hpp) - plan to remove this dependency
- Boost Process for listener (http://www.highscore.de/boost/process/) not part of official Boost set of libs..
