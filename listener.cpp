#include "zhelpers.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/asio.hpp> 
#include <boost/process.hpp> 
#include <boost/array.hpp> 
#include <boost/bind.hpp> 
#include <string> 
#include <vector>
#include <chrono>

namespace bp = ::boost::process;
namespace ba = ::boost::asio; 

ba::io_service io_service; 
boost::array<char, 4096> buffer; 

#if defined(BOOST_POSIX_API) 
ba::posix::stream_descriptor in(io_service); 
#elif defined(BOOST_WINDOWS_API) 
ba::windows::stream_handle in(io_service); 
#else 
#  error "Unsupported platform." 
#endif 

bp::child start_child(std::string procName, std::string coordinator) { 
    std::string exec = "./listener-websocket"; 

    std::vector<std::string> args; 
    args.push_back("listener-websocket");
    args.push_back(procName); 
    args.push_back(coordinator);

    bp::context ctx; 
    //ctx.stdout_behavior = bp::capture_stream(); 
    ctx.stdout_behavior = bp::silence_stream(); 

    return bp::launch(exec, args, ctx); 
} 

void end_read(const boost::system::error_code &ec, std::size_t bytes_transferred); 

void begin_read() 
{ 
    in.async_read_some(boost::asio::buffer(buffer), 
    boost::bind(&end_read, ba::placeholders::error, ba::placeholders::bytes_transferred)); 
} 

void end_read(const boost::system::error_code &ec, std::size_t bytes_transferred) 
{ 
    if (!ec) { 
        std::cout << std::string(buffer.data(), bytes_transferred) << std::flush; 
        begin_read(); 
    } 
} 

int main (int argc, char *argv[])
{
    std::cout<< "Listener version 5" << std::endl;
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
    std::string sink_addr = "tcp://"+address+":5564";
    //  First, connect our subscriber socket
    zmq::socket_t subscriber (context, ZMQ_SUB);
    subscriber.connect(subscriber_addr.c_str());
    subscriber.setsockopt(ZMQ_SUBSCRIBE, "StartProcesses", strlen("StartProcesses"));
    subscriber.setsockopt(ZMQ_SUBSCRIBE, "END", strlen("END"));
    subscriber.setsockopt(ZMQ_SUBSCRIBE, "HELLO_LISTENER", strlen("HELLO_LISTENER"));

    //  Second, synchronize with publisher
    zmq::socket_t syncclient (context, ZMQ_REQ);
    syncclient.connect(syncclient_addr.c_str());

    //  Finally, Sink for results
    zmq::socket_t sink(context, ZMQ_PUSH);
    sink.connect(sink_addr.c_str());
    
    while (1) {
        std::string msg = s_recv(subscriber);
        if (msg == "HELLO_LISTENER") {
            std::cout << listenerName << " Sending synchronization request" << std::endl;
            s_send (syncclient, "");
            s_recv (syncclient);
            while (1) {
                std::string processingRequest = s_recv(subscriber);
                std::cout << listenerName << " processing request " <<  processingRequest << std::endl;
                if (processingRequest == "END") {
                    std::cout << "Finishing" << std::endl;
                    return 0;
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
                    //     in.assign(is.handle().release()); 
                    //     begin_read();
                    // }

                    // Wait for all of them to finish
                    std::cout << "Waiting for the processes to finish";
                    int unfinished = processes;
                    while (unfinished > 0) {
                    //for (int i = 0; i < processes; i++) {
                        //std::string cname = listenerName + "-" + std::to_string(i);
                        //pid_t pid = children[i].get_id();
                        //std::cout << "child " << i << " id " << pid << std::endl;
                        
                        int status;
                        pid_t result = waitpid(0, &status, WNOHANG);
                        if (result == 0) {
                            //std::cout << " all still running " << result << std::endl; 
                            sleep(10);
                        } else if (result == -1) {
                            --unfinished;
                            std::cout << listenerName << " error..." << status << std::endl;
                            s_sendmore(sink, "Finished");
                            s_sendmore(sink, "");
                            s_sendmore(sink, std::to_string(status));
                            s_send(sink, "");
                        } else {
                            --unfinished;
                            std::cout << listenerName << " " << result << " child exited " << status << std::endl;
                            s_sendmore(sink, "Finished");
                            s_sendmore(sink, "");
                            s_sendmore(sink, std::to_string(status));
                            s_send(sink, "");
                        }

                        //bp::status s = children[i].wait();
                        //int status = s.exited() ? s.exit_status() : EXIT_FAILURE;
                        //std::cout << listenerName << " Child status " << status << std::endl;
                    }
                }
            }

        } else {
            //std::cout << "Missing hello message" << std::endl;
        }
    }
    
    // std::cout << listenerName << " Running children done" << std::endl;
    return 0;
}
