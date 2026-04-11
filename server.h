#include "boost/asio/buffers_iterator.hpp"
#include "boost/asio/post.hpp"
#include "boost/asio/write.hpp"
#include "boost/system/detail/error_code.hpp"
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
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <istream>
#include <iterator>
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
  std::string transfer_encoded_string;
  std::map<std::string, std::string> request_parsed;

  bool keepALive = false;
};

class Response {
public:
  enum class RESPONSE_CODES { HTTP_200, HTTP_404, HTTP_500 };
  std::map<RESPONSE_CODES, std::string> RESPONSE_MESSAGE_MAP = {
      {RESPONSE_CODES::HTTP_200, "Ok"},
      {RESPONSE_CODES::HTTP_404, "Not Found"},
      {RESPONSE_CODES::HTTP_500, "Server Failed"}};
  std::map<RESPONSE_CODES, std::string> RESPONSE_CODE_MAP = {
      {RESPONSE_CODES::HTTP_200, "200"},
      {RESPONSE_CODES::HTTP_404, "404"},
      {RESPONSE_CODES::HTTP_500, "500"}};
  Connection *parent_conn = nullptr;
  boost::asio::streambuf response_buffer;
  std::string version;
  std::string body;
  std::map<std::string, std::string> map_body;
  RESPONSE_CODES res_code;
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
  enum class MIME { HTML, CSS, JS };

  class Processor : public std::enable_shared_from_this<Processor> {
  public:
    enum class FILTERS {
      PARSE_HEADER,
      BODY_PARSER,
      TRANSFER_ENCODING,
      WSS_HEADER,
      WSS_BODY,
      STATIC_SERVING,
      HTTP2,
      SSE
    };
    std::shared_ptr<Connection> conn;
    std::shared_ptr<Request> Incoming_unprocessed_request;
    Processor(std::shared_ptr<Connection> conn)
        : conn(conn), Incoming_unprocessed_request(conn->req) {};

