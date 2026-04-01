g++ -std=c++17 test_client.cpp -o client \
-I/usr/local/include \
-I/usr/local/opt/openssl@3/include \
-L/usr/local/lib \
-L/usr/local/opt/openssl@3/lib \
-lssl -lcrypto -pthread 

