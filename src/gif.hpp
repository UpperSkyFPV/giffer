#pragma once

// This file offers a simple, very limited way to create animated GIFs directly
// in code.
//
// Those looking for particular cleverness are likely to be disappointed; it's
// pretty much a straight-ahead implementation of the GIF format with optional
// Floyd-Steinberg dithering. (It does at least use delta encoding - only the
// changed portions of each frame are saved.)
//
// So resulting files are often quite large. The hope is that it will be handy
// nonetheless as a quick and easily-integrated way for programs to spit out
// animations.
//
// Only RGBA8 is currently supported as an input format. (The alpha is ignored.)
//
// If capturing a buffer with a bottom-left origin (such as OpenGL), define
// GIF_FLIP_VERT to automatically flip the buffer data when writing the image
// (the buffer itself is unchanged.
//
// USAGE:
// Create a GifWriter struct. Pass it to GifBegin() to initialize and write the
// header. Pass subsequent frames to GifWriteFrame(). Finally, call GifEnd() to
// close the file handle and free memory.
//

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <tuple>

namespace uppr::gif {

using u8 = std::uint8_t;
using i8 = std::int8_t;
using u16 = std::uint16_t;
using i16 = std::int16_t;
using u32 = std::uint32_t;
using i32 = std::int32_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;
using usize = std::size_t;

using std::array;

using Coloru32 = std::tuple<u32, u32, u32>;
using Colori32 = std::tuple<i32, i32, i32>;

constexpr auto transparency_index = 0;

// max, min, and abs functions
template <typename T>
constexpr auto max(T l, T r) -> T {
    return l > r ? l : r;
}
template <typename T>
constexpr auto min(T l, T r) -> T {
    return l < r ? l : r;
}
template <typename T>
constexpr auto abs(T i) -> T {
    return i < 0 ? -i : i;
}

enum ColorIndex : usize { RED = 0, GREEN = 1, BLUE = 2, ALPHA = 3 };

/// Get the index of the pixel for the given color.
constexpr auto pixidx(usize i, ColorIndex color) -> usize {
    return i * 4 + color;
}

/// Get the color value of the pixel at index `i`.
constexpr auto pixat(u8 const *image, usize i, ColorIndex color) -> u8 {
    return image[pixidx(i, color)];
}

/// Get the color value of the pixel at index `i` as a 32bit unsigned integer.
constexpr auto u32_pixat(u8 const *image, usize i, ColorIndex color) -> u32 {
    return image[pixidx(i, color)];
}

/// Find the darkest pixel in an image
constexpr auto find_darkest_color(u8 const *image, usize num_pixels)
    -> Coloru32 {
    u32 r = 255;
    u32 g = 255;
    u32 b = 255;

    for (usize i{}; i < num_pixels; ++i) {
        r = min(r, u32_pixat(image, i, RED));
        g = min(g, u32_pixat(image, i, GREEN));
        b = min(b, u32_pixat(image, i, BLUE));
    }

    return {r, g, b};
}

/// Find the lightest pixel in an image
constexpr auto find_lightest_color(u8 const *image, usize num_pixels)
    -> Coloru32 {
    u32 r = 0;
    u32 g = 0;
    u32 b = 0;

    for (usize i{}; i < num_pixels; ++i) {
        r = max(r, u32_pixat(image, i, RED));
        g = max(g, u32_pixat(image, i, GREEN));
        b = max(b, u32_pixat(image, i, BLUE));
    }

    return {r, g, b};
}

constexpr auto find_subcube_average(u8 const *image, usize num_pixels)
    -> Coloru32 {
    u64 r = 0;
    u64 g = 0;
    u64 b = 0;

    for (usize i{}; i < num_pixels; ++i) {
        r += pixat(image, i, RED);
        g += pixat(image, i, GREEN);
        b += pixat(image, i, BLUE);
    }

    r += num_pixels / 2; // round to nearest
    g += num_pixels / 2;
    b += num_pixels / 2;

    r /= num_pixels;
    g /= num_pixels;
    b /= num_pixels;

    return {r, g, b};
}

constexpr auto find_largest_range(u8 const *image, usize num_pixels)
    -> Colori32 {
    int min_r = 255;
    int max_r = 0;
    int min_g = 255;
    int max_g = 0;
    int min_b = 255;
    int max_b = 0;

    for (usize i{}; i < num_pixels; ++i) {
        auto const r = pixat(image, i, RED);
        auto const g = pixat(image, i, GREEN);
        auto const b = pixat(image, i, BLUE);

        if (r > max_r) max_r = r;
        if (r < min_r) min_r = r;

        if (g > max_g) max_g = g;
        if (g < min_g) min_g = g;

        if (b > max_b) max_b = b;
        if (b < min_b) min_b = b;
    }

    return {max_r - min_r, max_g - min_g, max_b - min_b};
}

constexpr void swap_pixels(u8 *image, usize a, usize b) {
    const auto ra = pixat(image, a, RED);
    const auto ga = pixat(image, a, GREEN);
    const auto ba = pixat(image, a, BLUE);
    const auto aa = pixat(image, a, ALPHA);

    const auto rb = pixat(image, b, RED);
    const auto gb = pixat(image, b, GREEN);
    const auto bb = pixat(image, b, BLUE);
    const auto ab = pixat(image, b, ALPHA);

    image[pixidx(a, RED)] = rb;
    image[pixidx(a, GREEN)] = gb;
    image[pixidx(a, BLUE)] = bb;
    image[pixidx(a, ALPHA)] = ab;

    image[pixidx(b, RED)] = ra;
    image[pixidx(b, GREEN)] = ga;
    image[pixidx(b, BLUE)] = ba;
    image[pixidx(b, ALPHA)] = aa;
}

// just the partition operation from quicksort
constexpr auto partition(u8 *image, usize left, usize right, usize elt,
                         usize pivot_index) -> usize {
    auto const pivot_value = image[(pivot_index)*4 + elt];
    swap_pixels(image, pivot_index, right - 1);

    auto store_index = left;
    auto split = false;
    for (auto i = left; i < right - 1; ++i) {
        auto const val = image[i * 4 + elt];
        if (val < pivot_value) {
            swap_pixels(image, i, store_index);
            ++store_index;
        } else if (val == pivot_value) {
            if (split) {
                swap_pixels(image, i, store_index);
                ++store_index;
            }
            split = !split;
        }
    }

    swap_pixels(image, store_index, right - 1);

    return store_index;
}

// Perform an incomplete sort, finding all elements above and below the desired
// median
constexpr void partition_by_median(u8 *image, usize left, usize right,
                                   usize com, usize needed_center) {
    if (left < right - 1) {
        auto pivot_index = left + (right - left) / 2;
        pivot_index = partition(image, left, right, com, pivot_index);

        // Only "sort" the section of the array that contains the median
        if (pivot_index > needed_center)
            partition_by_median(image, left, pivot_index, com, needed_center);

        if (pivot_index < needed_center)
            partition_by_median(image, pivot_index + 1, right, com,
                                needed_center);
    }
}

struct Palette {
    int bit_depth;

