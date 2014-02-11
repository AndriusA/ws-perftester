#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/message_buffer/alloc.hpp>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/poisson_distribution.hpp>
#include <boost/random/variate_generator.hpp>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/count.hpp>

#include <chrono>

using namespace boost::accumulators;

/**
 * The telemetry client connects to a WebSocket server and sends a message every
 * second containing an integer count. This example can be used as the basis for
 * programs where a client connects and pushes data for logging, stress/load
 * testing, etc.
 */
class telemetry_client {
public:
    typedef websocketpp::client<websocketpp::config::asio_client> client;
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;
    typedef websocketpp::lib::shared_ptr<telemetry_client> ptr;

    telemetry_client(int total_messages, bool random_interval, int interval) 
        : m_open(false)
        , m_done(false)
        , m_finite_messages(true)
        , m_total_messages(total_messages)
        , m_count(0)
        , m_random_interval(random_interval)
        , m_interval(interval)
        , m_message_pending(false)
    {   
        // set up access channels to only log interesting things
        m_client.clear_access_channels(websocketpp::log::alevel::all);
        // Initialize the Asio transport policy
        m_client.init_asio();

        // Bind the handlers we are using
        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::bind;
        m_client.set_open_handler(bind(&telemetry_client::on_open,this,::_1));
        m_client.set_close_handler(bind(&telemetry_client::on_close,this,::_1));
        m_client.set_fail_handler(bind(&telemetry_client::on_fail,this,::_1));
        m_client.set_message_handler(bind(&telemetry_client::on_message,this,::_1,::_2));
    }

    // This method will block until the connection is complete
    void run(const std::string & uri) {
        // Create a new connection to the given URI
        websocketpp::lib::error_code ec;
        client::connection_ptr con = m_client.get_connection(uri, ec);
        if (ec) {
            m_client.get_alog().write(websocketpp::log::alevel::app,
                    "Get Connection Error: "+ec.message());
            return;
        }
        m_hdl = con->get_handle();
        m_client.connect(con);
        asio_thread.reset(new thread_type(&client::run, &m_client));
        telemetry_thread.reset(new thread_type(&telemetry_client::telemetry_loop,this));        
    }

    void wait() {
        asio_thread->join();
        telemetry_thread->join();
    }

    // The open handler will signal that we are ready to start sending telemetry
    void on_open(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "Connection opened, starting telemetry!");

        scoped_lock guard(m_lock);
        m_open = true;
    }

    // The close handler will signal that we should stop sending telemetry
    void on_close(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "Connection closed, stopping telemetry!");

        scoped_lock guard(m_lock);
        m_done = true;
    }

    // The fail handler will signal that we should stop sending telemetry
    void on_fail(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "Connection failed, stopping telemetry!");

        scoped_lock guard(m_lock);
        m_done = true;
    }

    void on_message(websocketpp::connection_hdl hdl, client::message_ptr message) {
        scoped_lock guard(m_lock);
        m_message_pending = false;
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start);
        std::cout << "Message echo received " << message->get_payload() << " in " << elapsed.count() << "microseconds" << std::endl;
        m_stats(elapsed.count()/1000.0);
    }

    void telemetry_loop() {
        std::stringstream val;
        websocketpp::lib::error_code ec;
        
        boost::mt19937 gen{static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count())};
        // gen.seed(std::time(NULL));
        boost::poisson_distribution<int> pdist(m_interval);
        rng_type rng(gen, pdist);

        while(!m_finite_messages || m_count < m_total_messages ) {
            if (!m_random_interval)
                sleep(m_interval);
            else {
                int sleep_interval = rng();
                sleep(sleep_interval);
            }
            bool wait = false;

            {

                scoped_lock guard(m_lock);
                // If the connection has been closed, stop generating telemetry
                if (m_done) {break;}

                // If the connection hasn't been opened yet wait a bit and retry
                if (!m_open || m_message_pending) {
                    wait = true;
                }
            }

            if (wait) {
                sleep(1);
                continue;
            }

            val.str("");
            val << "count is " << m_count++;
            val.str("this is a fixed message");

            {
                // m_client.get_alog().write(websocketpp::log::alevel::app, val.str());
                scoped_lock guard(m_lock);
                m_message_pending = true;
                m_start = std::chrono::high_resolution_clock::now();
                m_client.send(m_hdl,val.str(),websocketpp::frame::opcode::text,ec);
            }

            // The most likely error that we will get is that the connection is
            // not in the right state. Usually this means we tried to send a
            // message to a connection that was closed or in the process of
            // closing. While many errors here can be easily recovered from,
            // in this simple example, we'll stop the telemetry loop.
            if (ec) {
                m_client.get_alog().write(websocketpp::log::alevel::app,
                    "Send Error: "+ec.message());
                break;
            }

            
        }
        if (m_count == m_total_messages) {
            // Allow for some time to send the message
            sleep(1);
            m_client.close(m_hdl, websocketpp::close::status::normal, "connection finished");
        }
    }

    std::string get_stats() {
        std::stringstream ss;
        ss << count(m_stats) << " ";
        ss << min(m_stats) << " ";
        ss << max(m_stats) << " ";
        ss << mean(m_stats) << " ";
        ss << variance(m_stats);
        return ss.str();
    }
private:
    typedef websocketpp::lib::thread thread_type;
    typedef websocketpp::lib::shared_ptr<thread_type> thread_ptr;
    typedef boost::variate_generator< boost::mt19937&, boost::poisson_distribution<> > rng_type;

    client m_client;
    websocketpp::connection_hdl m_hdl;
    websocketpp::lib::mutex m_lock;
    bool m_open;
    bool m_done;
    bool m_finite_messages;
    int m_total_messages, m_count;
    bool m_random_interval;
    int m_interval;
    bool m_message_pending;

    accumulator_set<double, stats<tag::count, tag::min, tag::max, tag::mean, tag::variance> > m_stats;

    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;

    thread_ptr asio_thread, telemetry_thread;
};