#include "boost/asio/buffer.hpp"
#include "boost/asio/completion_condition.hpp"
#include "boost/asio/connect.hpp"
#include "boost/asio/detail/chrono.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/read.hpp"
#include "boost/asio/registered_buffer.hpp"
#include "boost/asio/ssl/context.hpp"
#include "boost/asio/ssl/stream.hpp"
#include "boost/asio/ssl/verify_mode.hpp"
#include "boost/asio/steady_timer.hpp"
#include "boost/asio/streambuf.hpp"
#include "boost/system/detail/error_code.hpp"
#include <boost/asio.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ostream>
#include <string_view>
#include <sys/socket.h>

class Wss_client {
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &socket;

public:
  Wss_client(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &soc)
      : socket(soc) {};

  void connect(){

    std::cout << "WSS testing" << std::endl;
                    auto writebuf = std::make_shared<boost::asio::streambuf>();
                    auto readbuf = std::make_shared<boost::asio::streambuf>();
                    std::ostream os(writebuf.get());

                    // 1. WebSocket Upgrade Request
                    os << "GET /test HTTP/1.1\r\n"
                       << "Host: 127.0.0.1:8000\r\n"
                       << "Upgrade: websocket\r\n"
                       << "Connection: Upgrade\r\n"
                       << "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                       << "Sec-WebSocket-Protocol: multiplex-v1\r\n"
                       << "Sec-WebSocket-Version: 13\r\n\r\n";

                    boost::asio::async_write(
                        socket, *writebuf,
                        [&, readbuf, writebuf](
                            const boost::system::error_code &ec, std::size_t) {
                          if (ec)
                            return;

                          // 2. Wait for 101 Switching Protocols
                          std::cout << "waiting switching protocols "
                                       "confirmation from server .."
                                    << std::endl;
                          boost::asio::async_read_until(
                              socket, *readbuf, "\r\n\r\n",
                              [&, readbuf,
                               writebuf](const boost::system::error_code &ec,
                                         std::size_t bytes) {
                                if (ec)
                                  return;

                                std::string resp(
                                    boost::asio::buffers_begin(readbuf->data()),
                                    boost::asio::buffers_begin(
                                        readbuf->data()) +
                                        bytes);
                                std::cout << "Handshake Response:\n"
                                          << resp << std::endl;

                                if (resp.find("101 Switching Protocols") !=
                                    std::string::npos) {
                                    auto timer = std::make_shared<boost::asio::steady_timer>(this->socket.get_executor(),boost::asio::chrono::seconds(45)); 
                                    read_from_linfr(timer);

                                }
                              });
                        });

  }


  struct wss_payload {
    int STREAM_ID;
    std::string payload;
  };
  struct wss_frame {
    int FIN;  // 1bit
    int RSV1; // 1 bit
    int RSV2;
    int RSV3;
    int OPCODE;          // 4 bits
    int MASK;            // 1 bit
    int LENGTH;          // 7 bits divide into 125 chunks remaining 126 ,127
                         // for extended version frame length
    int EXTENDED_LENGTH; // 16 unsigned bit integer as frame length
                         // ...
    struct wss_payload py;
  };
  struct ReadState {
    std::vector<wss_frame> current_message_frames;
    uint8_t header[2];
  };

  void create_frame(std::string_view data, uint32_t stream_id, bool fin,
                    uint8_t opcode, std::ostream &os) {
    // Byte 0: FIN | RSV | Opcode (0x02 for binary)
    uint8_t byte0 = (fin ? 0x80 : 0x00) | opcode & 0x02;
    os.put(static_cast<char>(byte0));

    // Byte 1: Mask(0) | Length(126 for 16-bit extended)
    os.put(static_cast<char>(126));

    // Extended Length: payload size + 4 bytes for StreamID
    uint16_t ext_len = htons(static_cast<uint16_t>(data.size() + 4));
    os.write(reinterpret_cast<const char *>(&ext_len), 2);

    // Stream ID (4 bytes)
    uint32_t net_sid = htonl(stream_id);
    os.write(reinterpret_cast<const char *>(&net_sid), 4);

    // Payload
    std::cout << byte0 << "--" << ext_len << std::endl;
    os.write(data.data(), data.size());
  };

