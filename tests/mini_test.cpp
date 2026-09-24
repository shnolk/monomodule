#include "mini_test.h"
#include <cstring>
#include <exception>
std::vector<mt::Case>& mt::cases() { static std::vector<Case> c; return c; }
int main(int argc, char** argv)
{
    int failed = 0, ran = 0, skipped = 0;
    for (auto& c : mt::cases()) {
        if (argc > 1 && std::strstr(c.name, argv[1]) == nullptr) continue;
        ++ran;
        try { c.fn(); std::printf("[ OK ] %s\n", c.name); }
        catch (const mt::Skip& s) { ++skipped; std::printf("[SKIP] %s (%s)\n", c.name, s.why.c_str()); }
        catch (const mt::Failure& f) { ++failed; std::printf("[FAIL] %s\n       %s\n", c.name, f.msg.c_str()); }
        catch (const std::exception& e) { ++failed; std::printf("[FAIL] %s\n       exception: %s\n", c.name, e.what()); }
    }
    std::printf("%d/%d passed, %d skipped\n", ran - failed - skipped, ran - skipped, skipped);
    return failed ? 1 : 0;
}