    array<u8, 256> r;
    array<u8, 256> g;
    array<u8, 256> b;

    // k-d tree over RGB space, organized in heap fashion
    // i.e. left child of node i is node i*2, right child is node i*2+1
    // nodes 256-511 are implicitly the leaves, containing a color
    array<u8, 256> tree_split_elt;
    array<u8, 256> tree_split;

    // Creates a palette by placing all the image pixels in a k-d tree and then
    // averaging the blocks at the bottom. This is known as the "modified median
    // split" technique
    Palette(u8 const *last_frame, u8 const *next_frame, usize width,
            usize height, int bit_depth, bool build_for_dither);

    // walks the k-d tree to pick the palette entry for a desired color.
    // Takes as in/out parameters the current best color and its error -
    // only changes them if it finds a better color in its subtree.
    // this is the major hotspot in the code at the moment.
    constexpr void get_closest_pallete_color(int r, int g, int b, int &best_ind,
                                             int &best_diff, int tree_root) {
        // base case, reached the bottom of the tree
        if (tree_root > (1 << bit_depth) - 1) {
            auto const ind = tree_root - (1 << bit_depth);
            if (ind == transparency_index) return;

            // check whether this color is better than the current winner
            auto const r_err = r - this->r[ind];
            auto const g_err = g - this->g[ind];
            auto const b_err = b - this->b[ind];
            auto const diff = abs(r_err) + abs(g_err) + abs(b_err);

            if (diff < best_diff) {
                best_ind = ind;
                best_diff = diff;
            }

            return;
        }

        // take the appropriate color (r, g, or b) for this node of the k-d tree
        array comps{r, g, b};
        auto const split_comp = comps[tree_split_elt[tree_root]];

        auto const split_pos = tree_split[tree_root];
        if (split_pos > split_comp) {
            // check the left subtree
            get_closest_pallete_color(r, g, b, best_ind, best_diff,
                                      tree_root * 2);
            if (best_diff > split_pos - split_comp) {
                // cannot prove there's not a better value in the right subtree,
                // check that too
                get_closest_pallete_color(r, g, b, best_ind, best_diff,
                                          tree_root * 2 + 1);
            }
        } else {
            get_closest_pallete_color(r, g, b, best_ind, best_diff,
                                      tree_root * 2 + 1);
            if (best_diff > split_comp - split_pos) {
                get_closest_pallete_color(r, g, b, best_ind, best_diff,
                                          tree_root * 2);
            }
        }
    }

