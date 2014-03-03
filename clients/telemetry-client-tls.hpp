#include <websocketpp/config/asio_client.hpp>
#include "base-client.hpp"
// #include <websocketpp/config/asio_client.hpp>

class telemetry_client : public base_client<websocketpp::config::asio_tls_client> {
public:
    telemetry_client(std::string const client_id, int total_messages, bool random_interval, int interval, int message_length) 
        : base_client(client_id, total_messages, random_interval, interval, message_length)
    {
        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::bind;
        client_endpoint.set_tls_init_handler(bind(&telemetry_client::on_tls_init,this,::_1));
        client_endpoint.set_socket_init_handler(bind(&telemetry_client::on_socket_init, this, ::_1, ::_2));
    }

    context_ptr on_tls_init(websocketpp::connection_hdl hdl) {
        context_ptr ctx(new boost::asio::ssl::context(boost::asio::ssl::context::tlsv1));

        try {
            ctx->set_options(boost::asio::ssl::context::default_workarounds |
                             boost::asio::ssl::context::no_sslv2 |
                             boost::asio::ssl::context::single_dh_use );
                             // | SSL_OP_NO_COMPRESSION
                             // | SSL_MODE_RELEASE_BUFFERS );

            // boost::asio::ssl::context::native_handle_type native_ctx = ctx->native_handle();
            // long mode = SSL_CTX_get_mode(native_ctx);
            // mode = SSL_CTX_set_mode(native_ctx, mode | SSL_MODE_RELEASE_BUFFERS);
            // std::bitset<32> m(mode);
            // std::cout << "SSL mode " << m << std::endl;
        } catch (std::exception& e) {
            std::cout << e.what() << std::endl;
        }
        return ctx;
    }

    void on_socket_init(websocketpp::connection_hdl hdl, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket) {
        boost::asio::ip::tcp::no_delay option(true);
        boost::asio::socket_base::receive_buffer_size buf_option;
        socket.lowest_layer().set_option(option);
        socket.lowest_layer().get_option(buf_option);
        // int size = buf_option.value();
        // std::cout << "no_delay true" << std::endl;
        // std::cout << "buffer size " << size << std::endl; 
    }


};
