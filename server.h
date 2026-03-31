#include "boost/exception/exception.hpp"
#include <boost/asio.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/detail/std_fenced_block.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_at.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/system/error_code.hpp>
#include <cstddef>
#include <exception>
#include <iostream>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

class Connection;
class Request {
public:
  boost::asio::streambuf request_buffer;
  std::string version;
  std::string method;
  std::string body;
  std::string path;
  std::map<std::string, std::string> request_parsed;
  bool keepALive = false;
};

class Response {
public:
  Connection *parent_conn = nullptr;
  boost::asio::streambuf response_buffer;
  std::string version;
  void send();
  void write_response(); // write the response status,body ..etc
};

class Connection : std::enable_shared_from_this<Connection> {

public:
  int id;
  std::string name;
  std::shared_ptr<Request> req;
  std::shared_ptr<Response> res;
  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> conn_socket;
  boost::asio::streambuf reader;
  Connection(int id, std::string name,
             boost::asio::ssl::stream<boost::asio::ip::tcp::socket> soc)
      : id(id), name(std::to_string(id) + name), conn_socket(std::move(soc)) {
    std::cout << "Connection accepted:" + std::to_string(id) << std::endl;
    res = std::make_shared<Response>();
    req = std::make_shared<Request>();
    res->parent_conn = this;
  };
};

// class forwarding inline function for response obj
inline void Response::send() {

  std::ostream os(&response_buffer);
  os << "HTTP/1.1 200 OK\r\n"
     << "Content-Type: text/plain\r\n"
     << "Content-Length: 11\r\n"
     << "\r\n"
     << "Hello World";

  boost::asio::async_write(
      this->parent_conn->conn_socket, response_buffer,
      [](const boost::system::error_code &ec, std::size_t bytes_transferred) {
        if (!ec) {
          std::cout << "Sent a response message .." << std::endl;
        }
      });
};

