#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

// A CPU-only starting point for Chapter 1. No Vulkan is used here.
int main(int argc, char** argv) {
    try {
        const std::filesystem::path output =
            argc > 1 ? argv[1] : "out/cpu.ppm";
        constexpr std::uint32_t width = 512;
        constexpr std::uint32_t height = 512;
        const std::array<std::uint8_t, 3> color{6, 9, 14};
        std::vector<std::uint8_t> pixels(width * height * 3);

        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t offset = (y * width + x) * 3;
                pixels[offset + 0] = color[0];
                pixels[offset + 1] = color[1];
                pixels[offset + 2] = color[2];
            }
        }

        if (!output.parent_path().empty()) {
            std::filesystem::create_directories(output.parent_path());
        }
        std::ofstream file(output, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open output file");
        file << "P6\n" << width << ' ' << height << "\n255\n";
        file.write(reinterpret_cast<const char*>(pixels.data()),
                   static_cast<std::streamsize>(pixels.size()));
        if (!file) throw std::runtime_error("Cannot write output file");
        std::cout << "CPU image: " << output.string() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
