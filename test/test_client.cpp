#include "boost/asio/buffer.hpp"
#include "boost/asio/completion_condition.hpp"
#include "boost/asio/connect.hpp"
#include "boost/asio/detail/chrono.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/read.hpp"
#include "boost/asio/ssl/context.hpp"
#include "boost/asio/ssl/stream.hpp"
#include "boost/asio/ssl/verify_mode.hpp"
#include "boost/asio/steady_timer.hpp"
#include "boost/asio/streambuf.hpp"
#include "boost/system/detail/error_code.hpp"
#include <boost/asio.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <ostream>
#include <string_view>
#include <sys/socket.h>

class Wss_client : public std::enable_shared_from_this<Wss_client> {
  std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
      socket;
  std::shared_ptr<int> message_counter_client;
  struct wss_payload {
    int STREAM_ID;
    int FRAME_ID;
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

  std::shared_ptr<std::deque<wss_frame>> Sender_DQ;
  bool debug_replay = false;

public:
  Wss_client(
      std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
          soc)
      : socket(soc) {
    message_counter_client = std::make_shared<int>();
    Sender_DQ = std::make_shared<std::deque<wss_frame>>();
  };

  void connect() {

    auto self = shared_from_this();

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
        *socket, *writebuf,
        [self, readbuf, writebuf](const boost::system::error_code &ec,
                                  std::size_t) {
          if (ec)
            return;

          // 2. Wait for 101 Switching Protocols
          std::cout << "waiting switching protocols "
                       "confirmation from server .."
                    << std::endl;
          boost::asio::async_read_until(
              *(self->socket), *readbuf, "\r\n\r\n",
              [self, readbuf, writebuf](const boost::system::error_code &ec,
                                        std::size_t bytes) {
                if (ec)
                  return;

                std::string resp(boost::asio::buffers_begin(readbuf->data()),
                                 boost::asio::buffers_begin(readbuf->data()) +
                                     bytes);
                std::cout << "Handshake Response:\n" << resp << std::endl;

                if (resp.find("101 Switching Protocols") != std::string::npos) {
                  auto timer = std::make_shared<boost::asio::steady_timer>(
                      self->socket->get_executor());
                  timer->expires_after(boost::asio::chrono::seconds(45));
                  timer->async_wait(
                      [timer](const boost::system::error_code &ec) {
                        if (!ec) {
                          auto expiry_time = timer->expiry();
                          auto count =
                              std::chrono::duration_cast<std::chrono::seconds>(
                                  expiry_time.time_since_epoch())
                                  .count();

                          std::cout << "Ping was not recieved so we break "
                                       "connection ...time_duration_passed:"
                                    << count << std::endl;
                        }
                      });
                  self->read_from_linfr(timer);
                }
              });
        });
  }

  void create_frame(std::string_view data, uint32_t stream_id, bool fin,
                    uint8_t opcode, std::ostream &os) {
    *(this->message_counter_client) = *(this->message_counter_client) + 1;
    // Byte 0: FIN | RSV | Opcode (0x02 for binary)
    uint8_t byte0 = (fin ? 0x80 : 0x00) | opcode & 0x0F;
    os.put(static_cast<char>(byte0));

    if (data.size() + 8 <= 125) {
      os.put(static_cast<char>(0x80 | (data.size() + 8)));

    } else if (data.size() + 8 <= 65536) {
      os.put(static_cast<char>(0x80 | 126));
      uint16_t ext_len = htons(static_cast<uint16_t>(data.size() + 8));
      os.write(reinterpret_cast<const char *>(&ext_len), 2);

    } else {
      os.put(static_cast<char>(126));
      uint16_t ext_len = htons(static_cast<uint16_t>(65536));
      os.write(reinterpret_cast<const char *>(&ext_len), 2);
    }

    // Stream ID (4 bytes)
    uint32_t net_sid = htonl(stream_id);
    os.write(reinterpret_cast<const char *>(&net_sid), 4);
    // Frame ID (4 bytes)
    uint32_t frame_id_sid = htonl(*(this->message_counter_client));
    os.write(reinterpret_cast<const char *>(&frame_id_sid), 4);

    // Payload
    os.write(data.data(), data.size());
  };