class Server {

protected:
  // Variables:
  //  Need asio context
  //  need host & port
  //  a method to send message over the wire
  //  request parser for incoming request over the wire
  //  listen function for accepting connections
  //  workflow
  //  listen -> accept -> parse -> send_response ( http_response )
  boost::asio::io_context io_context;
  std::string host;
  unsigned int port;
  std::optional<std::thread> server_thread;
  boost::asio::ip::tcp::endpoint server_endpoint;
  boost::asio::ip::tcp::acceptor acceptor;
  std::vector<std::shared_ptr<Connection>> connections_list;
  boost::asio::ssl::context ssl_context{boost::asio::ssl::context::tls_server};
  std::map<std::string,
           std::map<std::string,
                    std::function<void(std::shared_ptr<Request> &req,
                                       std::shared_ptr<Response> &res)>>>
      server_resources;
  std::map<std::string, std::string> ParsedResourceMap;
  std::function<void(std::shared_ptr<Request> &req,
                     std::shared_ptr<Response> &res)>
      default_callback =
          [](std::shared_ptr<Request> &req, std::shared_ptr<Response> &res) {
            std::cout << "404 Page " << std::endl;
          };
  bool keepALive = 0;
  void load_ssl_options() {
    try {
      const char *cert_path = "server.crt";
      const char *key_path = "server.key";
      this->ssl_context.use_certificate_chain_file(cert_path);
      this->ssl_context.use_private_key_file(key_path,
                                             boost::asio::ssl::context::pem);
      std::cout << "SSL OPTIONS Loaded .." << std::endl;
    } catch (const boost::system::error_code &err) {
      std::cout << err.message() << std::endl;
    }
  }
  class Processor {
  public:
    enum class FILTERS {
      PARSE_HEADER,
      BODY_PARSER,
      TRANSFER_ENCODING,
      WSS,
      HTTP2,
      SSE
    };
    std::shared_ptr<Connection> conn;
    Processor(std::shared_ptr<Connection> conn) : conn(conn) {
      Incoming_unprocessed_request = conn->req;
    };
    std::shared_ptr<Request> Incoming_unprocessed_request;
    std::map<FILTERS, std::function<void(std::shared_ptr<Request>)>> filters = {

        {FILTERS::PARSE_HEADER,
         [this](std::shared_ptr<Request> req) {
           boost::asio::async_read_until(
               this->conn->conn_socket, req->request_buffer, "\r\n\r\n",
               [this, req](const boost::system::error_code &error,
                           std::size_t bytes_transferred) {
                 if (!error) {

                   std::string line(bytes_transferred, '\0');
                   std::istream is(&req->request_buffer);
                   is.read(line.data(), bytes_transferred);
                   req->request_buffer.consume(bytes_transferred);

                   std::istream iss(&req->request_buffer);
                   std::string request_line;
                   std::getline(
                       iss,
                       request_line); // first line: GET /index?x=1 HTTP/1.1

                   std::string full_path;
                   std::istringstream rl(request_line);
                   rl >> conn->req->method >> full_path >> conn->req->version;
                   // Extract path and query
                   auto qpos = full_path.find("?");
                   conn->req->path = (qpos != std::string::npos)
                                         ? full_path.substr(0, qpos)
                                         : full_path;

                   if (qpos != std::string::npos) {
                     size_t aepos = full_path.find("=", qpos);
                     if (aepos != std::string::npos) {
                       std::string line;
                       auto cur = qpos + 1;
                       while (aepos != std::string::npos) {
                         auto apos = full_path.find("&", aepos);
                         if (apos == std::string::npos)
                           apos = full_path.length();
                         auto k1 = full_path.substr(cur, aepos - cur);
                         auto v1 =
                             full_path.substr(aepos + 1, apos - aepos - 1);
                         cur = apos + 1;
                         aepos = full_path.find("=", cur);

                         req->request_parsed[k1] = v1;
                       }
                     }
                   }

                   bool te_flag = 0;
                   bool sse_flag = 0;

                   // header_parsing done here we will add the extra required
                   // modules here accordingly
                   while (std::getline(iss, request_line)) {
                     std::cout << request_line << std::endl;
                     auto e_pos = request_line.find(":");

                     if (e_pos == -1)
                       break;

                     req->request_parsed[request_line.substr(0, e_pos)] =
                         request_line.substr(e_pos + 2, request_line.length() -
                                                            e_pos - 2 - 1);
                     if (request_line.substr(0, e_pos) == "Transfer-Encoding" &&
                         request_line.substr(e_pos + 2, request_line.length() -
                                                            e_pos - 2 - 1) ==
                             "chunked") {
                       this->filter_chain.push_back(FILTERS::TRANSFER_ENCODING);
                       req->keepALive = true;
                     }
                     if (request_line.substr(0, e_pos) == "Content-Type" &&
                         request_line.substr(e_pos + 2, request_line.length() -
                                                            e_pos - 2 - 1) ==
                             "text/event-stream") {
                       this->filter_chain.push_back(FILTERS::SSE);
                       req->keepALive = true;
                     }
                   }

                   std::cout << "remaining possible body buffer:"
                             << conn->reader.size() << std::endl;

                 } else {
                   std::cout << "Error at parsing:" << error.message()
                             << std::endl;
                 }
               });
         }},

        {FILTERS::TRANSFER_ENCODING,
         [this](std::shared_ptr<Request> req) {
           boost::asio::async_read_until(
               this->conn->conn_socket, req->request_buffer, "\r\n",
               [this](const boost::system::error_code &ec, size_t bytes_size) {
                 if (!ec) {
                   
                 } else {
                   std::cout << "Error in transfer encoding read "
                             << ec.message() << std::endl;
                 }
               });
         }

        }

    };
    std::vector<FILTERS> filter_chain;
    void process_request() {
      return;
    }; // convert Incoming request into some other forma if required ....
    void register_filter(FILTERS reg_filter) {
      filter_chain.push_back(reg_filter);
    }; // register a filter to be applied
    void apply_filters() {
      for (auto i : filter_chain) {
        filters[i](Incoming_unprocessed_request);
      }
    }; // apply the filters
  };

