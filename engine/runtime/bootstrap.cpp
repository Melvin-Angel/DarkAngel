#include <darkangel/bootstrap.hpp>
#include <darkangel/foundation.hpp>
#include <iostream>
namespace darkangel {
int bootstrap() {
    std::cout << build_identity() << " profile=M0-headless\n";
    diagnostic("bootstrap complete");
    return 0;
}
}
