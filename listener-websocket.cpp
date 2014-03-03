#include "zhelpers.hpp"
#include "clients/telemetry-client-tls.hpp"
#include <stdint.h>


void doTheWork(std::string clientName, std::string uri, 
    int instances, bool send_messages, bool randomized_interval, 
    int interval, int messages, int message_length, 
    zmq::socket_t & sink)
{
    // 1. Create the client instances
    // 2. Connect them in parallel and start sending (run)
    // 3. Wait for all to finish
    // 4. Process and return results

    int id_length_bytes = 16;
    int longsize = sizeof(unsigned long);
    char client_id[id_length_bytes];
    for (int i = 0; i < id_length_bytes; i += longsize) {
        for (int j = 0; j < longsize; j++) {
            client_id[i+j] = static_cast<char>((i >> (4*j)) & 0xFF);
        }
    }
    std::string id = clientName;
    id += client_id;
    std::cout << clientName << "+" << client_id << "=" << id << std::endl;

    std::vector<telemetry_client::ptr> clients;
    for (int i = 0; i < instances; i++) {

        telemetry_client::ptr client(new telemetry_client(id, messages, randomized_interval, interval, message_length));
        clients.push_back(client);
    }
    for (int i = 0; i < instances; i++) {
        std::cout << "run client " << i << std::endl;
        clients[i]->run(uri);
    }
    for (int i = 0; i < instances; i++) {
        clients[i]->wait();
    }
    for (int i = 0; i < instances; i++) {
        std::string stats = clients[i]->get_stats();
        s_sendmore(sink, "Result");
        s_sendmore(sink, stats);
        s_send(sink, "");
    }
}

int main (int argc, char *argv[]) {
    zmq::context_t context(1);
    std::string listenerName = "";
    if (argc >= 2) {
        listenerName += argv[1];
    }
    std::string address = "localhost";
    if (argc >= 3)
        address = argv[2];
    std::cout << "Starting client " << listenerName << std::endl;

    std::string subscriber_addr = "tcp://"+address+":5561";
    std::string syncclient_addr = "tcp://"+address+":5563";
    std::string sink_addr = "tcp://"+address+":5564";

    //  First, connect our subscriber socket
    zmq::socket_t subscriber (context, ZMQ_SUB);
    subscriber.connect(subscriber_addr.c_str());
    subscriber.setsockopt(ZMQ_SUBSCRIBE, "StartWebsockets", strlen("StartWebsockets"));
    subscriber.setsockopt(ZMQ_SUBSCRIBE, "HELLO_SOCKET", strlen("HELLO_SOCKET"));

    //  Second, synchronize child processes
    zmq::socket_t syncclient (context, ZMQ_REQ);
    syncclient.connect(syncclient_addr.c_str());

    //  Finally, send back some results
    zmq::socket_t sink (context, ZMQ_PUSH);
    sink.connect(sink_addr.c_str());

    while(1) {
        std::string msg = s_recv(subscriber);
        if (msg == "HELLO_SOCKET") {
            std::cout << listenerName << " Sending synchronization request" << std::endl;
            s_send (syncclient, listenerName);
            s_recv (syncclient);
            // std::cout << listenerName << " Received sync response " << std::endl;

            //  Third, get our updates and report how many we got
            // std::cout << listenerName << " Receiving processing requests" << std::endl;
            
            std::istringstream ss();
            while(1) {
                std::string processingRequest = s_recv(subscriber);
                if (processingRequest == "StartWebsockets") {
                    std::string uri;
                    int instances, interval, messages, message_length;
                    bool send_messages, randomized_interval;
                    int param_count = 7;
                    // Receive all parts of a multipart message
                    // A state machine of sorts...
                    for (int i = 0; i < param_count; i++) {
                        std::istringstream ss(s_recv(subscriber));
                        switch (i) {
                            case 0: ss >> uri; break;
                            case 1: ss >> instances; break;
                            case 2: ss >> send_messages; break;
                            case 3: ss >> randomized_interval; break;
                            case 4: ss >> interval; break;
                            case 5: ss >> messages; break;
                            case 6: ss >> message_length; break;
                        }
                    }
                    // Receive the empty delimiter as well
                    s_recv(subscriber);
                    std::cout << "Received request " << processingRequest << " " 
                                                    << instances << " "
                                                    << send_messages << " "
                                                    << randomized_interval << " "
                                                    << interval << " "
                                                    << messages << " "
                                                    << std::endl;
                    // TODO do the work!
                    try {
                        doTheWork(listenerName, uri, instances, send_messages, randomized_interval, interval, messages, message_length, sink);
                    } catch ( std::exception &e) {
                        std::cout << "Exception caught " << e.what() << std::endl;
                    }
                    break;
                } else {
                    // std::cerr << "Wrong command" << std::endl;
                }
            }
            std::cout << listenerName << "DONE" << std::endl;
            break;
        }
    }

    // std::cout << "Starting client" << std::endl;
    // telemetry_client client("", 10, true, 1, 256);
    // std::cout << "Run!" << std::endl;
    // client.run("ws://localhost:9002");
    // client.wait();

    return 0;
}
