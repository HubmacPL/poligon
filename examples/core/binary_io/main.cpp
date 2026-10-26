// Example: write values, an array and a string to bytes and read them back.
// This is how cooked assets and land patches are stored.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "poligon/core/binary_io.h"

int main() {
    const std::string header = "demo 1\n";
    poligon::BinaryWriter writer;
    writer.data.assign(header.begin(), header.end());  // a header line, checked by Header()
    writer.Raw(std::int32_t{42});
    writer.Array(std::vector<float>{1.5f, 2.5f, 3.5f});
    writer.String("hello");
    std::printf("written: %zu bytes\n", writer.data.size());

    poligon::BinaryReader reader(writer.data.data(), writer.data.size());
    std::int32_t number = 0;
    std::vector<float> values;
    std::string text;
    reader.Header(header.c_str());
    reader.Raw(number);
    reader.Array(values);
    reader.String(text);
    std::printf("read: %d, %zu values, \"%s\", ok %d\n", number, values.size(), text.c_str(), reader.IsOk());

    // Reading past the end fails instead of reading garbage.
    std::int32_t extra = 0;
    const bool read_extra = reader.Raw(extra);
    std::printf("past the end: read %d, ok %d\n", read_extra, reader.IsOk());
    return (number == 42 && values.size() == 3 && text == "hello" && !reader.IsOk()) ? 0 : 1;
}
