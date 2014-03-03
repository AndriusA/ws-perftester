#include <websocketpp/config/asio_no_tls_client.hpp>
#include "base-client.hpp"

class telemetry_client : public base_client<websocketpp::config::asio_client>  {
public:
    telemetry_client(std::string const client_id, int total_messages, bool random_interval, int interval, int message_length) 
        : base_client(client_id, total_messages, random_interval, interval, message_length)
    {
        client_endpoint.set_socket_init_handler(bind(&telemetry_client::on_socket_init, this, ::_1, ::_2));
    }

    void on_socket_init(websocketpp::connection_hdl hdl, boost::asio::ip::tcp::socket & socket) {
        boost::asio::ip::tcp::no_delay option(true);
        boost::asio::socket_base::receive_buffer_size buf_option;
        socket.lowest_layer().set_option(option);
        socket.lowest_layer().get_option(buf_option);
        // int size = buf_option.value();
        // std::cout << "no_delay true" << std::endl;
        // std::cout << "buffer size " << size << std::endl; 
    }
};