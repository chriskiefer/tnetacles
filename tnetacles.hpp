#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <chrono>
#include <thread>

#define IPADDRESS "127.0.0.1" // "192.168.1.64"
#define UDP_PORT 13251
#define UDP_ADVERTISEMENT_PORT 13252
#define SERVER_ADVERT_STRING "%groundControlTo?"

using boost::asio::ip::udp;
using boost::asio::ip::address;
using boost::asio::ip::tcp;

struct tnetacles {
    
    static const unsigned int CONTROL_SERVICE_PORT = 14672;
    static const char CONTROL_SERVICE_REPLY = 174;
    static const unsigned int STREAM_TARGET_PORT = 17232;
    static const unsigned int STREAM_SOURCE_PORT = 17233;

    struct socketService {
        socketService() : socket(io_service) {}
        boost::asio::io_service io_service;
        udp::socket socket;
        udp::endpoint endpoint;
    };
    
    struct streamRequest {
        enum streamTypes {GIVE=17, RECEIVE} streamType;
        char endRequest = '\n';
        streamRequest(streamTypes T = streamTypes::RECEIVE) : streamType(T) {}
    };
    

    
    static void initAdvertisingSocket(socketService & service) {
        
//        boost::asio::io_service io_service;
//        boost::asio::ip::tcp::resolver resolver(io_service);
//        boost::asio::ip::tcp::resolver::query query(boost::asio::ip::host_name(), "");
//        boost::asio::ip::tcp::resolver::iterator it = resolver.resolve(query);
//        while (it != boost::asio::ip::tcp::resolver::iterator())
//        {
//            boost::asio::ip::address addr = (it++)->endpoint().address();
//            if (addr.is_v4()) {
//                cout << "dest:" << addr << endl;
//            }
//        }
        boost::system::error_code error;
        service.socket.open(udp::v4(), error);
        if (!error) {
            service.socket.set_option(udp::socket::reuse_address(true));
            service.socket.set_option(boost::asio::socket_base::broadcast(true));
            service.endpoint =  udp::endpoint(boost::asio::ip::address_v4::broadcast(), UDP_ADVERTISEMENT_PORT);
        }else{
            cout << "Socket Error\n";
        }
    }
    static void initUDPSocket(socketService * service, string ipaddress, unsigned int port) {
        boost::system::error_code error;
        service->socket.open(udp::v4(), error);
        if (!error) {
            boost::asio::socket_base::send_buffer_size option(2048);
            service->socket.set_option(option);
            boost::asio::socket_base::receive_buffer_size receive_option(8192);
            service->socket.set_option(receive_option);
            service->endpoint =  udp::endpoint(boost::asio::ip::address::from_string(ipaddress), port);
        }else{
            cout << "Socket Error\n";
        }
    }

    static void advertise(socketService &service) {
        boost::system::error_code err;
        auto sent = service.socket.send_to(boost::asio::buffer(SERVER_ADVERT_STRING), service.endpoint);
        std::cout << "Sent adverisment packet --- " << SERVER_ADVERT_STRING << ", " << sent << "\n";
    }
    
    template<class T>
    static void sendUDPPacket(socketService &service, T data) {
        boost::system::error_code err;
        auto sent = service.socket.send_to(boost::asio::buffer(data), service.endpoint);
//        std::cout << "Sent data packet: " << sent << "\n";
    }
    
    template<class T>
    static void sendUdpPacket(socketService &service, T buffer) {
        boost::system::error_code err;
        auto sent = service.socket.send_to(boost::asio::buffer(buffer), service.endpoint);
        std::cout << "Sent udp packet --- " << SERVER_ADVERT_STRING << ", " << sent << "\n";
    }

    static void advertiseLoop(socketService &service) {
        while(1) {
            tnetacles::advertise(service);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
    
    template<typename T, unsigned int N, unsigned int port>
    struct udpClient {
        socketService service;
        std::array<T,N> recv_buffer;
        udp::endpoint remote_endpoint;
        std::function<bool(const udp::endpoint&, std::array<T,N>&)> dataHandler;
        udpClient(std::function<bool(const udp::endpoint&, std::array<T,N>&)> f) : dataHandler(f) {}
        
        void handle_receive(const boost::system::error_code& error, size_t bytes_transferred) {
            if (error) {
                std::cout << "Receive failed: " << error.message() << "\n";
            }else if (dataHandler(remote_endpoint, recv_buffer)) {
                wait();
            }
        }
        
        void wait() {
            service.socket.async_receive_from(boost::asio::buffer(recv_buffer),
                                      remote_endpoint,
                                              boost::bind(&udpClient::handle_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
        }

        void receiver()
        {
            service.socket.open(udp::v4());

            boost::asio::socket_base::send_buffer_size option(2048);
            service.socket.set_option(option);
            boost::asio::socket_base::receive_buffer_size receive_option(8192);
            service.socket.set_option(receive_option);

            service.endpoint = udp::endpoint(boost::asio::ip::address_v4::any(), port);
            service.socket.bind(service.endpoint);
            wait();
            std::cout << "Receiving\n";
            service.io_service.run();
            std::cout << "Receiver exit\n";
        }
    };
    
    template<class T>
    static void tcpServer(const unsigned int port, function<T(boost::asio::streambuf&, string&)> onReceive) {
        try {
            boost::asio::io_service io_service;
            for (;;)
            {
                tcp::socket socket(io_service);
                tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), port));
                boost::asio::socket_base::reuse_address option(true);
                acceptor.set_option(option);
                acceptor.accept(socket);
                boost::asio::streambuf buf;
                cout << "Accepting buf\n";
                boost::asio::read_until( socket, buf, "\n" );
                cout << "Received buf\n";
                string ipaddress = socket.remote_endpoint().address().to_string();
                T response = onReceive(buf, ipaddress);
                boost::system::error_code ignored_error;
                boost::asio::write(socket, boost::asio::buffer(response),
                                   boost::asio::transfer_all(), ignored_error);
            }
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
    }
    
    template<class T>
    static void tcpClientSingleTransaction(const string host, const unsigned int port, T message) {
        try {
            boost::asio::io_service io_service;
            tcp::socket socket(io_service);
            boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(host), port);
            socket.connect(endpoint);

            std::array<char, 20> inputBuf;
            vector<T> reqBuffer(1, message);
            auto buf = boost::asio::buffer(reqBuffer);
            boost::system::error_code error;
            socket.write_some(buf, error);

            for (;;)
            {
                boost::system::error_code error;
                size_t len = socket.read_some(boost::asio::buffer(inputBuf), error);
                if (error == boost::asio::error::eof) {
                    cout << "connection closed\n";
                    break; // Connection closed cleanly by peer.
                }
                else if (error)
                    throw boost::system::system_error(error); // Some other error.
                std::cout.write(inputBuf.data(), len);
            }
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
    }
    
    
};

