// A small C++ file for the test of larry code.
#include <string>
#include "larry/json.hpp"

namespace sample {

struct Reader {
    std::string path;
};

int count_words(const std::string& text) {
    return static_cast<int>(text.size());
}

std::string read_file(const std::string& path) {
    Reader reader{path};
    return std::to_string(count_words(reader.path));
}

}  // namespace sample
