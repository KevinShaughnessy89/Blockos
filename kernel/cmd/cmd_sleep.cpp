#include "command.hpp"
#include "drivers/power.hpp"
#include <iostream>

namespace blockos::cmd {

extern "C" int sleep_main(int argc, char** argv)
{
    const char* mode = (argc > 1 && argv[1]) ? argv[1] : "sleep";

    if (!power::is_available()) {
        std::cout << "BlockOS power management is unavailable\n";
        return 1;
    }

    if (mode[0] == 's' && mode[1] == 'h' && mode[2] == 'u' &&
        mode[3] == 't' && mode[4] == 'd' && mode[5] == 'o' && mode[6] == 'w' &&
        mode[7] == 'n' && mode[8] == '\0') {
        std::cout << "BlockOS: syncing storage and shutting down\n";
        if (!power::shutdown()) {
            std::cout << "BlockOS: ACPI S5 shutdown is unavailable\n";
            return 1;
        }
        return 0;
    }

    std::cout << "BlockOS: syncing storage and entering ACPI sleep\n";
    if (!power::sleep()) {
        std::cout << "BlockOS: ACPI S3 sleep is unavailable\n";
        return 1;
    }
    return 0;
}

} // namespace blockos::cmd
