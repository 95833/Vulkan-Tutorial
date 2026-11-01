#include "lessons.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#ifndef LAB_SHADER_DIR
#define LAB_SHADER_DIR "shaders"
#endif

static void print_help() {
    std::cout <<
        "Vulkan 1.3 self-study experiments, 512x512 offscreen\n"
        "Usage: vulkan_lessons --lesson NAME --output out/NAME.ppm [options]\n"
        "Lessons: probe clear triangle vertex camera texture depth mips layers frames\n"
        "  --validation       require Khronos layer; explicitly enable synchronization validation\n"
        "  --shader-dir DIR   override compiled SPIR-V directory\n"
        "  --shift X          camera: scene NDC translation; frames: extra translation\n"
        "  --lod L            mips: explicit LOD 0..6 (nearest mip selection)\n"
        "  --layer I          layers: array layer 0, 1 or 2\n"
        "  --no-depth         depth: disable depth test for the comparison\n"
        "  --frames N         frames: count 1..10000, default 9\n"
        "  --inflight N       frames: independently owned slots, 1 or 2; default 1\n"
        "  --help             print this help\n";
}

int main(int argc, char** argv) {
    lab::Context context;
    try {
        lab::Options options;
        options.shader_directory = LAB_SHADER_DIR;
        bool explicit_output = false;
        auto value = [&](int& i) -> std::string {
            if (++i >= argc) throw std::runtime_error("Missing value after command-line option.");
            return argv[i];
        };
        auto parse_float = [](const std::string& text) {
            size_t consumed = 0;
            const float result = std::stof(text, &consumed);
            if (consumed != text.size() || !std::isfinite(result)) throw std::runtime_error("Expected a finite number: " + text);
            return result;
        };
        auto parse_uint = [](const std::string& text) {
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("Expected a nonnegative integer: " + text);
            const auto result = std::stoull(text);
            if (result > UINT32_MAX) throw std::runtime_error("Integer too large: " + text);
            return static_cast<uint32_t>(result);
        };
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") { print_help(); return 0; }
            if (arg == "--lesson") options.lesson = value(i);
            else if (arg == "--output") { options.output = value(i); explicit_output = true; }
            else if (arg == "--shader-dir") options.shader_directory = value(i);
            else if (arg == "--validation") options.validation = true;
            else if (arg == "--no-depth") options.no_depth = true;
            else if (arg == "--shift") options.shift = parse_float(value(i));
            else if (arg == "--lod") options.lod = parse_float(value(i));
            else if (arg == "--layer") options.layer = parse_uint(value(i));
            else if (arg == "--frames") options.frames = parse_uint(value(i));
            else if (arg == "--inflight") options.inflight = parse_uint(value(i));
            else throw std::runtime_error("Unknown option: " + arg);
        }
        using Entry = void (*)(lab::Context&, const lab::Options&);
        const std::unordered_map<std::string, Entry> lessons = {
            {"probe", lab::run_probe}, {"clear", lab::run_clear}, {"triangle", lab::run_triangle},
            {"vertex", lab::run_vertex}, {"camera", lab::run_camera}, {"texture", lab::run_texture},
            {"depth", lab::run_depth}, {"mips", lab::run_mips}, {"layers", lab::run_layers}, {"frames", lab::run_frames}
        };
        const auto lesson = lessons.find(options.lesson);
        if (lesson == lessons.end()) throw std::runtime_error("Unknown lesson: " + options.lesson);
        if (!explicit_output) options.output = "out/" + options.lesson + ".ppm";
        if (options.lod < 0 || options.lod > 6) throw std::runtime_error("--lod must be in [0,6].");
        if (options.layer > 2) throw std::runtime_error("--layer must be 0, 1 or 2.");
        if (options.frames < 1 || options.frames > 10000) throw std::runtime_error("--frames must be in [1,10000].");
        if (options.inflight != 1 && options.inflight != 2) throw std::runtime_error("--inflight must be 1 or 2.");
        if (options.no_depth && options.lesson != "depth") throw std::runtime_error("--no-depth applies only to lesson depth.");
        lab::create_context(context, options.validation, options.lesson != "probe");
        std::cout << "Lesson: " << options.lesson << "\n";
        lesson->second(context, options);
        // 各课局部资源已销毁；debug callback在这些销毁期间保持有效。
        std::cout << "Validation summary: errors=" << context.validation_errors
                  << "; warnings=" << context.validation_warnings << "\n";
        return context.validation_errors ? 2 : 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << "\n";
        return 1;
    }
}
