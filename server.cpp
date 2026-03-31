#include "server.h"
#include <boost/asio/streambuf.hpp>
int main() {
  Server s("127.0.0.1", 8000);
  s.register_handler("/test", "GET",
                     [](const std::shared_ptr<Request> &req,
                        const std::shared_ptr<Response> &res) {
                       std::cout << "GET /test endpoint invovked" << std::endl;
                       res->send();
                     });
  s.register_handler("/test", "POST",
                     [](const std::shared_ptr<Request> &req,
                        const std::shared_ptr<Response> &res) {
                       std::cout << "POST /test endpoint invovked" << std::endl;
                       res->send();
                     });

  s.register_handler("/test_chunked", "POST",
                     [](const std::shared_ptr<Request> &req,
                        const std::shared_ptr<Response> &res) {
                       std::cout << "POST /test_chunked endpoint invovked"
                                 << std::endl;
                       std::cout << req->body << std::endl;
                       res->send();
                     });
  s.run();
  std::cout << "Press enter to stop" << std::endl;
  if (std::cin.get() == '\n') {
    s.stop();
    std::cout << "Server stopped" << std::endl;
  }
  return 0;
}