  void send_message(std::shared_ptr<boost::asio::streambuf> write_buf) {
    boost::asio::async_write(
        this->socket, *write_buf,
        [](const boost::system::error_code &ec, std::size_t bytes) {
          if (!ec) {
            std::cout << "Sent message ..." << std::endl;
          }
        });
  }
void read_from_linfr(std::shared_ptr<boost::asio::steady_timer> timer) {
    auto state = std::make_shared<ReadState>();

    // Recursive lambda to process subsequent frames within the same message state
    auto read_next_frame = std::make_shared<std::function<void()>>();
    if(timer->expiry() <= boost::asio::steady_timer::clock_type::now()){
      std::cout << "Something is wrong with connection ig here .... " << std::endl;
      return;
    }
    *read_next_frame = [this, state, timer, read_next_frame]() {
        boost::asio::async_read(
            socket, boost::asio::buffer(state->header, 2),
            [this, state, timer, read_next_frame](const boost::system::error_code &ec, std::size_t) {
                if (!ec) {
                    // Reset timer: Extend deadline by 45s from current expiry
                    timer->expires_at(timer->expiry() + boost::asio::chrono::seconds(45));
                    
                    wss_frame frame;
                    frame.OPCODE = state->header[0] & 0x0F;
                    frame.FIN = (state->header[0] >> 7) & 0x01;

                    auto ext_len_buf = std::make_shared<uint16_t>();
                    boost::asio::async_read(
                        socket, boost::asio::buffer(ext_len_buf.get(), 2),
                        [this, frame, ext_len_buf, state, timer, read_next_frame](const boost::system::error_code &ec, std::size_t) mutable {
                            if (ec) return;
                            frame.EXTENDED_LENGTH = ntohs(*ext_len_buf);

                            auto full_payload = std::make_shared<std::vector<uint8_t>>(frame.EXTENDED_LENGTH);
                            boost::asio::async_read(
                                socket, boost::asio::buffer(full_payload->data(), full_payload->size()),
                                [this, frame, full_payload, state, timer, read_next_frame](const boost::system::error_code &ec, std::size_t) mutable {
                                    if (ec) return;

                                    uint32_t net_sid;
                                    std::memcpy(&net_sid, full_payload->data(), 4);
                                    frame.py.STREAM_ID = ntohl(net_sid);
                                    frame.py.payload.assign(reinterpret_cast<char *>(full_payload->data() + 4), full_payload->size() - 4);

                                    state->current_message_frames.push_back(std::move(frame));

                                    // Handle Ping (Opcode 0x09)
                                    if (state->current_message_frames.back().OPCODE == 0x09) {
                                        auto write_buff = std::make_shared<boost::asio::streambuf>();
                                        std::ostream os(write_buff.get());
                                        create_frame("", 1234, 1, 0x0A, os); // Send Pong
                                        this->send_message(write_buff);
                                    }

                                    // Check if message is complete
                                    if (state->current_message_frames.back().FIN == 1) {
                                        std::string final_msg;
                                        for (auto &f : state->current_message_frames) {
                                            final_msg += f.py.payload;
                                        }
                                        std::cout << "Message complete: " << final_msg << std::endl;
                                        
                                        // Start fresh with a new state for the next message
                                        this->read_from_linfr(timer);
                                    } else {
                                        // Recursively read the next frame using the SAME state
                                        (*read_next_frame)();
                                    }
                                });
                        });
                } else if (ec != boost::asio::error::operation_aborted) {
                    std::cerr << "Read error: " << ec.message() << std::endl;
                }
            });
    };

    // Kick off the first read
    boost::asio::post(socket.get_executor(), [read_next_frame]() {
        (*read_next_frame)();
    });
}

  void send_wss_binary_frames() {
    auto writebuf = std::make_shared<boost::asio::streambuf>();
    auto read_buf = std::make_shared<boost::asio::streambuf>();
    std::ostream os(writebuf.get());

    auto create_frame = [&](std::string_view data, uint32_t stream_id, bool fin,
                            uint8_t opcode) {
      // Byte 0: FIN | RSV | Opcode (0x02 for binary)
      uint8_t byte0 = (fin ? 0x80 : 0x00) | opcode & 0x02;
      os.put(static_cast<char>(byte0));

      // Byte 1: Mask(0) | Length(126 for 16-bit extended)
      os.put(static_cast<char>(126));

      // Extended Length: payload size + 4 bytes for StreamID
      uint16_t ext_len = htons(static_cast<uint16_t>(data.size() + 4));
      os.write(reinterpret_cast<const char *>(&ext_len), 2);

      // Stream ID (4 bytes)
      uint32_t net_sid = htonl(stream_id);
      os.write(reinterpret_cast<const char *>(&net_sid), 4);

      // Payload
      std::cout << byte0 << "--" << ext_len << std::endl;
      os.write(data.data(), data.size());
    };

    // Frame 1: Partial data (FIN = 0)
    create_frame("Stream part 1: Init", 1234, false, 0x02);
    // Frame 2: Final data (FIN = 1)
    create_frame("Stream part 2: End", 1234, true, 0x02);

    create_frame("", 1234, true, 0x08);

    this->send_message(writebuf);
  };
};


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
                  std::cout << "tls handshake ... complete" << std::endl;
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
                  } else if (cmd == "wss") {
                     auto wss_client = Wss_client(socket);
                     wss_client.connect();
                     wss_client.send_wss_binary_frames();
                  }
                }

                else
                  std::cout << ec.message() << std::endl;
              });
        } else {
          std::cout << err.message() << std::endl;
        }
      });

  io_context.run();
  return 0;
}
