#include <boost/version.hpp>
#include <iostream>

int main() {
  std::cout << "Boost version: " << BOOST_VERSION / 100000 // Major version
            << "." << BOOST_VERSION / 100 % 1000           // Minor version
            << "." << BOOST_VERSION % 100                  // Patch level
            << std::endl;
  return 0;
}