    // Builds a palette by creating a balanced k-d tree of all pixels in the
    // image
    constexpr void split(uint8_t *image, usize num_pixels, usize first_elt,
                         usize last_elt, usize split_elt, usize split_dist,
                         usize tree_node, bool build_for_dither) {
        if (last_elt <= first_elt || num_pixels == 0) return;

        // base case, bottom of the tree
        if (last_elt == first_elt + 1) {
            if (build_for_dither) {
                // Dithering needs at least one color as dark as anything
                // in the image and at least one brightest color -
                // otherwise it builds up error and produces strange artifacts
                if (first_elt == 1) {
                    // special case: the darkest color in the image
                    auto const [r, g, b] =
                        find_darkest_color(image, num_pixels);

                    this->r[first_elt] = r;
                    this->g[first_elt] = g;
                    this->b[first_elt] = b;

                    return;
                }

                if (first_elt == (1 << bit_depth) - 1) {
                    // special case: the lightest color in the image
                    auto const [r, g, b] =
                        find_lightest_color(image, num_pixels);

                    this->r[first_elt] = r;
                    this->g[first_elt] = g;
                    this->b[first_elt] = b;

                    return;
                }
            }

            // otherwise, take the average of all colors in this subcube
            auto const [r, g, b] = find_subcube_average(image, num_pixels);

            this->r[first_elt] = r;
            this->g[first_elt] = g;
            this->b[first_elt] = b;

            return;
        }

        // Find the axis with the largest range
        auto const [r_range, g_range, b_range] =
            find_largest_range(image, num_pixels);

        // and split along that axis. (incidentally, this means this isn't a
        // "proper" k-d tree but I don't know what else to call it)
        auto split_com = 1;
        if (b_range > g_range) split_com = 2;
        if (r_range > b_range && r_range > g_range) split_com = 0;

        auto const sub_pixels_a =
            num_pixels * (split_elt - first_elt) / (last_elt - first_elt);
        auto const sub_pixels_b = num_pixels - sub_pixels_a;

        partition_by_median(image, 0, num_pixels, split_com, sub_pixels_a);

        tree_split_elt[tree_node] = split_com;
        tree_split[tree_node] = image[sub_pixels_a * 4 + split_com];

        split(image, sub_pixels_a, first_elt, split_elt, split_elt - split_dist,
              split_dist / 2, tree_node * 2, build_for_dither);
        split(image + sub_pixels_a * 4, sub_pixels_b, split_elt, last_elt,
              split_elt + split_dist, split_dist / 2, tree_node * 2 + 1,
              build_for_dither);
    }

    // write a 256-color (8-bit) image palette to the file
    void write(FILE *f) const;
};

// Simple structure to write out the LZW-compressed portion of the image
// one bit at a time
struct BitStatus {
    u8 bit_index = 0; // how many bits in the partial byte written so far
    u8 byte = 0;      // current partial byte

    u32 chunk_index = 0;
    array<u8, 256> chunk; // bytes are written in here until we have 256 of
                          // them, then written to the file

    // insert a single bit
    constexpr void write_bit(u32 bit) {
        bit = bit & 1;
        bit = bit << bit_index;
        byte |= bit;

        ++bit_index;
        if (bit_index > 7) {
            // move the newly-finished byte to the chunk buffer
            chunk[chunk_index++] = byte;
            // and start a new byte
            bit_index = 0;
            byte = 0;
        }
    }

    // write all bytes so far to the file
    void write_chunk(FILE *f);

    void write_code(FILE *f, u32 code, u32 length);
};

struct Writer {
    using OwnedImage = std::unique_ptr<u8[]>;
    using File = std::unique_ptr<FILE, void (*)(FILE *f)>;

    File f = {nullptr, nullptr};
    OwnedImage old_image = nullptr;
    bool first_frame = true;

    // Creates a gif file.
    // The delay value is the time between frames in hundredths of a second -
    // note that not all viewers pay much attention to this value.
    static auto open(std::string const &filename, usize width, usize height,
                     usize delay, int bit_depth = 8, bool dither = false)
        -> std::optional<Writer>;

    // Writes out a new frame to a GIF in progress.
    // AFAIK, it is legal to use different bit depths for different frames of an
    // image - this may be handy to save bits in animations that don't change
    // much.
    auto write_frame(u8 const *image, usize width, usize height, usize delay,
                     int bit_depth = 8, bool dither = false) -> bool;

    // Writes the EOF code, closes the file handle, and frees temp memory used
    // by a GIF. Many if not most viewers will still display a GIF properly if
    // the EOF code is missing, but it's still a good idea to write it out.
    auto close() -> bool;
};
} // namespace uppr::gif
