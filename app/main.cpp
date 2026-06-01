#include <iostream>

#include "indusscope/protocol/version.h"
#include "indusscope/core/version.h"
#include "indusscope/ui/version.h"

int main() {
    std::cout << indusscope::protocol::version() << std::endl;
    std::cout << indusscope::core::version() << std::endl;
    std::cout << indusscope::ui::version() << std::endl;
    return 0;
}
