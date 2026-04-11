# PLAN
- Plan of action is that we first write the structur of functions we want to write without implementation and required TTD on my own and then fill up one by one , but write TTD first , it helps making sure that we know exactly what we want to know and what it needs to solves .
- Write a simple client lib in cpp and port to js .
- HTTP 1.1
- Websocket
- transfer encoding 
- file transfer 
- static hosting 
- connection pooling and custom allocater 
- Cache implementation from nginx 
- Stress Testing 
- Webrtc
- HTTP 2
- Custom messaging protocol
- Video streaming using http2 or better algos using DL for ADP
- BitTorrent support & P2P for gaming system (mainly UDP) .

## SUBNOTES
- [x] Refactor current simple navie (request->parser->server API callback ->response) code flow to modular (Request -> Filters module -> Upstream API module -> Generator Module)
- [x] HTTP /1.1 (simple stuff :| )
- [x] TLS handshake Testing ...
- [ ] internal Caching inspired by nginx 
- [ ] Complete Test suite for SSE, STATIC HOSTING .
- [x] Transfer encoding 
- [] File transfer a large file using merkle using transfer encoding ... deferred for now because this reqiures me to write a separate client just for this as merkle tree compliant is an issue
- [x] SSE for simple stream of text notifications
- [x] Static file serving of assests .
