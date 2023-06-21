//
// gif-h-demo.cpp
// by Charlie Tangora
// Public domain.
// Email me : ctangora -at- gmail -dot- com
//
// Shows an example usage of gif.h
//

#include "gif.hpp"

#include <cmath>

using uppr::gif::begin;
using uppr::gif::end;
using uppr::gif::usize;
using uppr::gif::write_frame;
using uppr::gif::Writer;

const int width = 256;
const int height = 256;
uint8_t image[width * height * 4];

void set_pixel(usize xx, usize yy, uint8_t red, uint8_t grn, uint8_t blu) {
    uint8_t *pixel = &image[(yy * width + xx) * 4];
    pixel[0] = red;
    pixel[1] = blu;
    pixel[2] = grn;
    pixel[3] = 255; // no alpha for this demo
}

void set_pixel_float(usize xx, usize yy, float fred, float fgrn, float fblu) {
    // convert float to unorm
    auto const red = static_cast<uint8_t>(roundf(255.0F * fred));
    auto const grn = static_cast<uint8_t>(roundf(255.0F * fgrn));
    auto const blu = static_cast<uint8_t>(roundf(255.0F * fblu));

    set_pixel(xx, yy, red, grn, blu);
}

auto main(int argc, const char *argv[]) -> int {
    const char *filename = "./my_gif.gif";
    if (argc > 1) { filename = argv[1]; }

    // Create a gif
    Writer writer;
    begin(writer, filename, width, height, 2, 8, true);

    for (usize frame{}; frame < 256; ++frame) {
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
        printf("Writing frame %zu...\n", frame);
        write_frame(writer, image, width, height, 2, 8, true);
    }

    // Write EOF
    end(writer);

    return 0;
}
