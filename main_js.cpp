#include <main.h>
#include <loguru.hpp>
#include <emscripten.h>
#include <iostream>
#include <string>

bool init() {
    LOG_F(INFO, "init()\n");

    loguru::set_fatal_handler([](const loguru::Message& message) {
        std::cerr << message.preamble << message.indentation << message.prefix << message.message << std::endl;
        // The abort() call that follows this handler will not have a detailed message, so report it here.
        std::string js_message = std::string(message.preamble) + message.prefix + message.message;
        EM_ASM_({ workerApi.setAbortError(UTF8ToString($0)); }, js_message.c_str());
    });
    return true;
}

void cleanup() {
    LOG_F(INFO, "cleanup()\n");
}