  void send_message(std::shared_ptr<boost::asio::streambuf> write_buf) {
    auto self = shared_from_this();
    boost::asio::async_write(
        *(self->socket), *write_buf,
        [self](const boost::system::error_code &ec, std::size_t bytes) {
          if (!ec) {
            std::cout << "Sent message ... Message_counter at:"
                      << *(self->message_counter_client) << std::endl;
          }
        });
  }
  void read_from_linfr(std::shared_ptr<boost::asio::steady_timer> timer) {
    auto self = shared_from_this();
    auto state = std::make_shared<ReadState>();

    // Recursive lambda to process subsequent frames within the same message
    // state
    auto read_next_frame = std::make_shared<std::function<void()>>();
    *read_next_frame = [self, state, timer, read_next_frame]() {
      boost::asio::async_read(
          *(self->socket), boost::asio::buffer(state->header, 2),
          [self, state, timer, read_next_frame](
              const boost::system::error_code &ec, std::size_t bytes) {
            if (!ec) {
              std::cout << bytes << std::endl;
              wss_frame frame;
              frame.OPCODE = state->header[0] & 0x0F;
              frame.FIN = (state->header[0] >> 7) & 0x01;
              frame.LENGTH = (state->header[1] & 0x7F);
              std::cout << frame.OPCODE << "---" << frame.EXTENDED_LENGTH
                        << "--" << frame.LENGTH << std::endl;
              if (frame.LENGTH > 125) {
                auto ext_len_buf = std::make_shared<uint16_t>();
                boost::asio::async_read(
                    *(self->socket), boost::asio::buffer(ext_len_buf.get(), 2),
                    [self, frame, ext_len_buf, state, timer,
                     read_next_frame](const boost::system::error_code &ec,
                                      std::size_t) mutable {
                      if (ec)
                        return;
                      (*(self->message_counter_client))++;
                      frame.EXTENDED_LENGTH = ntohs(*ext_len_buf);
                      std::cout << "125> message processign .."
                                << self->message_counter_client << std::endl;

                      auto full_payload =
                          std::make_shared<std::vector<uint8_t>>(
                              frame.EXTENDED_LENGTH);
                      boost::asio::async_read(
                          *(self->socket),
                          boost::asio::buffer(full_payload->data(),
                                              full_payload->size()),
                          [self, frame, full_payload, state, timer,
                           read_next_frame](const boost::system::error_code &ec,
                                            std::size_t) mutable {
                            if (ec)
                              return;

                            uint32_t net_sid;
                            std::memcpy(&net_sid, full_payload->data(), 4);
                            uint32_t frame_id_sid;
                            std::memcpy(&frame_id_sid, full_payload->data() + 4,
                                        4);
                            frame.py.STREAM_ID = ntohl(net_sid);
                            frame.py.FRAME_ID = ntohl(frame_id_sid);
                            frame.py.payload.assign(
                                reinterpret_cast<char *>(full_payload->data() +
                                                         8),
                                full_payload->size() - 8);

                            std::cout << "125> message processign .. payload "
                                      << std::endl;
                            state->current_message_frames.push_back(
                                std::move(frame));

                            // Handle Ping (Opcode 0x09)
                            if (state->current_message_frames.back().OPCODE ==
                                0x09) {
                              if(!self->debug_replay){
                              timer->expires_at(
                                  timer->expiry() +
                                  boost::asio::chrono::seconds(45));
                              auto write_buff =
                                  std::make_shared<boost::asio::streambuf>();
                              std::ostream os(write_buff.get());

                              self->create_frame("", 1234, 1, 0x0A,
                                                 os); // Send Pong
                              self->send_message(write_buff); }
                              else {
                              
                              
                              timer->expires_at(
                                  timer->expiry() +
                                  boost::asio::chrono::seconds(45));
                              auto write_buff =
                                  std::make_shared<boost::asio::streambuf>();
                              std::ostream os(write_buff.get());

                              self->create_frame("", 1234, 1, 0x0A,
                                                 os); // Send Pong
                              self->send_message(write_buff); 
                              
                              }
                            }

                            // Check if message is complete
                            if (state->current_message_frames.back().FIN == 1) {
                              std::string final_msg;
                              for (auto &f : state->current_message_frames) {
                                final_msg += f.py.payload;
                              }
                              std::cout << "Message complete: " << final_msg
                                        << std::endl;

                              // Start fresh with a new state for the next
                              // message
                              self->read_from_linfr(timer);
                            } else {
                              // Recursively read the next frame using the SAME
                              // state
                              std::cout << "125> message processign ..read next"
                                        << std::endl;

                              (*read_next_frame)();
                            }
                          });
                    });
              } else {

                frame.EXTENDED_LENGTH = ntohs(0);

                auto full_payload =
                    std::make_shared<std::vector<uint8_t>>(frame.LENGTH);
                boost::asio::async_read(
                    *(self->socket),
                    boost::asio::buffer(full_payload->data(),
                                        full_payload->size()),
                    [self, frame, full_payload, state, timer,
                     read_next_frame](const boost::system::error_code &ec,
                                      std::size_t) mutable {
                      if (ec)
                        return;
                      (*(self->message_counter_client))++;
                      uint32_t net_sid;
                      std::memcpy(&net_sid, full_payload->data(), 4);
                      uint32_t frame_id_sid;
                      std::memcpy(&frame_id_sid, full_payload->data() + 4, 4);
                      frame.py.STREAM_ID = ntohl(net_sid);
                      frame.py.FRAME_ID = ntohl(frame_id_sid);
                      frame.py.payload.assign(
                          reinterpret_cast<char *>(full_payload->data() + 8),
                          full_payload->size() - 8);

                      state->current_message_frames.push_back(std::move(frame));
                      std::cout << "125 message processign .." << std::endl;
                      // Handle Ping (Opcode 0x09)
                      if (state->current_message_frames.back().OPCODE == 0x09) {
                        timer->expires_at(timer->expiry() +
                                          boost::asio::chrono::seconds(45));
                        auto write_buff =
                            std::make_shared<boost::asio::streambuf>();
                        std::ostream os(write_buff.get());

                        self->create_frame("", 1234, 1, 0x0A,
                                           os); // Send Pong
                        self->send_message(write_buff);
                      }

                      // Check if message is complete
                      if (state->current_message_frames.back().FIN == 1) {
                        std::string final_msg;
                        for (auto &f : state->current_message_frames) {
                          final_msg += f.py.payload;
                        }
                        std::cout << "Message complete: " << final_msg
                                  << std::endl;

                        // Start fresh with a new state for the next
                        // message
                        self->read_from_linfr(timer);
                      } else {
                        // Recursively read the next frame using the SAME
                        // state
                        std::cout << "125 message processign .. read_next_frame"
                                  << std::endl;

                        (*read_next_frame)();
                      }
                    });
              }
            } else if (ec != boost::asio::error::operation_aborted) {
              std::cerr << "Read error: " << ec.message() << std::endl;
            }
          });
    };

    // Kick off the first read
    boost::asio::post(socket->get_executor(),
                      [read_next_frame]() { (*read_next_frame)(); });
  }

