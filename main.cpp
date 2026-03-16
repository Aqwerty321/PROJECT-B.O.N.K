#include <iostream>

// Verify Boost linkage
#include <boost/version.hpp>

// Use simdjson for JSON parsing (header only usage in this file)
#include <simdjson.h>

// Julia bridge
#include <jluna.hpp>

int main() {
  jluna::initialize();

  std::cout << "CASCADE (Project BONK) SYSTEM ONLINE" << std::endl;

  std::cout << "Boost version: " << BOOST_LIB_VERSION << std::endl;

  return 0;
}
