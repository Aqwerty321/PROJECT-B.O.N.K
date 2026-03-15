#include <iostream>

// Verify Boost linkage
#include <boost/version.hpp>

// Verify nlohmann/json header resolution
#include <nlohmann/json.hpp>

// Verify ObjectBox header resolution (do not initialize)
#include <objectbox.h>

// Julia bridge
#include <jluna.hpp>

int main()
{
    jluna::initialize();

    std::cout << "PROJECT B.O.N.K. SYSTEM ONLINE" << std::endl;

    std::cout << "Boost version: "
              << BOOST_LIB_VERSION
              << std::endl;

    return 0;
}
