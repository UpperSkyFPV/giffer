#include "CLI11.hpp"
#include "gif.hpp"
#include "stb_image.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <string>

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;
using uppr::gif::u8;
using uppr::gif::usize;
using uppr::gif::Writer;

auto example(std::string const &filename, int delay, int bit_depth) -> int {
    const auto width = 512UL;
    const auto height = 512UL;
    std::array<u8, width * height * 4> image;

    auto set_pixel = [&](usize x, usize y, u8 red, u8 green, u8 blue) {
        uint8_t *pixel = &image[(y * width + x) * 4];
        pixel[0] = red;
        pixel[1] = blue;
        pixel[2] = green;
        pixel[3] = 255; // no alpha for this demo
    };

    auto set_pixel_float = [&](usize xx, usize yy, float fred, float fgrn,
                               float fblu) {
        // convert float to unorm
        auto const red = static_cast<u8>(roundf(255.0F * fred));
        auto const grn = static_cast<u8>(roundf(255.0F * fgrn));
        auto const blu = static_cast<u8>(roundf(255.0F * fblu));

        set_pixel(xx, yy, red, grn, blu);
    };

    auto start = steady_clock::now();

    // Create a gif
    auto writer_ = Writer::open(filename, width, height, delay, bit_depth, true);
    if (!writer_) {
        fprintf(stderr, "Error opening output file: %s\n", filename.c_str());
        return 1;
    }

    auto writer = std::move(*writer_);

    auto const total_frames = 256;
    for (usize frame{}; frame < total_frames; ++frame) {
        // Make an image, somehow
        // this is the default shadertoy - credit to shadertoy.com
        auto const tt = static_cast<float>(frame) * 3.14159F * 2 / 255.0F;
        for (usize y{}; y < height; ++y) {
            for (usize x{}; x < width; ++x) {
                float fx = static_cast<float>(x) / width;
                float fy = static_cast<float>(y) / height;

                float red = 0.5F + 0.5F * cosf(tt + fx);
                float grn = 0.5F + 0.5F * cosf(tt + fy + 2.F);
                float blu = 0.5F + 0.5F * cosf(tt + fx + 4.F);

                set_pixel_float(x, y, red, grn, blu);
            }
        }

        // Write the frame to the gif
        auto const p =
            static_cast<double>(frame) / static_cast<double>(total_frames);
        printf("Writing frame %zu/%d... (%.02f%%)\r", frame, total_frames,
               p * 100);
        fflush(stdout);
        writer.write_frame(image.data(), width, height, delay, bit_depth, true);
    }

    auto end = steady_clock::now();
    auto delta = duration_cast<milliseconds>(end - start).count();
    printf("\ndone %lds (%.02fms/frame)\n", delta / 1000,
           static_cast<double>(delta) / static_cast<double>(total_frames));

    // Write EOF (called on destructor)
    // writer.close();

    return 0;
}

auto main(int argc, const char *argv[]) -> int {
    CLI::App app{"giffer GIF maker"};

    std::vector<std::string> input_files;
    app.add_option("-i,--input-files", input_files,
                   "Name of the file to use as input in the conversion");

    std::string output_file;
    app.add_option("-o,--output-file", output_file,
                   "Name of the file to generate")
        ->default_val("out.gif");

    int delay;
    app.add_option("--delay", delay, "Delay in between GIF frames")
        ->default_val(2);

    int bit_depth;
    app.add_option("--bit-depth", bit_depth,
                   "Bit depth to use in the output image")
        ->default_val(8);

    bool dither = false;
    app.add_flag("--dither", dither,
                 "Dither the image instead of performing a threshold")
        ->default_val(false);

    bool gen_example = false;
    app.add_flag("--gen-example", gen_example, "Generate an example GIF file")
        ->default_val(false);

    bool numeric_sort = false;
    app.add_flag(
           "--numeric-sort", numeric_sort,
           "Try to find a number in all filenames and sort the list by it")
        ->default_val(false);

    CLI11_PARSE(app, argc, argv);

    if (gen_example) return example(output_file, delay, bit_depth);

    if (input_files.empty()) {
        fprintf(stderr, "--input-files requires at least one argument\n");
        return 1;
    }

    if (numeric_sort) {
        std::sort(input_files.begin(), input_files.end(),
                  [&](std::string const &a, std::string const &b) {
                      static constexpr auto const nums = "0123456789";
                      auto na = a.substr(a.find_first_of(nums));
                      na = na.substr(0, na.find_first_not_of(nums));

                      auto nb = b.substr(b.find_first_of(nums));
                      nb = nb.substr(0, nb.find_first_not_of(nums));

                      usize aa;
                      usize bb;

                      std::from_chars(na.data(), na.data() + na.size(), aa);
                      std::from_chars(nb.data(), nb.data() + nb.size(), bb);

                      return bb > aa;
                  });
    }

    auto start = steady_clock::now();

    auto it = input_files.begin();
    int w;
    int h;
    int n;
    auto data = std::unique_ptr<stbi_uc[], void (*)(stbi_uc *)>{
        stbi_load(it->c_str(), &w, &h, &n, 4),
        [](stbi_uc *a) { stbi_image_free(a); }};
    if (!data) {
        fprintf(stderr, "Error opening first input file: %s\n", it->c_str());
        return 1;
    }

    // Create a gif
    auto writer_ =
        Writer::open(output_file, w, h, delay, bit_depth, !dither);
    if (!writer_) {
        fprintf(stderr, "Error opening output file: %s\n", output_file.c_str());
        return 1;
    }

    auto writer = std::move(*writer_);
    auto frame = 0;
    auto const total_frames = input_files.size();
    writer.write_frame(data.get(), w, h, delay, bit_depth, !dither);

    for (it++, frame++; it != input_files.end(); it++, frame++) {
        data = std::unique_ptr<stbi_uc[], void (*)(stbi_uc *)>{
            stbi_load(it->c_str(), &w, &h, &n, 4),
            [](stbi_uc *a) { stbi_image_free(a); }};

        if (!data) {
            fprintf(stderr, "Error opening input file: %s\n", it->c_str());
            return 1;
        }

        auto const p =
            static_cast<double>(frame) / static_cast<double>(total_frames);
        printf("Writing frame %d/%zu... (%.02f%%)\r", frame, total_frames,
               p * 100);
        fflush(stdout);
        writer.write_frame(data.get(), w, h, delay, bit_depth, !dither);
    }

    auto end = steady_clock::now();
    auto delta = duration_cast<milliseconds>(end - start).count();
    printf("\ndone %lds (%.02fms/frame)\n", delta / 1000,
           static_cast<double>(delta) /
               static_cast<double>(input_files.size()));

    return 0;
}
