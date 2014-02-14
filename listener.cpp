//
//  Synchronized subscriber in C++
//
// Olivier Chamoux <olivier.chamoux@fr.thalesgroup.com>

#include "zhelpers.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/process.hpp> 
#include <string> 
#include <vector>
#include <chrono> 

namespace bp = ::boost::process;

bp::child start_child(std::string procName, std::string coordinator) { 
    std::string exec = "./listener-websocket"; 

    std::vector<std::string> args; 
    args.push_back("listener-websocket");
    args.push_back(procName); 
    args.push_back(coordinator);

    bp::context ctx; 
    // ctx.stdout_behavior = bp::capture_stream(); 
    ctx.stdout_behavior = bp::silence_stream(); 

    return bp::launch(exec, args, ctx); 
} 

int main (int argc, char *argv[])
{
    zmq::context_t context(1);
    std::string listenerName;
    srand (static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    listenerName = "Client" + std::to_string(rand()%10000+1);
    // std::cout << listenerName << " Starting client " << std::endl;
    std::string address = "localhost";
    if (argc >= 2)
        address = argv[1];
    else
        std::cout << "WARNING: need to specify coordinator address" << std::endl;

    std::string subscriber_addr = "tcp://"+address+":5561";
    std::string syncclient_addr = "tcp://"+address+":5562";
    std::string sink_addr = "tcp://"+address+":5558";
    //  First, connect our subscriber socket
    zmq::socket_t subscriber (context, ZMQ_SUB);
    subscriber.connect(subscriber_addr.c_str());
    subscriber.setsockopt(ZMQ_SUBSCRIBE, "StartProcesses", strlen("StartProcesses"));
    subscriber.setsockopt(ZMQ_SUBSCRIBE, "END", strlen("END"));

    //  Second, synchronize with publisher
    zmq::socket_t syncclient (context, ZMQ_REQ);
    syncclient.connect(syncclient_addr.c_str());

    //  Finally, Sink for results
    zmq::socket_t sink(context, ZMQ_PUSH);
    sink.connect(sink_addr.c_str());
    
    while (1) {
        std::cout << listenerName << " Sending synchronization request" << std::endl;
        s_send (syncclient, "");
        s_recv (syncclient);

        std::string processingRequest = s_recv(subscriber);
        std::cout << listenerName << " processing request " <<  processingRequest << std::endl;
        if (processingRequest == "END") {
            break;
        } else if (processingRequest == "StartProcesses") {
            int processes;
            std::stringstream req(s_recv(subscriber));
            s_recv(subscriber);
            req >> processes;
            std::vector<bp::child> children;
            std::cout << listenerName << "Launching " << processes << " processes" << std::endl;
            // Start up the required number of processes
            for (int i = 0; i < processes; i++) {
                std::string cname = listenerName + "-" + std::to_string(i);
                bp::child c = start_child(cname, address);
                children.push_back(c);    
            }
            
            // for (int i = 0; i < processes; i++) {
            //     bp::pistream &is = children[i].get_stdout(); 
            //     std::string line; 
            //     while (std::getline(is, line)) 
            //         std::cout << line << std::endl; 
            // }

            // Wait for all of them to finish
            for (int i = 0; i < processes; i++) {
                bp::status s = children[i].wait();
                int status = s.exited() ? s.exit_status() : EXIT_FAILURE;
                // std::cout << listenerName << " Child status " << status << std::endl;
                std::string cname = listenerName + "-" + std::to_string(i);
                s_sendmore(sink, "Finished");
                s_sendmore(sink, cname);
                s_sendmore(sink, std::to_string(status));
                s_send(sink, "");
            }
        }
    }
    
    // std::cout << listenerName << " Running children done" << std::endl;
    return 0;
}