#include "zhelpers.hpp"

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include <cmath>

using namespace boost::accumulators;

class config {
public:
    config () {
        machines = 1;
        processes = 100;
        instances = 75;
        send_messages = true;           // Send messages once connection is established or only connect
        randomized_interval = true;     // Send messages at a randomized interval or exactly every t seconds
        interval = 60;                  // Mean interval
        messages = 12;                   
        message_length = 240;           // Message payload length in bytes (random payload)
        bwBytes = 4000000;
    }
    int machines, processes, instances, interval, messages, message_length, bwBytes;
    bool send_messages, randomized_interval;
};

int main () {
    std::cout << "Starting controller" << std::endl;
    zmq::context_t context(1);

    //  Socket to talk to clients
    zmq::socket_t publisher (context, ZMQ_PUB);
    publisher.bind("tcp://*:5561");

    //  Socket to receive signals
    zmq::socket_t syncservice (context, ZMQ_REP);
    const int timeout = 5000;
    syncservice.setsockopt( ZMQ_RCVTIMEO, &timeout, sizeof( timeout ) );
    syncservice.bind("tcp://*:5562");

    zmq::socket_t instance_syncservice (context, ZMQ_REP);
    instance_syncservice.setsockopt( ZMQ_RCVTIMEO, &timeout, sizeof( timeout ) );
    instance_syncservice.bind("tcp://*:5563");

    zmq::socket_t receiver (context, ZMQ_PULL);
    receiver.bind("tcp://*:5564");

    int iterations = 30;
    config conf[iterations];
    //for (int i = 1; i < iterations; ++i) {
    //    conf[i].instances= conf[i-1].instances*2;
    //    //conf[i].messages = conf[i].processes;
        
    //    int trafficBytes = conf[i].message_length * conf[i].machines * conf[i].processes * conf[i].instances;
    //    conf[i].interval = trafficBytes/conf[i].bwBytes * 5 + 2
;
    //    // int overallSetupDelay = conf[i].instances * conf[i].interval ; 
    //    conf[i].messages = conf[i].instances * 7 / conf[i].interval + 3;       // Want to stay all connections alive while ramping up

    //}
    // conf[iterations-1].processes = 20;
    iterations = 30;

    for (int i = 0; i < iterations; ++i) {
        conf[i].machines = i+1; 
    }
    
    for (int iteration = 16; iteration < 17; iteration = iteration+2) {
        //  Get synchronization from subscribers
        int subscribers = 0;
        std::cout << "Syncing slaves" << std::endl;   
        while (subscribers < conf[iteration].machines) {
            std::cout << "sync " << subscribers+1 << std::endl;
            zmq::message_t message;
            int ret = syncservice.recv(&message);
            if (!ret) {
                // std::cout << "again...";
                // std::cout << "HELLO_LISTENER" << std::endl;
                s_send(publisher, "HELLO_LISTENER");    
                continue;
            } else {
                // std::cout << std::string(static_cast<char*>(message.data()), message.size()) << std::endl;
            }
            // std::cout << "sync reply" << std::endl;
            s_send (syncservice, "");
            subscribers++;
        }
        sleep(2);

        std::cout << "Broadcast StartProcesses" << std::endl;
        s_sendmore(publisher, "StartProcesses");
        s_sendmore(publisher, std::to_string(conf[iteration].processes));
        s_send(publisher, "");

        std::cout << "Syncing processes" << std::endl;
        int processes = 0;
        while (processes < conf[iteration].machines*conf[iteration].processes) {
            zmq::message_t message;
            int ret = instance_syncservice.recv(&message);
            if (!ret) {
                // std::cout << "again...";
                // std::cout << "HELLO_SOCKET" << std::endl;
                s_send(publisher, "HELLO_SOCKET");
                continue;
            } else {
                // std::cout << std::string(static_cast<char*>(message.data()), message.size()) << std::endl;
                // std::cout << "proc sync reply" << std::endl;
                s_send (instance_syncservice, "");
                processes++;
                if (processes % 100 == 0) {
                    // std::cout << processes << " synced" << std::endl;
                }
            }            
        }

        std::cout << "Broadcasting start request" << std::endl;

        std::cout << conf[iteration].instances << " instances "
                    // << conf[iteration].send_messages
                    // << conf[iteration].randomized_interval
                    << conf[iteration].interval << " interval "
                    << conf[iteration].messages << " messages "
                    << conf[iteration].message_length << " bytes"
                    << std::endl;
        s_sendmore(publisher, "StartWebsockets");
        // s_sendmore(publisher, "ws://192.95.61.160:9002");
        // s_sendmore(publisher, "ws://localhost:9002");
        s_sendmore(publisher, "wss://54.84.134.93:9002");
        // s_sendmore(publisher, "wss://echo.websocket.org");
        s_sendmore(publisher, std::to_string(conf[iteration].instances));
        s_sendmore(publisher, std::to_string(conf[iteration].send_messages));
        s_sendmore(publisher, std::to_string(conf[iteration].randomized_interval));
        s_sendmore(publisher, std::to_string(conf[iteration].interval));
        s_sendmore(publisher, std::to_string(conf[iteration].messages));
        s_sendmore(publisher, std::to_string(conf[iteration].message_length));
        s_send(publisher, "");
        
        

        int clientsFinished = 0;
        int clientsDied = 0;
        int resultsReceived = 0;
        int resultsExpected = conf[iteration].machines*conf[iteration].processes*conf[iteration].instances;
        int connectionsDied = 0;
        int connectionsSucceeded = 0;

        // TODO: careful with overflows
        double meansum = 0.0;
        double variancesum = 0.0;
        int totalcount = 0;
        int samples = 0;
        accumulator_set<double, stats<tag::mean, tag::variance> > setup_stats;
        while (clientsFinished + clientsDied < conf[iteration].machines*conf[iteration].processes) {
            std::string status = s_recv(receiver);
            if (status == "Finished") {
                std::string name = s_recv(receiver);
                std::string exit_status = s_recv(receiver);
                if (exit_status != "0")
                    std::cout << name << " exited with status " << exit_status << std::endl;
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
                double min, max, mean, variance, setup;
                std::string type;
                ss >> type;
                
                ss >> count;
                ss >> min;
                ss >> max;
                ss >> mean;
                ss >> variance;
                ss >> setup;
                setup_stats(setup);
                if (type != "success") {
                    ++connectionsDied;
                    // std::cout << stats << std::endl;
                } else {
                    if (count == conf[iteration].messages)
                        ++connectionsSucceeded;
                    else
                        ++connectionsDied;
                }

                if (count > 0) {
                    meansum += mean*count;
                    totalcount += count;
                    samples++;
                    variancesum += (count - 1)*variance;
                }
                if (resultsExpected / 100 > 0 && resultsReceived % (resultsExpected/100) == 0){
                    // std::cout << (resultsReceived*100/resultsExpected) << "%" << std::endl;
                }
                    
                // std::cout << "Received a set of results: " << stats << std::endl;
            }
        }
        std::cout << "Iteration " << iteration << " done:" << std::endl;
        std::cout << "Clients completed " << clientsFinished << ", clients died " << clientsDied << std::endl;
        std::cout << "Connections succeeded " << connectionsSucceeded << std::endl;
        std::cout << "Connections died " << connectionsDied << std::endl;
        std::cout << "Connection setup latency " << mean(setup_stats) << "+-" << sqrt(variance(setup_stats)) << " ms" << std::endl;
        double overallMean = meansum/totalcount;
        double overallVariance = variancesum/(totalcount-samples);
        std::cout << "Average message latency " << overallMean << "+-" << sqrt(overallVariance) << " ms" << std::endl;
    }

    std::cout << "Ending" << std::endl;
    s_send (publisher, "END");
    sleep (1);              //  Give 0MQ time to flush output
}
