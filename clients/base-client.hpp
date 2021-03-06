#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/message_buffer/alloc.hpp>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/poisson_distribution.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/variate_generator.hpp>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/count.hpp>

#include <chrono>
#include <bitset>
#include <fstream>

using namespace boost::accumulators;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
using websocketpp::lib::error_code;

/**
 * The telemetry client connects to a WebSocket server and sends a message every
 * second containing an integer count. This example can be used as the basis for
 * programs where a client connects and pushes data for logging, stress/load
 * testing, etc.
 */
template <typename config>
class base_client {
public:
    typedef base_client<config> type;
    typedef websocketpp::client<config> client;
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;
    typedef websocketpp::lib::shared_ptr<type> ptr;
    typedef websocketpp::lib::shared_ptr<boost::asio::ssl::context> context_ptr;
    typedef typename client::connection_ptr connection_ptr;
    typedef typename client::message_ptr message_ptr;

    base_client(std::string const client_id, int total_messages, bool random_interval, int interval, int message_length) 
        : m_open(false)
        , m_done(false)
        , m_finite_messages(true)
        , m_total_messages(total_messages)
        , m_count(0)
        , m_random_interval(random_interval)
        , m_interval(interval)
        , m_message_pending(false)
        , m_message_length(message_length)
        , m_failed(false)
        , gen(static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count()))
    {   
        // set up access channels to only log interesting things
        client_endpoint.clear_access_channels(websocketpp::log::alevel::all);
        client_endpoint.set_access_channels(websocketpp::log::alevel::app);
        // Initialize the Asio transport policy
        client_endpoint.init_asio();

        std::string filename = "logs/" + client_id + ".log";
        m_log.open(filename, std::fstream::app);
        std::cout << client_id << "log file " << filename << std::endl;

        // Bind the handlers we are using
        client_endpoint.set_open_handler(bind(&type::on_open,this,::_1));
        client_endpoint.set_close_handler(bind(&type::on_close,this,::_1));
        client_endpoint.set_fail_handler(bind(&type::on_fail,this,::_1));
        client_endpoint.set_message_handler(bind(&type::on_message,this, ::_2));
    }

    ~base_client() {
        m_log.close();
    }
    
    // This method will block until the connection is complete
    void run(const std::string & uri) {
        // Create a new connection to the given URI
        m_log << "Connect to " << uri;
        m_log.flush();

        error_code ec;
        std::cout << "Get connection" << std::endl;
        connection_ptr con;
        con = client_endpoint.get_connection(uri, ec);
        if (ec) {
            client_endpoint.get_alog().write(websocketpp::log::alevel::app,
                    "Get Connection Error: "+ec.message());
            return;
        }
        boost::random::uniform_int_distribution<> dist(0,15);
        int delay = dist(gen);
        m_hdl = con->get_handle();
        sleep(delay);

        m_con_start = std::chrono::high_resolution_clock::now();
        client_endpoint.connect(con);
        asio_thread.reset(new thread_type(&client::run, &client_endpoint));
        telemetry_thread.reset(new thread_type(&type::telemetry_loop,this));        
    }


    void wait() {
        asio_thread->join();
        telemetry_thread->join();    
    }

    // The open handler will signal that we are ready to start sending telemetry
    void on_open(websocketpp::connection_hdl hdl) {
        client_endpoint.get_alog().write(websocketpp::log::alevel::app,
            "Connection opened, starting telemetry!");
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - m_con_start);
        m_setupTime = elapsed.count()/1000.0;
        scoped_lock guard(m_lock);
        m_log << "Connection opened in " << m_setupTime << "ms , starting telemetry!" << std::endl;
        m_open = true;
    }

    // The close handler will signal that we should stop sending telemetry
    void on_close(websocketpp::connection_hdl hdl) {
        // client_endpoint.get_alog().write(websocketpp::log::alevel::app,
        //     "Connection closed, stopping telemetry!");
        std::cout << "Closed" << std::endl;
        scoped_lock guard(m_lock);
        m_log << "Connection closed, stopping telemetry!" << std::endl;
        m_done = true;
    }

    // The fail handler will signal that we should stop sending telemetry
    void on_fail(websocketpp::connection_hdl hdl) {
        client_endpoint.get_alog().write(websocketpp::log::alevel::app,
            "Connection failed, stopping telemetry!");

        scoped_lock guard(m_lock);
        m_log << "Connection failed, stopping telemetry!" << std::endl;
        m_failed = true;
        m_done = true;
    }

    void on_message (message_ptr message) {
        scoped_lock guard(m_lock);
        m_message_pending = false;
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start);

        std::stringstream ss;
        ss << "Message echo received in " << elapsed.count() << "microseconds";
        client_endpoint.get_alog().write(websocketpp::log::alevel::app, ss.str());
        
        m_log << "Echo in " << elapsed.count()/1000.0 << "ms" << std::endl;
        m_stats(elapsed.count()/1000.0);
    }

    virtual error_code send_message (std::string const & msg) {
        error_code ec;
        client_endpoint.send(m_hdl, msg, websocketpp::frame::opcode::text, ec);
        return ec;
    }

    virtual void close(std::string const & status) {
        client_endpoint.close(m_hdl, websocketpp::close::status::normal, status);
    }
    virtual void fail(std::string const & status) {
        std::cerr << "Connection failed " << status;
    }

    void telemetry_loop () {
        std::cout << "telemetry_loop" << std::endl;
        std::stringstream val;
        error_code ec;
        
        boost::poisson_distribution<int> pdist(m_interval);
        rng_type rng(gen, pdist);

        std::uniform_int_distribution<> udist(32, 127);
        boost::variate_generator< boost::mt19937&, std::uniform_int_distribution<> > payload_rng(gen, udist);

        while(!m_finite_messages || m_count < m_total_messages ) {
            if (!m_random_interval)
                sleep(m_interval);
            else {
                int sleep_interval = rng();
                client_endpoint.get_alog().write(websocketpp::log::alevel::app, 
                    "Sleeping for " + std::to_string(sleep_interval) + "s"
                );
                sleep(sleep_interval);
            }

            bool wait = false;
            {
                scoped_lock guard(m_lock);
                // If the connection has been closed, stop generating telemetry
                if (m_done) { 
                    client_endpoint.get_alog().write(websocketpp::log::alevel::app, "Connection done, breaking");
                    break; 
                }
                // If the connection hasn't been opened yet wait a bit and retry
                if (!m_open || m_message_pending) {
                    wait = true;
                }
            }
            if (wait) {
                client_endpoint.get_alog().write(websocketpp::log::alevel::app, "connection not ready for sending");
                sleep(1);
                continue;
            }

            val.str("");
            for (int i = 0; i < m_message_length; ++i) {
                val << static_cast<char> (payload_rng());
            }
            // val.str("foo");
            // std::cout << "Will send " << val.str() << std::endl;
            m_count++;

            {
                // client_endpoint.get_alog().write(websocketpp::log::alevel::app, val.str());
                // std::cout << "Try sending " << val.str() << std::endl;
                scoped_lock guard(m_lock);
                m_message_pending = true;
                m_start = std::chrono::high_resolution_clock::now();
                ec = send_message(val.str());
            }

            // The most likely error that we will get is that the connection is
            // not in the right state. Usually this means we tried to send a
            // message to a connection that was closed or in the process of
            // closing. While many errors here can be easily recovered from,
            // in this simple example, we'll stop the telemetry loop.
            if (ec) {
                client_endpoint.get_elog().write(websocketpp::log::elevel::warn, "Send Error: " + ec.message());
                m_message_pending = false;
                // break;
            }

            
        }
        if (m_count == m_total_messages) {
            // Allow for some time to send the message
            sleep(10);
            client_endpoint.get_alog().write(websocketpp::log::alevel::app, "Close");
            close("connection finished");
        } else {
            sleep(1);
            client_endpoint.get_elog().write(websocketpp::log::elevel::fatal, "Fail");
            fail("connection failed");
        }
    }

    std::string get_stats() {
        std::stringstream ss;
        if (!m_failed) {
            ss << "success" << " ";
            ss << count(m_stats) << " ";
            ss << min(m_stats) << " ";
            ss << max(m_stats) << " ";
            ss << mean(m_stats) << " ";
            ss << variance(m_stats) << " ";
            ss << m_setupTime;    
        } else {
            ss << "fail";
            if (m_open) {
                ss << "-opened";
            }
            ss << " ";
            ss << count(m_stats) << " ";
            ss << min(m_stats) << " ";
            ss << max(m_stats) << " ";
            ss << mean(m_stats) << " ";
            ss << variance(m_stats) << " ";
            ss << m_setupTime;
        }
        return ss.str();
    }

protected:
    client client_endpoint;
    typedef websocketpp::lib::thread thread_type;
    typedef websocketpp::lib::shared_ptr<thread_type> thread_ptr;
    thread_ptr asio_thread, telemetry_thread;
    std::ofstream m_log;

private:
    typedef boost::variate_generator< boost::mt19937&, boost::poisson_distribution<> > rng_type;

    websocketpp::connection_hdl m_hdl;
    websocketpp::lib::mutex m_lock;
    bool m_open;
    bool m_done;
    bool m_finite_messages;
    int m_total_messages, m_count;
    bool m_random_interval;
    int m_interval;
    bool m_message_pending;
    int m_message_length;
    bool m_failed;
    boost::mt19937 gen;

    accumulator_set<double, stats<tag::count, tag::min, tag::max, tag::mean, tag::variance> > m_stats;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start, m_con_start;
    double m_setupTime;    
};