  class Generator {
  public:
    enum class PHASE { SUBREQUEST };
    Generator(std::shared_ptr<Connection> connection) : conn(connection) {};
    std::shared_ptr<Connection> conn;
    std::shared_ptr<Response> Outgoing_unprocessed_response;
    std::map<PHASE, std::function<std::shared_ptr<Response>(
                        std::shared_ptr<Response>)>>
        phase_handlers;
    std::vector<PHASE> phase_link;
    void generate_unprocessed_default_response();
    void register_phase_handler(PHASE phase) { phase_link.push_back(phase); }
    void apply_phase_handlers() {
      for (auto j : phase_link) {
        Outgoing_unprocessed_response =
            phase_handlers[j](Outgoing_unprocessed_response);
      }
    };
    void write_to_client() {
      Outgoing_unprocessed_response
          ->send(); // rewrite this this to be more than default value , use
                    // Response class itself
    };
  };

  void listen(int init_id) {
    // Listen logic
    acceptor.async_accept([init_id,
                           this](const boost::system::error_code &error,
                                 boost::asio::ip::tcp::socket peer) {
      if (!error) {
        auto new_conn = std::make_shared<Connection>(
            init_id, "",
            boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(
                std::move(peer), this->ssl_context));
        new_conn->conn_socket.set_verify_mode(boost::asio::ssl::verify_none);
        TLS_handshake_connection_worker(new_conn);
        boost::asio::post(this->io_context, [this, init_id]() mutable {
          auto temp_id = init_id + 1;
          this->listen(temp_id);
        });
      } else {
        std::cout << "[Error at connection acceptance:]" + error.message()
                  << std::endl;
      }
    });
  };
  void read(const std::shared_ptr<Connection> &conn) {
    boost::asio::async_read_until(
        conn->conn_socket, conn->reader, "\r\n\r\n",
        [this, conn](const boost::system::error_code &error,
                     std::size_t bytes_transferred) {
          if (!error) {

            std::string line(bytes_transferred, '\0');
            std::istream is(&conn->reader);
            is.read(line.data(), bytes_transferred);
            conn->reader.consume(bytes_transferred);
            std::cout << "--" << bytes_transferred << "--"
                      << conn->reader.size() << std::endl;
            std::cout << "----\n";
            std::cout << line << std::endl;
            std::cout << "----\n";
            if (!line.empty())
              request_parser(line, conn);
          } else {
            std::cout << "Error at read:" << error.message() << std::endl;
          }
        });
  };

  void API_HANDLER(const std::shared_ptr<Connection> &conn) {
    boost::asio::post(this->io_context, [this, conn] {
      // take care of the request handler for the endpoint /blah.....
      if (conn->req->path.length() != 0) {
        if (requesthandlercallback(conn->req->path, conn->req->method,
                                   conn->req, conn->res))
          std::cout << "Handler request processed" << std::endl;
        else
          default_callback(conn->req, conn->res);
      }
      if (!conn->req->keepALive) {

        conn->conn_socket.async_shutdown(
            [this, conn](const boost::system::error_code &ec) {
              if (!ec) {
                conn->conn_socket.lowest_layer().close();
              } else {
                std::cout << ec.message() << std::endl;
              }
            });
        connections_list.erase(std::remove_if(
            connections_list.begin(), connections_list.end(),
            [conn](const auto &n_conn) { return conn == n_conn; }));
      }
    });
  }

