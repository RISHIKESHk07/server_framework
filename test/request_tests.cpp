#include <bits/stdc++.h>
#include <cstddef>
#include <string>
using namespace std;
std::map<string, string> ParsedResourceMap;
void request_parser(std::string request_content) {
  // request_parser for query string , content-length ,version
  std::istringstream iss(request_content);
  std::string request_line;
  std::getline(iss, request_line); // first line: GET /index?x=1 HTTP/1.1

  std::string full_path;
  std::string method;
  std::string version;
  std::string path;
  std::istringstream rl(request_line);
  rl >> method >> full_path >> version;
  // Extract path and query
  auto qpos = full_path.find("?");
  path = (qpos != std::string::npos) ? full_path.substr(0, qpos) : full_path;
  cout << "--" << method << "--" << full_path << "---" << version << endl;
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
        request_line.substr(e_pos + 2, request_line.length() - e_pos - 2);
    cout << request_line.substr(0, e_pos) << "<-->"
         << request_line
                .substr(e_pos + 2, request_line.length() - e_pos - 2 - 1)
                .size()
         << endl;
    if (request_line.substr(0, e_pos) == "Transfer-Encoding" &&
        request_line.substr(e_pos + 2, request_line.length() - e_pos - 2 - 1) ==
            "chunked") {
      te_flag = 1;
    }
    if (request_line.substr(0, e_pos) == "Content-Type" &&
        request_line.substr(e_pos + 2, request_line.length() - e_pos - 2) ==
            "text/event-stream") {
      sse_flag = 1;
    }
  }
  // removing the rndline to body
  std::getline(iss, request_line);

  if (te_flag) {
    std::cout << "ct" << std::endl;
  }
}

int main() {
  cout << "Test the parsing :" << endl;
  std::string dummy = "POST /test_chunked HTTP/1.1\r\n"
                      "Host: localhost:8000\r\n"
                      "User-Agent: curl/7.81.0\r\n"
                      "Accept: */*\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "Content-Type: text/plain\r\n"
                      "\r\n"
                      "4\r\n"
                      "qqq\r\n"
                      "\r\n"
                      "0\r\n"
                      "\r\n";
  request_parser(dummy);
}
