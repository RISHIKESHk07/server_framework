#include "boost/asio/connect.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/ssl/context.hpp"
#include "boost/asio/ssl/stream.hpp"
#include "boost/asio/ssl/verify_mode.hpp"
#include "boost/asio/streambuf.hpp"
#include <boost/asio.hpp>
#include <iostream>
int main() {
  boost::asio::io_context io_context;
  boost::asio::ssl::context cl(boost::asio::ssl::context::tlsv12_client);
  cl.set_verify_mode(boost::asio::ssl::verify_none);

  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket(io_context, cl);

  boost::asio::ip::tcp::resolver resolver(io_context);
  auto endpoint = resolver.resolve("127.0.0.1", "8000");

  boost::asio::async_connect(
      socket.lowest_layer(), endpoint,
      [&socket](const boost::system::error_code &err, const auto &endpoints) {
        if (!err) {
          socket.async_handshake(
              boost::asio::ssl::stream_base::client,
              [&socket](const boost::system::error_code &ec) {
                if (!ec) {
                  std::cout << "handshake ... complete" << std::endl;
                  std::string HTTP_GETrequest = ```GET /test 1.1```;
                  boost::asio::streambuf writeBuf;
                  std::ostream iss(&writeBuf);
                  iss << HTTP_GETrequest;
                  boost::asio::async_write(
                      socket.lowest_layer(), writeBuf,
                      [](const boost::system::error_code &ec,
                         std::size_t bytes) {
                            if(!ec){
                             std::cout << "written succesfully" <<std::endl;
                             // start a read for the output .... 
                            }
                      });
                } else
                  std::cout << ec.message() << std::endl;
              });
        } else {
          std::cout << err.message() << std::endl;
        }
      });

  io_context.run();
  return 0;
}