  void send_wss_binary_frames() {
    auto writebuf = std::make_shared<boost::asio::streambuf>();
    auto read_buf = std::make_shared<boost::asio::streambuf>();
    std::ostream os(writebuf.get());

    // Frame 1: Partial data (FIN = 0)
    create_frame("Stream part 1: Init", 1234, false, 0x02, os);
    // Frame 2: Final data (FIN = 1)
    create_frame("Stream part 2: End", 1234, true, 0x02, os);

    create_frame("", 1234, true, 0x08, os);

    this->send_message(writebuf);
  };
};

int main(int argc, char *argv[]) {
  boost::asio::io_context io_context;
  auto cl = std::make_shared<boost::asio::ssl::context>(
      boost::asio::ssl::context::tlsv12_client);
  cl->set_verify_mode(boost::asio::ssl::verify_none);

  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket(io_context,
                                                                *cl);

  boost::asio::ip::tcp::resolver resolver(io_context);
  auto endpoint = resolver.resolve("127.0.0.1", "8000");

  boost::asio::async_connect(
      socket.lowest_layer(), endpoint,
      [&](const boost::system::error_code &err, const auto &endpoints) {
        if (!err) {
          socket.async_handshake(
              boost::asio::ssl::stream_base::client,
              [&socket, argv, cl](const boost::system::error_code &ec) {
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
                        [&socket, &reader, &writebuf,
                         cl](const boost::system::error_code &ec,
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
                        [&](const boost::system::error_code &ec,
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
                    auto wss_socket = std::make_shared<
                        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
                        std::move(socket));
                    auto wss_client = std::make_shared<Wss_client>(wss_socket);
                    wss_client
                        ->connect(); // logging  a simple idea of hsared_ptr
                                     // which i keep forgetting , here if wss
                                     // client is not inheriting the std ptr
                                     // class this break when we invoke the
                                     // connect , because here wss_client is
                                     // shared_ptr so wss_client with invoke
                                     // connect , ... etc , move forward due
                                     // async nature and starts to remove stuff
                                     // stack/heap , so wss client goes missing
                                     // , when our connect func executes after a
                                     // while we captured 'this' which is a
                                     // simple pointer to a shared_ptr which is
                                     // over , boom , breaks .... , here we need
                                     // to pass share_ptr into wss-client to
                                     // tell that this variable is required ,
                                     // fixxed
                    // wss_client->send_wss_binary_frames();
                  }else if (cmd=="wss-t1"){
                    auto wss_socket = std::make_shared<
                        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
                        std::move(socket));
                    auto wss_client = std::make_shared<Wss_client>(wss_socket);
                    wss_client
                        ->connect();
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