  void request_parser(std::string request_content,
                      const std::shared_ptr<Connection> &conn) {
    // request_parser for query string , content-length ,version
    std::istringstream iss(request_content);
    std::string request_line;
    std::getline(iss, request_line); // first line: GET /index?x=1 HTTP/1.1

    std::string full_path;
    std::istringstream rl(request_line);
    rl >> conn->req->method >> full_path >> conn->req->version;
    // Extract path and query
    auto qpos = full_path.find("?");
    conn->req->path =
        (qpos != std::string::npos) ? full_path.substr(0, qpos) : full_path;

    if (qpos != std::string::npos) {
      size_t aepos = full_path.find("=", qpos);
      if (aepos != std::string::npos) {
        std::string line;
        auto cur = qpos + 1;
        while (aepos != std::string::npos) {
          auto apos = full_path.find("&", aepos);
          if (apos == std::string::npos)
            apos = full_path.length();
          auto k1 = full_path.substr(cur, aepos - cur);
          auto v1 = full_path.substr(aepos + 1, apos - aepos - 1);
          cur = apos + 1;
          aepos = full_path.find("=", cur);

          ParsedResourceMap[k1] = v1;
        }
      }
    }

    bool te_flag = 0;
    bool sse_flag = 0;

    // header_parsing
    while (std::getline(iss, request_line)) {
      std::cout << request_line << std::endl;
      auto e_pos = request_line.find(":");

      if (e_pos == -1)
        break;

      ParsedResourceMap[request_line.substr(0, e_pos)] =
          request_line.substr(e_pos + 2, request_line.length() - e_pos - 2 - 1);
      if (request_line.substr(0, e_pos) == "Transfer-Encoding" &&
          request_line.substr(e_pos + 2, request_line.length() - e_pos - 2 -
                                             1) == "chunked") {
        te_flag = 1;
        keepALive = 1;
      }
      if (request_line.substr(0, e_pos) == "Content-Type" &&
          request_line.substr(e_pos + 2, request_line.length() - e_pos - 2 -
                                             1) == "text/event-stream") {
        sse_flag = 1;
        keepALive = 1;
      }
    }

    std::cout << "remaining possible body buffer:" << conn->reader.size()
              << std::endl;

    if (te_flag) {
      if (conn->reader.size()) {
        std::istream ss(&conn->reader);
        std::string line;
        int bytes_to_read = 0;
        while (conn->reader.size() > 0) {

          std::getline(ss, line);

          if (!line.empty() && line.back() == '\r') {
            line.pop_back();
          }
          try {
            bytes_to_read = std::stoul(line, nullptr, 16);
          } catch (std::exception &err) {
            std::cout << "expecetion stdoul" << std::endl;
          }

          std::size_t buffered_bytes = conn->reader.size() - ss.tellg();

          if (buffered_bytes >= bytes_to_read + 2) {
            std::vector<char> chunk(bytes_to_read);
            ss.read(chunk.data(), bytes_to_read);
            conn->req->body.append(chunk.data(), bytes_to_read);

            ss.ignore(2);
            conn->reader.consume(ss.tellg());
          } else {
            boost::asio::streambuf temp;
            boost::asio::async_read(
                conn->conn_socket, temp,
                boost::asio::transfer_exactly(bytes_to_read - buffered_bytes),
                [&temp, conn, &ss,
                 bytes_to_read](const boost::system::error_code &ec,
                                size_t bytes_transferred) {
                  if (!ec) {
                    std::ostream os(&conn->reader);
                    os << &temp;
                    std::vector<char> chunk(bytes_to_read);
                    ss.read(chunk.data(), bytes_to_read);
                    conn->req->body.append(chunk.data(), bytes_to_read);

                    ss.ignore(2);
                    conn->reader.consume(ss.tellg());
                  }
                });
          }
        }
      }
      read_chunked_transfer(conn, conn->req);
    }
  }

  void read_chunked_transfer(const std::shared_ptr<Connection> &conn,
                             const std::shared_ptr<Request> &req) {
    boost::asio::async_read_until(
        conn->conn_socket, conn->reader, "\r\n",
        [conn, this, req](const boost::system::error_code &ec,
                          size_t bytes_transferred) {
          if (!ec) {
            std::istream ss(&conn->reader);
            std::string line;
            std::getline(ss, line);
            std::cout << line << "---" << bytes_transferred << std::endl;
            if (!line.empty() && line.back() == '\r')
              line.pop_back();
            const size_t chunk_size = std::stoull(line, 0, 16);

            if (chunk_size == 0) {
              // stopping condition
              std::cout << "Received the last chunk" << std::endl;
              boost::asio::streambuf buff;
              boost::asio::async_read(
                  conn->conn_socket, buff, boost::asio::transfer_all(),
                  [this, conn](const boost::system::error_code &ec,
                               size_t bytes_transferred) {
                    if (!ec) {
                      std::cout << " final line removed " << std::endl;
                    }
                  });
            } else {
              boost::asio::async_read(
                  conn->conn_socket, conn->reader,
                  boost::asio::transfer_exactly(chunk_size + 2),
                  [req, this, conn](const boost::system::error_code &ec,
                                    std::size_t bytes_transferred) mutable {
                    if (!ec) {
                      std::istream bs(&conn->reader);
                      std::string line_c;
                      std::getline(bs, line_c);

                      std::cout << "line _rec:" << line_c << std::endl;
                      req->body += line_c.substr(0, line_c.size() - 2);

                      bs.ignore(2);
                      this->read_chunked_transfer(conn, req);
                    }
                  });
            }
          } else {
          }
        });
  }

