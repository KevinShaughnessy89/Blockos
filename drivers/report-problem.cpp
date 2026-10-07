#include "report-problem.hpp"
#include "io.hpp"

/*
 * Early diagnostics sink: writes "[LEVEL] component: message" to COM1.
 * Port I/O only, so it is safe before and after ExitBootServices.
 */
static void com1_putc(char c)
{
    for (int i = 0; i < 100000; ++i) {
        if (io::inb(0x3F8 + 5) & 0x20)
            break;
    }
    io::outb(0x3F8, (uint8_t) c);
}

static void com1_puts(const char* s)
{
    if (!s)
        return;
    while (*s)
        com1_putc(*s++);
}

void ReportProblem(ProblemLevel level, const char* component, const char* message)
{
    const char* tag = "INFO";
    if (level == ProblemLevel::WARNING)
        tag = "WARN";
    else if (level == ProblemLevel::CRITICAL)
        tag = "CRIT";

    com1_putc('[');
    com1_puts(tag);
    com1_puts("] ");
    com1_puts(component);
    com1_puts(": ");
    com1_puts(message);
    com1_puts("\r\n");
}
