#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/error.hpp>
#include <websocketpp/extensions/mobile_signaling/enabled.hpp>
#include "base-client.hpp"

struct mobile_signaling_conf : public websocketpp::config::asio_client {
    typedef mobile_signaling_conf type;

    /// mobile_signaling extension
    struct m_s_conf : public type::mobile_signaling_config {
        static const websocketpp::uri coordinator() {
            static websocketpp::uri ret("ws://ec2-54-81-216-95.compute-1.amazonaws.com:9000");
            return ret;
        };
        static const websocketpp::uri destination() {
            static websocketpp::uri ret("ws://ec2-54-82-55-179.compute-1.amazonaws.com:9002");
            return ret;
        };
    };

    typedef websocketpp::extensions::mobile_signaling::enabled
        <m_s_conf> mobile_signaling_type;
};


template <typename config>
class mobile_signaling_client : public base_client<mobile_signaling_conf>  {
public:
    static std::string client_id;

    typedef websocketpp::lib::function<void(message_ptr)> message_handler;

    mobile_signaling_client (std::string const client_id, int total_messages, bool random_interval, int interval, int message_length) 
        : base_client(client_id, total_messages, random_interval, interval, message_length)
        , m_client_id(client_id)
    {
        // Start perpetual to avoid closig it when there is no more work remaining
        client_endpoint.start_perpetual();

        // Bind the handlers we are using
        client_endpoint.set_close_handler(bind(&mobile_signaling_client::on_close_primary,this,::_1));
        client_endpoint.set_fail_handler(bind(&mobile_signaling_client::on_fail,this,::_1));
        client_endpoint.set_message_handler(bind(&mobile_signaling_client::on_message_primary, this, ::_1, ::_2));

        // Calling on_message of the parent
        message_handler msg_hdl = bind(&mobile_signaling_client::on_message, this, ::_1);
        set_message_handler(msg_hdl);
    }

    static void generate_client_id () {
        std::random_device rd;
        std::mt19937 gen(rd());
        gen.seed(std::time(NULL));
        std::uniform_int_distribution<unsigned long> dis(0, ULONG_MAX);
        int id_length_bytes = 16;
        int longsize = sizeof(unsigned long);
        unsigned char client_id[id_length_bytes];
        for (int i = 0; i < id_length_bytes; i += longsize) {
            unsigned long val = dis(gen);
            for (int j = 0; j < longsize; j++) {
                client_id[i+j] = static_cast<char>((val >> (4*j)) & 0xFF);
            }
        }
        client_id = websocketpp::base64_encode(client_id, 16);
    }

    // This method will block until the connection is complete
    // Override that of the parent with custom logic for the dual links
    void run (const std::string & uri) {
        // The algorithm:
        // 1. Set desination to uri, take coordinator from the config
        // 2. Set up primary connection to uri
        // 3. Once the connection is established (so, fron on_connect handler) set up connection
        //    to coordinator, destination pointing to uri
        m_log << "Connect to " << uri;
        m_log.flush();

        std::string connectUri = config::m_s_conf::destination().str();
        std::cout << "Connect primary " << connectUri;
        connect_primary(connectUri);

        asio_thread.reset(new thread_type(&client::run, &client_endpoint));
        telemetry_thread.reset(new thread_type(&type::telemetry_loop,this));
    }

    void set_message_handler (message_handler h) {
        m_on_message_handler = h;
    }

    void close(std::string const & status) {
        // TODO
        std::cout << "mobile close " << status << std::endl;
        client_endpoint.close(m_hdl_primary, websocketpp::close::status::normal, status);
        client_endpoint.close(m_hdl_signaling, websocketpp::close::status::normal, status);
        client_endpoint.stop_perpetual();
    }
    void fail(std::string const & status) {
        std::cerr << "Mobile connection failed " << status;
    }

    error_code send_message (std::string const & msg) {
        // std::cout << "Sending message " << msg << std::endl;
        // If we have a primary link for the connection
        error_code ec;

        connection_ptr signaling_con = client_endpoint.get_con_from_hdl(m_hdl_signaling, ec);
        if (!ec && signaling_con->get_state() == websocketpp::session::state::open) {
            std::cout << "sending message to signaling" << std::endl;
            ec = signaling_con->send(msg, websocketpp::frame::opcode::text);
            if (!ec)
                return ec;
        } 

        connection_ptr primary_con = client_endpoint.get_con_from_hdl(m_hdl_primary, ec);
        if (!ec && primary_con->get_state() == websocketpp::session::state::open) {
            std::cout << "sending message to primary" << std::endl;
            ec = primary_con->send(msg, websocketpp::frame::opcode::text);
            if (!ec)
                return ec;
        }
        
        return ec;
    }

private:
    websocketpp::connection_hdl m_hdl_primary, m_hdl_signaling;
    std::string m_client_id;
    