    std::map<FILTERS, std::function<void(std::shared_ptr<Request>,
                                         std::function<void()>)>>
        filters = {

            {FILTERS::PARSE_HEADER,
             [this](std::shared_ptr<Request> req, auto next) {
               boost::asio::async_read_until(
                   this->conn->conn_socket, req->request_buffer, "\r\n\r\n",
                   [this, req, next](const boost::system::error_code &error,
                                     std::size_t bytes_transferred) {
                     if (!error) {

                       std::istream iss(&req->request_buffer);
                       std::string request_line;
                       std::getline(
                           iss,
                           request_line); // first line: GET /index?x=1 HTTP/1.1

                       std::string full_path;
                       std::istringstream rl(request_line);
                       rl >> req->method >> full_path >> req->version;
                       std::cout << req->version << "-" << full_path << "-"
                                 << req->method;
                       // Extract path and query
                       auto qpos = full_path.find("?");
                       req->path = (qpos != std::string::npos)
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
                       auto temp_path = req->path;
                       auto spos = temp_path.find(".");
                       std::vector<MIME> mime_types;

                       if (spos != std::string::npos) {
                         auto s_word = temp_path.substr(
                             spos + 1, temp_path.size() - spos);
                         if (s_word == "html")
                           mime_types.push_back(MIME::HTML);
                         else if (s_word == "css")
                           mime_types.push_back(MIME::CSS);
                         else if (s_word == "js")
                           mime_types.push_back(MIME::JS);
                       }
                       if (mime_types.size() != 0) {
                         this->filter_chain.push_back(FILTERS::STATIC_SERVING);
                       }

                       bool te_flag = 0;
                       bool sse_flag = 0;

                       // header_parsing done here we will add the extra
                       // required modules here accordingly
                       while (std::getline(iss, request_line)) {
                         std::cout << request_line << std::endl;
                         auto e_pos = request_line.find(":");

                         if (e_pos == -1)
                           break;

                         req->request_parsed[request_line.substr(0, e_pos)] =
                             request_line.substr(e_pos + 2,
                                                 request_line.length() - e_pos -
                                                     2 - 1);
                         if (request_line.substr(0, e_pos) ==
                                 "Transfer-Encoding" &&
                             request_line.substr(e_pos + 2,
                                                 request_line.length() - e_pos -
                                                     2 - 1) == "chunked") {
                           this->filter_chain.push_back(
                               FILTERS::TRANSFER_ENCODING);
                           req->keepALive = true;
                         }
                         if (request_line.substr(0, e_pos) == "Content-Type" &&
                             request_line.substr(
                                 e_pos + 2, request_line.length() - e_pos - 2 -
                                                1) == "text/event-stream") {
                           this->filter_chain.push_back(FILTERS::SSE);
                           req->keepALive = true;
                         }
                         if (request_line.substr(0, e_pos) == "Upgrade" &&
                             request_line.substr(e_pos + 2,
                                                 request_line.length() - e_pos -
                                                     2 - 1) == "websocket") {
                           this->filter_chain.push_back(FILTERS::WSS_HEADER);
                           req->keepALive = true;
                         }
                       }

                       std::cout << "remaining possible body buffer:"
                                 << req->request_buffer.size() << std::endl;
                       next();
                     } else {
                       std::cout << "Error at parsing:" << error.message()
                                 << std::endl;
                     }
                   });
             }},

            {FILTERS::TRANSFER_ENCODING,
             [this](std::shared_ptr<Request> req, auto next) {
               auto read_transfer_encoding =
                   std::make_shared<std::function<void()>>();
               *(read_transfer_encoding) = [this, req, read_transfer_encoding,
                                            next]() {
                 boost::asio::async_read_until(
                     this->conn->conn_socket, req->request_buffer, "\r\n",
                     [this, req, read_transfer_encoding,
                      next](const boost::system::error_code &ec,
                            size_t bytes_size) {
                       if (!ec) {
                         std::istream iss(&req->request_buffer);
                         std::string line;
                         std::getline(iss, line);
                         std::cout << req->request_buffer.size() << std::endl;
                         std::cout << line << "<----" << line.size()
                                   << std::endl;
                         int chunk_length;
                         if (line.back() == '\r')
                           line.pop_back();
                         try {
                           chunk_length = std::stoul(line, nullptr, 16);
                         } catch (boost::system::error_code err) {
                           std::cout << "STOUL error" << std::endl;
                         };
                         std::cout << "chunk_length:" << chunk_length
                                   << "buffer_size_current:"
                                   << req->request_buffer.size() << std::endl;
                         if (chunk_length == 0) {
                           std::cout << "inside zero chunk" << std::endl;
                           boost::asio::async_read_until(
                               this->conn->conn_socket, req->request_buffer,
                               "\r\n",
                               [this,
                                next](const boost::system::error_code &err,
                                      size_t bytes_transferred) {
                                 if (!err) {
                                   std::cout << this->conn->req
                                                    ->transfer_encoded_string
                                             << std::endl;
                                   std::cout << "Done with transfer encoding "
                                             << std::endl;
                                   next();
                                 }
                               });
                         }

                         else {

                           if (req->request_buffer.size() >= chunk_length) {

                             auto bufs = req->request_buffer.data();
                             std::string data(boost::asio::buffers_begin(bufs),
                                              boost::asio::buffers_begin(bufs) +
                                                  chunk_length);
                             std::cout << "1 case" << std::endl;
                             req->request_buffer.consume(chunk_length + 2);
                             req->transfer_encoded_string += data;
                             boost::asio::post(
                                 this->conn->conn_socket.get_executor(),
                                 [read_transfer_encoding]() {
                                   (*read_transfer_encoding)();
                                 }); // here we need post wrapping because we
                                     // want the make sure this line is applied
                                     // after we are done with current work on
                                     // the loop , if not not used then this can
                                     // interfere by increasing the stack of
                                     // this section , while some other async
                                     // code is working
                           } else {
                             boost::asio::async_read(
                                 this->conn->conn_socket, req->request_buffer,
                                 boost::asio::transfer_exactly(
                                     chunk_length + 2 -
                                     req->request_buffer.size()),
                                 [this, req, read_transfer_encoding,
                                  chunk_length](
                                     const boost::system::error_code &ec,
                                     std::size_t bytes_transferred) {
                                   if (!ec) {
                                     std::cout
                                         << "2 case"
                                         << std::endl; // in the case we have \n
                                                       // in middle of our
                                                       // payload chunk this
                                                       // logic will break , so
                                                       // you could rewrite
                                                       // using constant buffer
                                     auto bufs = req->request_buffer.data();
                                     std::string data(
                                         boost::asio::buffers_begin(bufs),
                                         boost::asio::buffers_begin(bufs) +
                                             chunk_length);
                                     req->transfer_encoded_string += data;
                                     req->request_buffer.consume(chunk_length +
                                                                 2);
                                     boost::asio::post(
                                         this->conn->conn_socket.get_executor(),
                                         [read_transfer_encoding]() {
                                           (*read_transfer_encoding)();
                                         });
                                   } else {
                                     std::cout << ec.message() << std::endl;
                                   }
                                 });
                           }
                         }

                       } else {
                         std::cout << "Error in transfer encoding read "
                                   << ec.message() << std::endl;
                       }
                     });
               };
               boost::asio::post(this->conn->conn_socket.get_executor(),
                                 [read_transfer_encoding]() {
                                   std::cout << "starting" << std::endl;
                                   (*read_transfer_encoding)();
                                 });
             }

            },
            {FILTERS::SSE,
             [this](std::shared_ptr<Request> req, auto next) {
               std::shared_ptr<std::function<void()>> sse_function;
               *sse_function = [this, req, next]() {
                 // empty for now ig if we need to do decoding of gzip and stuff
                 // we can do it here .... but not required now
                 next();
               };
               boost::asio::post(this->conn->conn_socket.get_executor(),
                                 [sse_function]() {
                                   std::cout << "starting" << std::endl;
                                   (*sse_function)();
                                 });
             }},
            {FILTERS::WSS_HEADER,
             [this](std::shared_ptr<Request> req, auto next) {
               std::vector<std::string> protocols = {"multiplex-v1", "chat-v2"};
               std::vector<std::string> extensions = {"permessage-deflate"};
               std::pair<std::string, std::string> choice_made;
             std:
               auto protocol_finder =
                   find(protocols.begin(), protocols.end(),
                        req->request_parsed["Sec-WebSocket-Protocol"]);
               auto extensions_finder =
                   find(extensions.begin(), extensions.end(),
                        req->request_parsed["Sec-WebSocket-Extensions"]);
               if (protocol_finder != protocols.end()) {
                 auto base64_encode = [](const unsigned char *input,
                                         int length) {
                   BIO *b64, *mem;
                   BUF_MEM *bptr;

                   b64 = BIO_new(BIO_f_base64());
                   mem = BIO_new(BIO_s_mem());

                   // Chain them: b64 -> mem
                   BIO_push(b64, mem);

                   // REQUIRED: No newlines for WebSocket headers
                   BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

                   BIO_write(b64, input, length);
                   BIO_flush(
                       b64); // Ensure final padding characters are written

                   BIO_get_mem_ptr(mem, &bptr);
                   std::string result(bptr->data, bptr->length);

                   BIO_free_all(b64);
                   return result;
                 };
                 auto generate_hash =
                     [base64_encode](const std::string &client_key) {
                       std::string server_magic =
                           "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                       std::string magic = client_key + server_magic;
                       unsigned char hash[20];
                       SHA1((const unsigned char *)magic.data(), magic.size(),
                            hash);
                       return base64_encode(hash, 20);
                     };

                 std::string response =
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: " +
                     generate_hash(req->request_parsed["Sec-WebSocket-Key"]) +
                     "\r\n"
                     "Sec-WebSocket-Protocol: " +
                     *protocol_finder + "\r\n";

                 if (extensions_finder != extensions.end()) {
                   // send a response with extension permessage-deflate
                   response +=
                       "Sec-WebSocket-Extensions: permessage-deflate\r\n";
                 }

                 response += "\r\n";
                 // send a switching message which just sends the protocol
                 // nogotiation and no extension
                 auto response_ptr =
                     std::make_shared<std::string>(std::move(response));

                 boost::asio::async_write(
                     this->conn->conn_socket,
                     boost::asio::buffer(*response_ptr),
                     [response_ptr, this](const boost::system::error_code &ec,
                                          std::size_t /*bytes*/) {
                       if (!ec) {
                         // Handshake complete. Start your Frame Queue logic
                         // here, ehich is out WSS_BODY ...
                         this->filter_chain.push_back(FILTERS::WSS_BODY);
                       }
                     });

               } else {
                 // send denied response here , we cannot find a subprotocol for
                 // you to use .
                 std::string denied =
                     "HTTP/1.1 400 Bad Request\r\n"
                     "Content-Type: text/plain\r\n"
                     "Connection: close\r\n\r\n"
                     "Error: Unsupported WebSocket Subprotocol";

                 auto denied_ptr =
                     std::make_shared<std::string>(std::move(denied));
                 boost::asio::async_write(
                     this->conn->conn_socket, boost::asio::buffer(*denied_ptr),
                     [denied_ptr](const boost::system::error_code &ec,
                                  std::size_t) {
                       if (!ec) {
                         std::cout << "Failed Websocket Protocol Negotiation"
                                   << std::endl;
                       }
                     });
               }
               next();
             }},

        };
    std::vector<FILTERS> filter_chain;
    void process_request() {
      return;
    }; // convert Incoming request into some other forma if required ....
    void register_filter(FILTERS reg_filter) {
      filter_chain.push_back(reg_filter);
    }; // register a filter to be applied
    void
    apply_filters(int index = 0,
                  std::function<void()> next_in_global_chain_link = nullptr) {
      auto self = shared_from_this();
      if (filter_chain.size() == 0)
        return;
      if (index >= filter_chain.size()) {
        std::cout << "escaping" << std::endl;
        next_in_global_chain_link();
        return;
      }
      FILTERS current_filter = filter_chain[index];

      std::cout << filter_chain.size() << " Chain size--index:" << index
                << std::endl;
      for (auto i : filter_chain) {
        std::cout << int(i) << std::endl;
      }

      filters[current_filter](
          self->conn->req, [index, self, next_in_global_chain_link]() {
            self->apply_filters(index + 1, next_in_global_chain_link);
          });
    }; // apply the filters
  };

