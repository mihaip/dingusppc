#include <main.h>
#include <loguru.hpp>

bool init() {
    LOG_F(INFO, "init()\n");
    return true;
}

void cleanup() {
    LOG_F(INFO, "cleanup()\n");
}