    message_handler m_on_message_handler;


    std::string get_response_connection_id(websocketpp::http::parser::response const & resp) {
        websocketpp::http::parameter_list extensions;
        resp.get_header_as_plist("Sec-WebSocket-Extensions", extensions);
        std::string connection_id;
        for (auto it = extensions.begin(); it != extensions.end(); it ++) {
            if (it->first == "mobile-signaling") {
                websocketpp::http::attribute_list a = it->second;
                if (a.find("connection_id") != a.end()) {
                    connection_id = a.find("connection_id")->second;
                }
            }
        }
        return connection_id;
    }

    void connect_primary(std::string const & uri) {
        websocketpp::lib::error_code ec;
        connection_ptr con = client_endpoint.get_connection(uri, ec);
        if (ec) {
            client_endpoint.get_alog().write(websocketpp::log::alevel::app,
                    "Get Connection Error: "+ec.message());
            return;
        }
        m_hdl_primary = con->get_handle();

        // Add client_id as part of connection_id
        con->replace_header("Sec-WebSocket-Extensions", "mobile-signaling; connection_id=\""+m_client_id+"\"");
        con->set_open_handler(bind(&mobile_signaling_client::on_open_primary,this,::_1));

        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        client_endpoint.connect(con);
        std::cout << "Return client pointer" << std::endl;
    }

    void connect_signaling(std::string const & uri, std::string connection_id) {
        websocketpp::lib::error_code ec;
        connection_ptr con = client_endpoint.get_connection(uri, ec);
        if (ec) {
            std::cerr << "Get Connection Error: "+ec.message() << std::endl;
            return;
        }
        m_hdl_signaling = con->get_handle();

        con->replace_header("Sec-WebSocket-Extensions", "mobile-signaling; connection_id=\""+connection_id+"\"");

        // Use parent-declared on_open
        con->set_open_handler(bind(&mobile_signaling_client::on_open, this, ::_1));

        con->set_close_handler(bind(&mobile_signaling_client::on_close_signaling, this, ::_1));
        con->set_message_handler(bind(&mobile_signaling_client::on_message_signaling, this, ::_1, ::_2));
        // Queue the connection. No DNS queries or network connections will be
        // made until the io_service event loop is run.
        client_endpoint.connect(con);
    }

    // The open handler will signal that we are ready to start sending telemetry
    void on_open_primary(websocketpp::connection_hdl hdl) {
        std::cout << "Primary connection open, opening secondary" << std::endl;
        std::string connectUri = config::m_s_conf::coordinator().str();
        m_hdl_primary = hdl;

        // Use the same connection_id generated on establishing primary
        connection_ptr con = client_endpoint.get_con_from_hdl(hdl);
        websocketpp::http::parser::response resp = con->get_response();
        std::string connection_id = get_response_connection_id(resp);
        connect_signaling(config::m_s_conf::coordinator().str(), connection_id);
    }

    // The close handler will signal that we should stop sending telemetry
    void on_close_primary(websocketpp::connection_hdl hdl) {
        std::cout << "Primary connection closed!" << std::endl;
        // TODO: if signaling alive, schedule reconnect of primary
        on_close(hdl);
    }
    // The close handler will signal that we should stop sending telemetry
    void on_close_signaling(websocketpp::connection_hdl hdl) {
        // std::cout << "Signaling connection closed!" << std::endl;
        // TODO: if signaling alive, schedule reconnect of primary
        on_close(hdl);
    }

    // The fail handler will signal that we should stop sending telemetry
    //void on_fail(websocketpp::connection_hdl hdl) {
        // std::cout << "Connection failed!" << std::endl;
        // TODO: if signaling alive, schedule reconnect of primary
    //}

    void on_message_primary(websocketpp::connection_hdl hdl, message_ptr msg) {
        // std::cout << "received primary " << msg->get_payload() << " ext " << msg->get_extension_data() << std::endl;
        if (m_on_message_handler) {
            m_on_message_handler(msg);
        }
    }

    void on_message_signaling(websocketpp::connection_hdl hdl, message_ptr msg) {
        // std::cout << "received signaling " << msg->get_payload() << " ext " << msg->get_extension_data() << std::endl;
        if (m_on_message_handler) {
            m_on_message_handler(msg);
        }
    }

    // void simple_message_handler(std::string msg) {
    //     std::cout << "received message " << msg << std::endl;
    // }
};