  class Generator : public std::enable_shared_from_this<Generator> {
  public:
    enum class PHASE {
      SSE,
      SUBREQUEST,
      RESPONSE_WRITE_TO_CLIENT,
      STATIC_SERVER,
      WSS
    };
    Generator(std::shared_ptr<Connection> connection) : conn(connection) {};
    std::shared_ptr<Connection> conn;
    std::map<PHASE, std::function<void(std::shared_ptr<Response>,
                                       std::function<void()>)>>
        phase_handlers = {
            {Server::Generator::PHASE::RESPONSE_WRITE_TO_CLIENT,
             [this](std::shared_ptr<Response> res, auto next) {
               std::ostream os(&res->response_buffer);
               os << this->conn->res
                         ->RESPONSE_CODE_MAP[this->conn->res->res_code]
                  << " "
                  << this->conn->res
                         ->RESPONSE_MESSAGE_MAP[this->conn->res->res_code]
                  << "HTTP1.1\r\n"
                  << "Content-Type: text/plain\r\n"
                  << "Content-Length: 11\r\n"
                  << "\r\n"
                  << "Hello World\r\n";

               if (res->body.size() > 0) {
                 for (auto i : res->map_body) { // TODO :fix this later this is
                                                // not how we do it ...
                   os << i.second;
                 }
               }

               boost::asio::async_write(
                   this->conn->conn_socket, res->response_buffer,
                   [](const boost::system::error_code &ec,
                      std::size_t bytes_transferred) {
                     if (!ec) {
                       std::cout << "Sent a response message .." << std::endl;
                     }
                   });
             }},
            {Server::Generator::PHASE::STATIC_SERVER,
             [this](std::shared_ptr<Response>, auto next) {
               auto temp_req_path = "./assets" + this->conn->req->path;
               auto s_pos = temp_req_path.find(".", 2);
               auto file_name = temp_req_path.substr(0, s_pos - 1);
               auto mime_type = temp_req_path.substr(
                   s_pos + 1, temp_req_path.size() - s_pos);

               try {
                 std::ifstream file(temp_req_path, std::ios::binary);
                 if (!file) {
                   throw std::runtime_error(
                       "404: File not found or access denied: " +
                       temp_req_path);
                 }
                 if (file.bad()) {
                   throw std::runtime_error(
                       "500: Low-level I/O error while reading: " +
                       temp_req_path);
                 }
                 std::ostringstream ss;
                 ss << file.rdbuf();

                 std::ostringstream oss;

                 this->conn->res->body = ss.str();
                 this->conn->res->res_code = Response::RESPONSE_CODES::HTTP_200;
                 std::cout << ss.str() << std::endl;
                 oss << "HTTP/1.1 200 OK\r\n";
                 oss << "Content-Type: text/" << mime_type << "\r\n";
                 oss << "Content-Length: " << ss.str().size() << "\r\n";
                 oss << "Connection: close\r\n";
                 oss << "\r\n";
                 oss << ss.str();
                 std::ostream os(&this->conn->res->response_buffer);
                 os << oss.str();
                 std::cout << oss.str() << std::endl;
                 boost::asio::async_write(
                     this->conn->conn_socket, this->conn->res->response_buffer,
                     [mime_type](const boost::system::error_code &ec,
                                 std::size_t bytes) {
                       if (!ec) {
                         std::cout << "Sent required Asset" << mime_type
                                   << std::endl;
                       }
                     });

               } catch (const boost::system::error_code &err) {
                 std::cout << err.message() << std::endl;
               }
             }},

            {Server::Generator::PHASE::SSE,
             [this](std::shared_ptr<Response>, auto next) {
               std::ostringstream header_oss;
               this->conn->res->res_code = Response::RESPONSE_CODES::HTTP_200;
               header_oss << "HTTP/1.1 200 OK\r\n"
                          << "Content-Type: text/event-stream\r\n"
                          << "Connection: keep-alive\r\n"
                          << "Transfer-Encoding: chunked\r\n"
                          << "\r\n";
               // Pair: {Event_ID, SSE_Formatted_Data}
               std::vector<std::pair<std::string, std::string>> sse_messages = {
                   {"0", "retry: 15000\n\n"},
                   {"1", "id: 1\ndata: First message is a simple string.\n\n"},
                   {"2", "id: 2\ndata: {\"message\": \"JSON payload\"}\n\n"},
                   {"42", "id: 42\nevent: bar\ndata: Multi-line message "
                          "of\ndata: type \"bar\" and id \"42\"\n\n"},
                   {"43", "id: 43\ndata: Last message, id \"43\"\n\n"}};

               boost::asio::async_write(
                   this->conn->conn_socket,
                   boost::asio::buffer(header_oss.str()),
                   [](const boost::system::error_code &ec, std::size_t bytes) {
                     if (!ec) {
                       std::cout << "SSE chunks sent " << std::endl;
                     }
                   });

               auto if_last_event_id_present =
                   this->conn->req->request_parsed.find("Last-Event-ID");
               int index_of_last_event_id_if_exists = -1;
               if (if_last_event_id_present !=
                   this->conn->req->request_parsed.end()) {
                 index_of_last_event_id_if_exists =
                     std::distance(this->conn->req->request_parsed.begin(),
                                   if_last_event_id_present);
               }
               for (const auto &[event_id, content] : sse_messages) {
                 int current_id_num = std::stoi(event_id);
                 if (current_id_num <=
                     std::stoi(
                         sse_messages[index_of_last_event_id_if_exists]
                             .first)) // inefficient code here honestly refactor
                                      // this to something better later on ...
                   continue;
                 std::ostringstream chunk_oss;

                 chunk_oss << std::hex << content.length() << "\r\n";

                 chunk_oss << content << "\r\n";

                 std::string chunk_data = chunk_oss.str();
                 boost::asio::async_write(
                     this->conn->conn_socket, boost::asio::buffer(chunk_data),
                     [](const boost::system::error_code &ec,
                        std::size_t bytes) {
                       if (!ec) {
                         std::cout << "SSE chunk sent " << std::endl;
                       }
                     });
               }

               std::string final_chunk = "0\r\n\r\n";
               boost::asio::async_write(
                   this->conn->conn_socket, boost::asio::buffer(final_chunk),
                   [](const boost::system::error_code &ec, std::size_t bytes) {
                     if (!ec) {
                       std::cout << "final SSE chunk sent " << std::endl;
                     }
                   });
             }},

            {PHASE::WSS, [this](std::shared_ptr<Response>, auto next) {
               struct wss_payload {
                 int STREAM_ID;
                 std::string payload;
               };
               struct wss_frame {
                 int FIN;  // 1bit
                 int RSV1; // 1 bit
                 int RSV2;
                 int RSV3;
                 int OPCODE; // 4 bits
                 int MASK;   // 1 bit
                 int LENGTH; // 7 bits divide into 125 chunks remaining 126 ,127
                             // for extended version frame length
                 int EXTENDED_LENGTH; // 16 unsigned bit integer as frame length
                                      // ...
                 struct wss_payload py;
               };

               std::shared_ptr<std::function<std::vector<wss_frame>(
                   std::string & message, int stream_id)>>
                   create_frame;
               create_frame = std::make_shared<
                   std::function<std::vector<wss_frame>(std::string &, int)>>(
                   [](std::string &msg, int streamid) {
                     std::vector<wss_frame> res_frames;
                     int string_size = msg.size();
                     int offset = 0;

                     while (offset <= string_size) {
                       int payload_chunk_size = std::min(
                           125, string_size -
                                    offset); // Not just chunk size as this is
                                             // for payload and we are trying
                                             // to just create a frame , when
                                             // we add the stream int 32 bit 4
                                             // bytes to existing 125 bytes ...
                                             // we need to use length code 126
                       struct wss_frame frame;
                       frame.FIN = 0;

                       if (offset == 0) {
                         frame.RSV1 = 0;
                         frame.RSV2 = 0;
                         frame.RSV3 = 0;
                         frame.OPCODE = 2;
                       }

                       else {
                         frame.OPCODE = 0;
                         frame.RSV1 = 0;
                       }

                       frame.MASK = 0;
                       frame.LENGTH = 126;
                       frame.EXTENDED_LENGTH =
                           125 + 4; // bytes (payload+streamid)
                       frame.py.payload =
                           msg.substr(offset, offset + payload_chunk_size);
                       frame.py.STREAM_ID = streamid;
                       res_frames.push_back(frame);
                       offset += payload_chunk_size;
                     }

                     return res_frames;
                   });

               std::shared_ptr<std::function<void(
                   std::shared_ptr<std::deque<std::vector<wss_frame>>>)>>
                   write_frame;
               write_frame = std::make_shared<std::function<void(
                   std::shared_ptr<std::deque<std::vector<wss_frame>>>)>>(
                   [this, write_frame](
                       std::shared_ptr<std::deque<std::vector<wss_frame>>> DQ) {
                     while (!DQ->empty()) {
                       auto vec_msgs = DQ->front();
                       DQ->pop_front();
                       for (auto i = 0; i < vec_msgs.size(); i++) {
                         // boost async write here send each chunk acordingly

                         std::vector<uint8_t> wire_buffer;
                         // [FIN(1) | RSV1(1) | RSV2(1) | RSV3(1) | OPCODE(4)]
                         uint8_t byte0 = 0;
                         byte0 |= (vec_msgs[i].FIN & 0x01) << 7;
                         byte0 |= (vec_msgs[i].RSV1 & 0x01) << 6;
                         byte0 |= (vec_msgs[i].RSV2 & 0x01) << 5;
                         byte0 |= (vec_msgs[i].RSV3 & 0x01) << 4;
                         byte0 |= (vec_msgs[i].OPCODE & 0x0F);
                         wire_buffer.push_back(byte0);

                         uint8_t byte1 = 0;
                         byte1 |= (vec_msgs[i].MASK & 0x01) << 7;

                         uint8_t byte01 = 0;
                         byte01 |=
                             (static_cast<uint8_t>(vec_msgs[i].LENGTH) & 0x7F);
                         wire_buffer.push_back(byte01);

                         byte1 |= (static_cast<uint8_t>(
                                       vec_msgs[i].EXTENDED_LENGTH) &
                                   0x7F);
                         wire_buffer.push_back(byte1);

                         uint32_t network_sid = htonl(vec_msgs[i].py.STREAM_ID);
                         uint8_t *sid_bytes =
                             reinterpret_cast<uint8_t *>(&network_sid);
                         wire_buffer.insert(wire_buffer.end(), sid_bytes,
                                            sid_bytes + 4);

                         const uint8_t *string_bytes =
                             reinterpret_cast<const uint8_t *>(
                                 vec_msgs[i].py.payload.data());
                         wire_buffer.insert(wire_buffer.end(), string_bytes,
                                            string_bytes +
                                                vec_msgs[i].py.payload.size());

                         std::cout
                             << "--- Wire Buffer (Size: " << wire_buffer.size()
                             << ") ---" << std::endl;

                         for (size_t i = 0; i < wire_buffer.size(); ++i) {
                           // Print each byte as a 2-digit Hexadecimal value
                           std::cout << std::hex << std::setw(2)
                                     << std::setfill('0')
                                     << static_cast<int>(wire_buffer[i]) << " ";

                           // Optional: Print a newline every 8 bytes for
                           // readability
                           if ((i + 1) % 8 == 0)
                             std::cout << std::endl;
                         }

                         std::cout << std::dec
                                   << "\n----------------------------------"
                                   << std::endl;

                         boost::asio::async_write(
                             this->conn->conn_socket,
                             boost::asio::buffer(wire_buffer.data(),
                                                 wire_buffer.size()),
                             [DQ,
                              write_frame](const boost::system::error_code &ec,
                                           size_t bytes) {
                               if (!ec) {
                                 (*write_frame)(DQ);
                               }
                             });
                       }
                     }
                     boost::asio::post(this->conn->conn_socket.get_executor(),
                         [write_frame, DQ]() { (*write_frame)(DQ); });
                     // start boost post here for write frame again
                   });

               std::shared_ptr<std::function<void()>> compress_frame;

               std::shared_ptr<std::function<void()>> ws_handler;
               ws_handler = std::make_shared<
                   std::function<void()>>([this, write_frame, create_frame]() {
                 std::shared_ptr<std::deque<std::vector<wss_frame>>>
                     Write_queue =
                         std::make_shared<std::deque<std::vector<wss_frame>>>();
                 std::string message = "hello world from my wss";

                 // operation we are sending to
                 // client , this could be any
                 // other db,or calcualtion do
                 // you for your client , we will
                 // use a handler in place here
                 auto msg_frams = (*create_frame)(message, 1);
                 Write_queue->push_back(msg_frams);
                 (*write_frame)(Write_queue);
                 // ...
                 // simentaneos read and write .... and we use deques and ping &
                 // pong using timestamps before every read and write operation
                 // based on timestamp , if we have not done a read or write in
                 // the last interval we do a autumatic heartbeat if we did a
                 // read or write its fine . create_frame -> vector of frames
                 // vector of frames -> into a deque
                 // write function
                 //
                 // read continoulsy for getting the client 's message ,
                 // decompress , build message from res_frames add messages to a
                 // read deque call a client use handler for embedding the
                 // custom logic .
               });

               next();
             }}};
    std::vector<PHASE> phase_link;
    void generate_unprocessed_default_response();
    void register_phase_handler(PHASE phase) { phase_link.push_back(phase); }
    void apply_phase_handlers(
        int index = 0,
        std::function<void()> next_in_global_chain_link = nullptr) {
      auto self = shared_from_this();
      if (phase_link.size() == 0)
        return;
      if (index >= phase_link.size()) {
        next_in_global_chain_link();
        return;
      }
      auto cp = phase_link[index];
      phase_handlers[cp](
          self->conn->res, [this, index, self, next_in_global_chain_link]() {
            self->apply_phase_handlers(index + 1, next_in_global_chain_link);
          });
    };
    void write_to_client() {
      this->conn->res->send(); // rewrite this this to be more than default
                               // value , use Response class itself
    };
  };

