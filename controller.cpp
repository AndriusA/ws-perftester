#include "zhelpers.hpp"

struct config {
    static const int machines = 10;
    static const int processes = 10;
    static const int instances = 10;
    static const bool send_messages = true;          // Send messages once connection is established or only connect
    static const bool randomized_interval = true;    // Send messages at a randomized interval or exactly every t seconds
    static const int interval = 20;                  // Interval
    static const int messages = 1;                 // How many messages to send
};

int main () {
    std::cout << "Starting controller" << std::endl;
    zmq::context_t context(1);

    //  Socket to talk to clients
    zmq::socket_t publisher (context, ZMQ_PUB);
    publisher.bind("tcp://*:5561");

    //  Socket to receive signals
    zmq::socket_t syncservice (context, ZMQ_REP);
    syncservice.bind("tcp://*:5562");

    zmq::socket_t instance_syncservice (context, ZMQ_REP);
    instance_syncservice.bind("tcp://*:5563");

    //  Get synchronization from subscribers
    int subscribers = 0;
    std::cout << "Syncing slaves" << std::endl;   
    while (subscribers < config::machines) {
        s_recv (syncservice);
        s_send (syncservice, "");
        subscribers++;
    }
    sleep(2);

    s_sendmore(publisher, "StartProcesses");
    s_sendmore(publisher, std::to_string(config::processes));
    s_send(publisher, "");

    std::cout << "Syncing processes" << std::endl;
    int processes = 0;
    while (processes < config::machines*config::processes) {
        std::string reg = s_recv (instance_syncservice);
        // std::cout << reg << " synced" << std::endl;
        s_send (instance_syncservice, "");
        processes++;
    }

    std::cout << "Broadcasting start request" << std::endl;

    s_sendmore(publisher, "StartWebsockets");
    // s_sendmore(publisher, "ws://192.95.61.160:9002");
    s_sendmore(publisher, "ws://localhost:9002");
    s_sendmore(publisher, std::to_string(config::instances));
    s_sendmore(publisher, std::to_string(config::send_messages));
    s_sendmore(publisher, std::to_string(config::randomized_interval));
    s_sendmore(publisher, std::to_string(config::interval));
    s_sendmore(publisher, std::to_string(config::messages));
    s_send(publisher, "");

    std::cout << "Ending" << std::endl;
    s_send (publisher, "END");
    sleep (1);              //  Give 0MQ time to flush output
    
    zmq::socket_t receiver (context, ZMQ_PULL);
    receiver.bind("tcp://*:5558");

    int clientsFinished = 0;
    int clientsDied = 0;
    int resultsReceived = 0;
    int resultsExpected = config::machines*config::processes*config::instances;
    int connectionsDied = 0;

    // TODO: careful with overflows
    double meansum = 0.0;
    double variancesum = 0.0;
    int totalcount = 0;
    int samples = 0;
    while (clientsFinished + clientsDied < config::machines*config::processes) {
        std::string status = s_recv(receiver);
        if (status == "Finished") {
            std::string name = s_recv(receiver);
            std::string exit_status = s_recv(receiver);
            // std::cout << name << " exited with status " << exit_status << std::endl;
            if (exit_status == "0")
                clientsFinished++;
            else
                clientsDied++;
        } else if (status == "Result") {
            std::string stats = s_recv(receiver);
            resultsReceived++;
            s_recv(receiver);
            std::stringstream ss(stats);
            int count;
            double min, max, mean, variance;
            ss >> count;
            ss >> min;
            ss >> max;
            ss >> mean;
            ss >> variance;
            if (count < config::messages - 1)
                connectionsDied++;
            else if (count > 0) {
                meansum += mean*count;
                totalcount += count;
                samples++;
                variancesum += (count - 1)*variance;
            }
            if (resultsExpected / 100 > 0 && resultsReceived % (resultsExpected/100) == 0)
                std::cout << (resultsReceived*100/resultsExpected) << "%" << std::endl;
                
            // std::cout << "Received a set of results: " << stats << std::endl;
        }
    }
    std::cout << "Clients completed " << clientsFinished << ", clients died " << clientsDied << std::endl;
    std::cout << "Connections died " << connectionsDied << std::endl;
    double overallMean = meansum/totalcount;
    double overallVariance = variancesum/(totalcount-samples);
    std::cout << "Average message latency " << overallMean << "ms, variance " << overallVariance << std::endl; 
}