#include "runtime/cli.hpp"

#include <iostream>

int main(int argc, char** argv) {
    return bastion::run_cli(argc, argv, std::cout, std::cerr);
}