  void listen(int init_id) {
    // Listen logic
    acceptor.async_accept(
        [init_id, this](const boost::system::error_code &error,
                        boost::asio::ip::tcp::socket peer) {
          if (!error) {
            auto new_conn = std::make_shared<Connection>(
                init_id, "",
                boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(
                    std::move(peer), this->ssl_context));
            // new_conn->conn_socket.set_verify_mode(boost::asio::ssl::verify_none);
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

  void API_HANDLER(const std::shared_ptr<Connection> &conn,
                   std::function<void()> next_in_global_chain_link) {
    boost::asio::post(
        this->io_context, [this, conn, next_in_global_chain_link] {
          // take care of the request handler for the endpoint /blah.....
          if (conn->req->path.length() != 0) {
            if (requesthandlercallback(conn->req->path, conn->req->method,
                                       conn->req, conn->res)) {
              std::cout << "Handler request processed" << std::endl;
              conn->res->res_code = Response::RESPONSE_CODES::HTTP_200;
            } else
              default_callback(conn->req, conn->res);
            conn->res->res_code = Response::RESPONSE_CODES::HTTP_404;
          }

          next_in_global_chain_link();
          // if (!conn->req->keepALive) {
          //
          //   conn->conn_socket.async_shutdown(
          //       [this, conn](const boost::system::error_code &ec) {
          //         if (!ec) {
          //           conn->conn_socket.lowest_layer().close();
          //         } else {
          //           std::cout << ec.message() << std::endl;
          //         }
          //       });
          //   connections_list.erase(std::remove_if(
          //       connections_list.begin(), connections_list.end(),
          //       [conn](const auto &n_conn) { return conn == n_conn; }));
          // }
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
            this->Initating_handler_for_Tasks(conn);
          } else {
            std::cout << "TLS_handshake failed .." + error.message()
                      << std::endl;
          }
        });
  }

  void Initating_handler_for_Tasks(const std::shared_ptr<Connection> &conn) {
    boost::asio::post(this->io_context, [conn, this]() mutable {
      this->processor_generator_handler(conn);
      if ((conn->req->request_parsed["Connection"] == "keep-alive") ||
          conn->req->keepALive && conn->conn_socket.lowest_layer().is_open()) {
        Initating_handler_for_Tasks(conn);
      } else {
        // connection termination here form the connection_list
      }
    });
  }

  // Main request-response (processor & generator) function
  using Task = std::function<void(std::function<void()>)>;

  void run_chain(std::shared_ptr<std::deque<Task>> sequence) {
    std::cout << "--------------------------------" << std::endl;
    if (sequence->empty()) {
      std::cout << "Runchain execution is done .." << std::endl;
      return;
    }
    auto current_task = std::move(sequence->front());
    sequence->pop_front();
    current_task([sequence, this]() mutable { run_chain(sequence); });
  }

  void processor_generator_handler(const std::shared_ptr<Connection> &conn) {
    auto processor = std::make_shared<Processor>(conn);
    auto generator = std::make_shared<Generator>(conn);

    processor->register_filter(Processor::FILTERS::PARSE_HEADER);

    auto sequence = std::make_shared<std::deque<Task>>();

    sequence->push_back([this, conn, processor, generator,
                         sequence](auto next_in_global_chain_link) mutable {
      processor->apply_filters(0, [this, conn, processor, generator, sequence,
                                   next_in_global_chain_link]() {
        // logic for further global_chain ...
        auto server_hosting = std::find(processor->filter_chain.begin(),
                                        processor->filter_chain.end(),
                                        Processor::FILTERS::STATIC_SERVING);
        auto sse =
            std::find(processor->filter_chain.begin(),
                      processor->filter_chain.end(), Processor::FILTERS::SSE);
        if (server_hosting != processor->filter_chain.end()) {
          std::cout << "SS" << std::endl;
          generator->register_phase_handler(Generator::PHASE::STATIC_SERVER);
          sequence->push_back([generator](auto next_in_global_chain_link) {
            generator->apply_phase_handlers(0, next_in_global_chain_link);
          });

        } else if (sse != processor->filter_chain.end()) {
          std::cout << "SSE" << std::endl;
          generator->register_phase_handler(Generator::PHASE::SSE);
          sequence->push_back([generator](auto next_in_global_chain_link) {
            generator->apply_phase_handlers(0, next_in_global_chain_link);
          });
        } else {
          std::cout << "API_H" << std::endl;
          sequence->push_back([this, conn](auto next_in_global_chain_link) {
            this->API_HANDLER(conn, next_in_global_chain_link);
          });
          generator->register_phase_handler(
              Generator::PHASE::RESPONSE_WRITE_TO_CLIENT);
          std::cout << "GEN" << std::endl;
          sequence->push_back([generator](auto next_in_global_chain_link) {
            generator->apply_phase_handlers(0, next_in_global_chain_link);
          });
        }
        next_in_global_chain_link();
      });
    });

    run_chain(sequence);
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