  bool requesthandlercallback(std::string &regex, std::string &method,
                              std::shared_ptr<Request> &req,
                              std::shared_ptr<Response> &res) {
    try {
      // check request contents here .... (should be processed)
      std::cout << "Request-post-body:" << req->body << std::endl;
      auto route_checker = server_resources.find(regex);
      if (route_checker == server_resources.end())
        return false;
      auto callback_checker = route_checker->second.find(method);
      if (callback_checker == route_checker->second.end())
        return false;
      auto handler_checker = callback_checker->second;
      if (!handler_checker)
        return false;
      server_resources[regex][method](req, res);
      return 1;
    } catch (std::error_code err) {
      std::cout << err.message() << std::endl;
      return false;
    }
  };

  void TLS_handshake_connection_worker(std::shared_ptr<Connection> &conn) {
    conn->conn_socket.async_handshake(
        boost::asio::ssl::stream_base::server,
        [this, conn](const boost::system::error_code &error) {
          if (!error) {
            this->log_info_tls(conn->conn_socket);
            this->connections_list.push_back(conn);
            this->read(conn);
          } else {
            std::cout << "TLS_handshake failed .." + error.message()
                      << std::endl;
          }
        });
  }

  void processor_generator_handler(std::shared_ptr<Connection> &conn) {
    auto processor = std::make_shared<Processor>(conn);
    auto generator = std::make_shared<Generator>(conn);

    processor->register_filter(Processor::FILTERS::PARSE_HEADER);
    processor->apply_filters();

    API_HANDLER(conn);

    generator->apply_phase_handlers();
    generator->write_to_client();
  }

  void log_info_tls(
      boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &tls_socket) {
    SSL *native_handle = tls_socket.native_handle();
    if (native_handle) {
      const char *tls_version = SSL_get_version(native_handle);
      const SSL_CIPHER *tls_cipher = SSL_get_current_cipher(native_handle);
      auto client_cipher = SSL_get_client_ciphers(native_handle);
      std::cout << "TLS Version:" << tls_version << std::endl;
      std::cout << "TLS Cipher:" << SSL_CIPHER_get_name(tls_cipher)
                << std::endl;
    }
  }

public:
  Server(std::string host, unsigned int port)
      : host(host), port(port),
        server_endpoint(boost::asio::ip::make_address_v4(host), port),
        acceptor(io_context) {
    load_ssl_options();
    int init_id = 123;
    // primed the acceptor object
    acceptor.open(boost::asio::ip::tcp::v4());
    acceptor.bind(server_endpoint);
    acceptor.listen();
    // listen
    listen(init_id);
  };
  void run() {
    if (this->acceptor.is_open())
      std::cout << "Acceptor is open" << std::endl;
    server_thread.emplace([this]() { this->io_context.run(); });
  };
  void stop() {
    this->io_context.stop();

    if (server_thread->joinable()) {
      server_thread->join();
    }

    for (auto c : connections_list) {
      boost::system::error_code ec;
      auto e = c->conn_socket.shutdown(ec);
      c->conn_socket.lowest_layer().close();
    }
    connections_list.clear();
    this->acceptor.set_option(
        boost::asio::ip::tcp::acceptor::reuse_address(true));
  };
  void register_handler(
      std::string regex_string, std::string method,
      std::function<void(std::shared_ptr<Request> &, std::shared_ptr<Response>)>
          callbackfunction) {
    server_resources[regex_string][method] = std::move(callbackfunction);
  }
};
