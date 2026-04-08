#include "boost/asio/completion_condition.hpp"
#include "boost/asio/connect.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/ssl/context.hpp"
#include "boost/asio/ssl/stream.hpp"
#include "boost/asio/ssl/verify_mode.hpp"
#include "boost/asio/streambuf.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <string_view>
int main(int argc, char *argv[]) {
  boost::asio::io_context io_context;
  boost::asio::ssl::context cl(boost::asio::ssl::context::tlsv12_client);
  cl.set_verify_mode(boost::asio::ssl::verify_none);

  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket(io_context, cl);

  boost::asio::ip::tcp::resolver resolver(io_context);
  auto endpoint = resolver.resolve("127.0.0.1", "8000");

  boost::asio::async_connect(
      socket.lowest_layer(), endpoint,
      [&socket, argv](const boost::system::error_code &err,
                      const auto &endpoints) {
        if (!err) {
          socket.async_handshake(
              boost::asio::ssl::stream_base::client,
              [&socket, argv](const boost::system::error_code &ec) {
                if (!ec) {
                  std::cout << "handshake ... complete" << std::endl;
                  std::string_view cmd = argv[1];

                  if (cmd == "g") {
                    std::string http_getrequest =
                        "GET /test http/1.1\r\n"
                        "host: example.com\r\n"
                        "user-agent: c++client/1.0\r\n"
                        "accept: application/json\r\n"
                        "connection: close\r\n"
                        "\r\n\r\n";
                    boost::asio::streambuf writebuf;
                    boost::asio::streambuf reader;
                    std::ostream iss(&writebuf);
                    iss << http_getrequest;
                    boost::asio::async_write(
                        socket, writebuf,
                        [&socket, &reader,
                         &writebuf](const boost::system::error_code &ec,
                                    std::size_t bytes) {
                          if (!ec) {
                            std::cout << "written succesfully" << std::endl;

                            // boost::asio::async_read(
                            //     socket, reader,
                            //     boost::asio::transfer_at_least(1),
                            //     [&reader, &writebuf](
                            //         const boost::system::error_code &err,
                            //         size_t bytes) {
                            //       if (!err) {
                            //         auto bufs = reader.data();
                            //         const char *data =
                            //             static_cast<const char
                            //             *>(bufs.data());
                            //         std::string rs(data, bufs.size());
                            //         std::cout << rs << std::endl;
                            //       }
                            //     });
                          }
                        });
                  } else if (cmd == "te") {
                    // std::string http_te_request =
                    //     "POST /test HTTP/1.1\r\n"
                    //     "Host: localhost\r\n"
                    //     "Transfer-Encoding: chunked\r\n"
                    //     "Content-Type: application/octet-stream\r\n"
                    //     "Connection: close\r\n"
                    //     "\r\n\r\n"
                    //     "B\r\n" // Hex for 11 bytes
                    //     "First Chunk\r\n"
                    //     "C\r\n" // Hex for 12 bytes
                    //     "Second Chunk\r\n"
                    //     "17\r\n" // Hex for 23 bytes
                    //     "Final data segment here\r\n"
                    //     "0\r\n"     // The "Last Chunk"
                    //     "\r\n\r\n"; // Final CRLF to end the request
                    std::string large_chunked_request =
                        "POST /stream HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Transfer-Encoding: chunked\r\n\r\n";

                    for (int i = 0; i < 100; ++i) {
                      std::string data = "Data segment " + std::to_string(i) +
                                         " followed by some padding...";
                      std::stringstream ss;
                      ss << std::hex << data.length() << "\r\n"
                         << data << "\r\n";
                      large_chunked_request += ss.str();
                    }
                    large_chunked_request += "0\r\n\r\n";
                    auto writebuf = std::make_shared<boost::asio::streambuf>();
                    auto reader = std::make_shared<boost::asio::streambuf>();
                    std::ostream iss(writebuf.get());
                    iss << large_chunked_request;
                    boost::asio::async_write(
                        socket, *writebuf,
                        [&socket, reader,
                         writebuf](const boost::system::error_code &ec,
                                   std::size_t bytes) {
                          if (!ec) {
                            std::cout << "TE written succesfully" << std::endl;

                            boost::asio::async_read(
                                socket, *reader,
                                boost::asio::transfer_at_least(1),
                                [reader](const boost::system::error_code &err,
                                         size_t bytes) {
                                  if (!err) {
                                    auto bufs = reader->data();
                                    const char *data =
                                        static_cast<const char *>(bufs.data());
                                    std::string rs(data, bufs.size());
                                    std::cout << rs << std::endl;
                                  }
                                });
                          }
                        });
                  }
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
