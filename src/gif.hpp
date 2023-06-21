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

constexpr auto transparency_index = 0;

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
};

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

// walks the k-d tree to pick the palette entry for a desired color.
// Takes as in/out parameters the current best color and its error -
// only changes them if it finds a better color in its subtree.
// this is the major hotspot in the code at the moment.
constexpr void get_closest_pallete_color(Palette const &pal, int r, int g,
                                         int b, int &best_ind, int &best_diff,
                                         int tree_root) {
    // base case, reached the bottom of the tree
    if (tree_root > (1 << pal.bit_depth) - 1) {
        auto const ind = tree_root - (1 << pal.bit_depth);
        if (ind == transparency_index) return;

        // check whether this color is better than the current winner
        auto const r_err = r - pal.r[ind];
        auto const g_err = g - pal.g[ind];
        auto const b_err = b - pal.b[ind];
        auto const diff = abs(r_err) + abs(g_err) + abs(b_err);

        if (diff < best_diff) {
            best_ind = ind;
            best_diff = diff;
        }

        return;
    }

    // take the appropriate color (r, g, or b) for this node of the k-d tree
    array comps{r, g, b};
    auto const splitComp = comps[pal.tree_split_elt[tree_root]];

    auto const splitPos = pal.tree_split[tree_root];
    if (splitPos > splitComp) {
        // check the left subtree
        get_closest_pallete_color(pal, r, g, b, best_ind, best_diff,
                                  tree_root * 2);
        if (best_diff > splitPos - splitComp) {
            // cannot prove there's not a better value in the right subtree,
            // check that too
            get_closest_pallete_color(pal, r, g, b, best_ind, best_diff,
                                      tree_root * 2 + 1);
        }
    } else {
        get_closest_pallete_color(pal, r, g, b, best_ind, best_diff,
                                  tree_root * 2 + 1);
        if (best_diff > splitComp - splitPos) {
            get_closest_pallete_color(pal, r, g, b, best_ind, best_diff,
                                      tree_root * 2);
        }
    }
}

constexpr void swap_pixels(u8 *image, usize a, usize b) {
    const auto ra = image[a * 4];
    const auto ga = image[a * 4 + 1];
    const auto ba = image[a * 4 + 2];
    const auto aa = image[a * 4 + 3];

    const auto rb = image[b * 4];
    const auto gb = image[b * 4 + 1];
    const auto bb = image[b * 4 + 2];
    const auto ab = image[a * 4 + 3];

    image[a * 4] = rb;
    image[a * 4 + 1] = gb;
    image[a * 4 + 2] = bb;
    image[a * 4 + 3] = ab;

    image[b * 4] = ra;
    image[b * 4 + 1] = ga;
    image[b * 4 + 2] = ba;
    image[b * 4 + 3] = aa;
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

constexpr auto find_darkest_color(u8 *image, usize num_pixels)
    -> std::tuple<u32, u32, u32> {
    u32 r = 255;
    u32 g = 255;
    u32 b = 255;

    for (usize i{}; i < num_pixels; ++i) {
        r = min(r, static_cast<u32>(image[i * 4 + 0]));
        g = min(g, static_cast<u32>(image[i * 4 + 1]));
        b = min(b, static_cast<u32>(image[i * 4 + 2]));
    }

    return {r, g, b};
}

constexpr auto find_lightest_color(u8 const *image, usize num_pixels)
    -> std::tuple<u32, u32, u32> {
    u32 r = 0;
    u32 g = 0;
    u32 b = 0;

    for (usize i{}; i < num_pixels; ++i) {
        r = max(r, static_cast<u32>(image[i * 4 + 0]));
        g = max(g, static_cast<u32>(image[i * 4 + 1]));
        b = max(b, static_cast<u32>(image[i * 4 + 2]));
    }

    return {r, g, b};
}

constexpr auto find_subcube_average(u8 const *image, usize num_pixels)
    -> std::tuple<u32, u32, u32> {
    u64 r = 0;
    u64 g = 0;
    u64 b = 0;

    for (usize i{}; i < num_pixels; ++i) {
        r += image[i * 4 + 0];
        g += image[i * 4 + 1];
        b += image[i * 4 + 2];
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
    -> std::tuple<int, int, int> {
    int min_r = 255;
    int max_r = 0;
    int min_g = 255;
    int max_g = 0;
    int min_b = 255;
    int max_b = 0;

    for (usize i{}; i < num_pixels; ++i) {
        auto const r = image[i * 4 + 0];
        auto const g = image[i * 4 + 1];
        auto const b = image[i * 4 + 2];

        if (r > max_r) max_r = r;
        if (r < min_r) min_r = r;

        if (g > max_g) max_g = g;
        if (g < min_g) min_g = g;

        if (b > max_b) max_b = b;
        if (b < min_b) min_b = b;
    }

    return {max_r - min_r, max_g - min_g, max_b - min_b};
}

// Builds a palette by creating a balanced k-d tree of all pixels in the image
constexpr void split_pallete(uint8_t *image, usize num_pixels, usize first_elt,
                             usize last_elt, usize split_elt, usize split_dist,
                             usize tree_node, bool build_for_dither,
                             Palette &pal) {
    if (last_elt <= first_elt || num_pixels == 0) return;

    // base case, bottom of the tree
    if (last_elt == first_elt + 1) {
        if (build_for_dither) {
            // Dithering needs at least one color as dark as anything
            // in the image and at least one brightest color -
            // otherwise it builds up error and produces strange artifacts
            if (first_elt == 1) {
                // special case: the darkest color in the image
                auto const [r, g, b] = find_darkest_color(image, num_pixels);

                pal.r[first_elt] = r;
                pal.g[first_elt] = g;
                pal.b[first_elt] = b;

                return;
            }

            if (first_elt == (1 << pal.bit_depth) - 1) {
                // special case: the lightest color in the image
                auto const [r, g, b] = find_lightest_color(image, num_pixels);

                pal.r[first_elt] = r;
                pal.g[first_elt] = g;
                pal.b[first_elt] = b;

                return;
            }
        }

        // otherwise, take the average of all colors in this subcube
        auto const [r, g, b] = find_subcube_average(image, num_pixels);

        pal.r[first_elt] = r;
        pal.g[first_elt] = g;
        pal.b[first_elt] = b;

        return;
    }

    // Find the axis with the largest range
    auto const [r_range, g_range, b_range] =
        find_largest_range(image, num_pixels);

    // and split along that axis. (incidentally, this means this isn't a
    // "proper" k-d tree but I don't know what else to call it)
    auto splitCom = 1;
    if (b_range > g_range) splitCom = 2;
    if (r_range > b_range && r_range > g_range) splitCom = 0;

    auto const sub_pixels_a =
        num_pixels * (split_elt - first_elt) / (last_elt - first_elt);
    auto const sub_pixels_b = num_pixels - sub_pixels_a;

    partition_by_median(image, 0, num_pixels, splitCom, sub_pixels_a);

    pal.tree_split_elt[tree_node] = (uint8_t)splitCom;
    pal.tree_split[tree_node] = image[sub_pixels_a * 4 + splitCom];

    split_pallete(image, sub_pixels_a, first_elt, split_elt,
                  split_elt - split_dist, split_dist / 2, tree_node * 2,
                  build_for_dither, pal);
    split_pallete(image + sub_pixels_a * 4, sub_pixels_b, split_elt, last_elt,
                  split_elt + split_dist, split_dist / 2, tree_node * 2 + 1,
                  build_for_dither, pal);
}

// Finds all pixels that have changed from the previous image and
// moves them to the fromt of th buffer.
// This allows us to build a palette optimized for the colors of the
// changed pixels only.
auto pick_changed_pixels(u8 const *last_frame, u8 *frame, usize num_pixels)
    -> int {
    auto num_changed = 0;
    auto wit = frame;

    for (usize i{}; i < num_pixels; ++i) {
        if (last_frame[0] != frame[0] || last_frame[1] != frame[1] ||
            last_frame[2] != frame[2]) {
            wit[0] = frame[0];
            wit[1] = frame[1];
            wit[2] = frame[2];
            ++num_changed;
            wit += 4;
        }
        last_frame += 4;
        frame += 4;
    }

    return num_changed;
}

// Creates a palette by placing all the image pixels in a k-d tree and then
// averaging the blocks at the bottom. This is known as the "modified median
// split" technique
void make_pallete(u8 const *last_frame, u8 const *next_frame, usize width,
                  usize height, int bit_depth, bool build_for_dither,
                  Palette &pal) {
    pal.bit_depth = bit_depth;

    // SplitPalette is destructive (it sorts the pixels by color) so
    // we must create a copy of the image for it to destroy
    auto const image_size = width * height * 4;
    auto destroyable_image = std::make_unique<u8[]>(image_size);
    memcpy(destroyable_image.get(), next_frame, image_size);

    auto numPixels = width * height;
    if (last_frame)
        numPixels =
            pick_changed_pixels(last_frame, destroyable_image.get(), numPixels);

    const auto last_elt = 1 << bit_depth;
    const auto split_elt = last_elt / 2;
    const auto split_dist = split_elt / 2;

    split_pallete(destroyable_image.get(), numPixels, 1, last_elt, split_elt,
                  split_dist, 1, build_for_dither, pal);

    // add the bottom node for the transparency index
    pal.tree_split[1 << (bit_depth - 1)] = 0;
    pal.tree_split_elt[1 << (bit_depth - 1)] = 0;

    pal.r[0] = pal.g[0] = pal.b[0] = 0;
}

// Implements Floyd-Steinberg dithering, writes palette value to alpha
void dither_image(u8 const *last_frame, u8 const *next_frame, u8 *out_frame,
                  usize width, usize height, Palette &pal) {
    auto const num_pixels = width * height;

    // quantPixels initially holds color*256 for all pixels
    // The extra 8 bits of precision allow for sub-single-color error values
    // to be propagated
    auto quant_pixels = std::make_unique<i32[]>(num_pixels * 4);

    for (usize i{}; i < num_pixels * 4; ++i) {
        quant_pixels[i] = static_cast<int32_t>(next_frame[i]) * 256;
    }

    for (usize y{}; y < height; ++y) {
        for (usize x{}; x < width; ++x) {
            auto const next_pix = quant_pixels.get() + 4 * (y * width + x);
            auto const last_pix =
                last_frame ? last_frame + 4 * (y * width + x) : nullptr;

            // Compute the colors we want (rounding to nearest)
            auto const rr = (next_pix[0] + 127) / 256;
            auto const gg = (next_pix[1] + 127) / 256;
            auto const bb = (next_pix[2] + 127) / 256;

            // if it happens that we want the color from last frame, then just
            // write out a transparent pixel
            if (last_frame && last_pix[0] == rr && last_pix[1] == gg &&
                last_pix[2] == bb) {
                next_pix[0] = rr;
                next_pix[1] = gg;
                next_pix[2] = bb;
                next_pix[3] = transparency_index;
                continue;
            }

            auto best_diff = 1000000;
            auto best_ind = transparency_index;

            // Search the palete
            get_closest_pallete_color(pal, rr, gg, bb, best_ind, best_diff, 1);

            // Write the result to the temp buffer
            auto const r_err =
                next_pix[0] - static_cast<int32_t>(pal.r[best_ind]) * 256;
            auto const g_err =
                next_pix[1] - static_cast<int32_t>(pal.g[best_ind]) * 256;
            auto const b_err =
                next_pix[2] - static_cast<int32_t>(pal.b[best_ind]) * 256;

            next_pix[0] = pal.r[best_ind];
            next_pix[1] = pal.g[best_ind];
            next_pix[2] = pal.b[best_ind];
            next_pix[3] = best_ind;

            // Propagate the error to the four adjacent locations
            // that we haven't touched yet
            auto const quantloc_7 = y * width + x + 1;
            auto const quantloc_3 = y * width + width + x - 1;
            auto const quantloc_5 = y * width + width + x;
            auto const quantloc_1 = y * width + width + x + 1;

            if (quantloc_7 < num_pixels) {
                auto pix7 = quant_pixels.get() + 4 * quantloc_7;
                pix7[0] += max(-pix7[0], r_err * 7 / 16);
                pix7[1] += max(-pix7[1], g_err * 7 / 16);
                pix7[2] += max(-pix7[2], b_err * 7 / 16);
            }

            if (quantloc_3 < num_pixels) {
                auto pix3 = quant_pixels.get() + 4 * quantloc_3;
                pix3[0] += max(-pix3[0], r_err * 3 / 16);
                pix3[1] += max(-pix3[1], g_err * 3 / 16);
                pix3[2] += max(-pix3[2], b_err * 3 / 16);
            }

            if (quantloc_5 < num_pixels) {
                auto pix5 = quant_pixels.get() + 4 * quantloc_5;
                pix5[0] += max(-pix5[0], r_err * 5 / 16);
                pix5[1] += max(-pix5[1], g_err * 5 / 16);
                pix5[2] += max(-pix5[2], b_err * 5 / 16);
            }

            if (quantloc_1 < num_pixels) {
                auto pix1 = quant_pixels.get() + 4 * quantloc_1;
                pix1[0] += max(-pix1[0], r_err / 16);
                pix1[1] += max(-pix1[1], g_err / 16);
                pix1[2] += max(-pix1[2], b_err / 16);
            }
        }
    }

    // Copy the palettized result to the output buffer
    for (usize i{}; i < num_pixels * 4; ++i) {
        out_frame[i] = quant_pixels[i];
    }
}

// Picks palette colors for the image using simple thresholding, no dithering
void threshold_image(u8 const *last_frame, u8 const *next_frame, u8 *out_frame,
                     usize width, usize height, Palette &pal) {
    auto const num_pixels = width * height;
    for (usize i{}; i < num_pixels; ++i) {
        // if a previous color is available, and it matches the current color,
        // set the pixel to transparent
        if (last_frame && last_frame[0] == next_frame[0] &&
            last_frame[1] == next_frame[1] && last_frame[2] == next_frame[2]) {
            out_frame[0] = last_frame[0];
            out_frame[1] = last_frame[1];
            out_frame[2] = last_frame[2];
            out_frame[3] = transparency_index;
        } else {
            // palettize the pixel
            auto best_diff = 1000000;
            auto best_ind = 1;
            get_closest_pallete_color(pal, next_frame[0], next_frame[1],
                                      next_frame[2], best_ind, best_diff, 1);

            // Write the resulting color to the output buffer
            out_frame[0] = pal.r[best_ind];
            out_frame[1] = pal.g[best_ind];
            out_frame[2] = pal.b[best_ind];
            out_frame[3] = best_ind;
        }

        if (last_frame) last_frame += 4;
        out_frame += 4;
        next_frame += 4;
    }
}

// Simple structure to write out the LZW-compressed portion of the image
// one bit at a time
struct BitStatus {
    u8 bit_index = 0; // how many bits in the partial byte written so far
    u8 byte = 0;      // current partial byte

    u32 chunk_index = 0;
    array<u8, 256> chunk; // bytes are written in here until we have 256 of
                          // them, then written to the file
};

// insert a single bit
constexpr void write_bit(BitStatus &stat, u32 bit) {
    bit = bit & 1;
    bit = bit << stat.bit_index;
    stat.byte |= bit;

    ++stat.bit_index;
    if (stat.bit_index > 7) {
        // move the newly-finished byte to the chunk buffer
        stat.chunk[stat.chunk_index++] = stat.byte;
        // and start a new byte
        stat.bit_index = 0;
        stat.byte = 0;
    }
}

// write all bytes so far to the file
void write_chunk(FILE *f, BitStatus &stat) {
    fputc(static_cast<int>(stat.chunk_index), f);
    fwrite(stat.chunk.data(), 1, stat.chunk_index, f);

    stat.bit_index = 0;
    stat.byte = 0;
    stat.chunk_index = 0;
}

void write_code(FILE *f, BitStatus &stat, u32 code, u32 length) {
    for (usize i{}; i < length; ++i) {
        write_bit(stat, code);
        code = code >> 1;

        if (stat.chunk_index == 255) { write_chunk(f, stat); }
    }
}

// The LZW dictionary is a 256-ary tree constructed as the file is encoded,
// this is one node
struct GifLzwNode {
    array<u16, 256> next;
};

// write a 256-color (8-bit) image palette to the file
void write_pallete(Palette const &pal, FILE *f) {
    fputc(0, f); // first color: transparency
    fputc(0, f);
    fputc(0, f);

    for (usize i{1}; i < (1 << pal.bit_depth); ++i) {
        uint32_t r = pal.r[i];
        uint32_t g = pal.g[i];
        uint32_t b = pal.b[i];

        fputc(static_cast<int>(r), f);
        fputc(static_cast<int>(g), f);
        fputc(static_cast<int>(b), f);
    }
}

// write the image header, LZW-compress and write out the image
void write_lzw_image(FILE *f, u8 const *image, usize left, usize top,
                     usize width, usize height, usize delay,
                     Palette const &pal) {
    // graphics control extension
    fputc(0x21, f);
    fputc(0xf9, f);
    fputc(0x04, f);
    fputc(0x05, f); // leave prev frame in place, this frame has transparency
    fputc(static_cast<int>(delay) & 0xff, f);
    fputc(static_cast<int>(delay >> 8) & 0xff, f);
    fputc(transparency_index, f); // transparent color index
    fputc(0, f);

    fputc(0x2c, f); // image descriptor block

    fputc(static_cast<int>(left & 0xff), f); // corner of image in canvas space
    fputc(static_cast<int>((left >> 8) & 0xff), f);
    fputc(static_cast<int>(top & 0xff), f);
    fputc(static_cast<int>((top >> 8) & 0xff), f);

    fputc(static_cast<int>(width & 0xff), f); // width and height of image
    fputc(static_cast<int>((width >> 8) & 0xff), f);
    fputc(static_cast<int>(height & 0xff), f);
    fputc(static_cast<int>((height >> 8) & 0xff), f);

    // fputc(0, f); // no local color table, no transparency
    // fputc(0x80, f); // no local color table, but transparency

    // local color table present, 2 ^ bitDepth entries
    fputc(0x80 + pal.bit_depth - 1, f);
    write_pallete(pal, f);

    const auto min_code_size = pal.bit_depth;
    const auto clear_code = static_cast<u32>(1 << pal.bit_depth);

    fputc(min_code_size, f); // min code size 8 bits

    static constexpr auto codetree_size = 4096;
    auto codetree = std::make_unique<GifLzwNode[]>(codetree_size);

    memset(codetree.get(), 0, sizeof(GifLzwNode) * codetree_size);
    auto curr_code = -1;
    auto code_size = static_cast<u32>(min_code_size + 1);
    auto max_code = clear_code + 1;

    BitStatus stat;

    // start with a fresh LZW dictionary
    write_code(f, stat, clear_code, code_size);

    for (usize y{}; y < height; ++y) {
        for (usize x{}; x < width; ++x) {
#ifdef GIF_FLIP_VERT
            // bottom-left origin image (such as an OpenGL capture)
            auto const next_value =
                image[((height - 1 - yy) * width + xx) * 4 + 3];
#else
            // top-left origin
            auto const next_value = image[(y * width + x) * 4 + 3];
#endif

            if (curr_code < 0) {
                // first value in a new run
                curr_code = next_value;
            } else if (codetree[curr_code].next[next_value]) {
                // current run already in the dictionary
                curr_code = codetree[curr_code].next[next_value];
            } else {
                // finish the current run, write a code
                write_code(f, stat, curr_code, code_size);

                // insert the new run into the dictionary
                codetree[curr_code].next[next_value] = ++max_code;

                if (max_code >= (1UL << code_size)) {
                    // dictionary entry count has broken a size barrier,
                    // we need more bits for codes
                    code_size++;
                }
                if (max_code == 4095) {
                    // the dictionary is full, clear it out and begin anew
                    write_code(f, stat, clear_code, code_size); // clear tree

                    memset(codetree.get(), 0,
                           sizeof(GifLzwNode) * codetree_size);
                    code_size = min_code_size + 1;
                    max_code = clear_code + 1;
                }

                curr_code = next_value;
            }
        }
    }

    // compression footer
    write_code(f, stat, curr_code, code_size);
    write_code(f, stat, clear_code, code_size);
    write_code(f, stat, clear_code + 1, min_code_size + 1);

    // write out the last partial chunk
    while (stat.bit_index)
        write_bit(stat, 0);
    if (stat.chunk_index) write_chunk(f, stat);

    fputc(0, f); // image block terminator
}

struct Writer {
    using OwnedImage = std::unique_ptr<u8[]>;
    using File = std::unique_ptr<FILE, void (*)(FILE *f)>;

    File f = {nullptr, nullptr};
    OwnedImage old_image = nullptr;
    bool first_frame = true;
};

// Creates a gif file.
// The input GIFWriter is assumed to be uninitialized.
// The delay value is the time between frames in hundredths of a second - note
// that not all viewers pay much attention to this value.
auto begin(Writer &writer, const char *filename, usize width, usize height,
           usize delay, int bit_depth = 8, bool dither = false) -> bool {
    writer.f = nullptr;
    FILE *f{};

#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    fopen_s(&f, filename, "wb");
#else
    f = fopen(filename, "wb");
#endif
    if (!f) return false;

    // allocate
    writer.old_image = std::make_unique<u8[]>(width * height * 4);
    writer.f = Writer::File{f, [](FILE *f) { fclose(f); }};

    fputs("GIF89a", writer.f.get());

    // screen descriptor
    fputc(static_cast<int>(width & 0xff), writer.f.get());
    fputc(static_cast<int>((width >> 8) & 0xff), writer.f.get());
    fputc(static_cast<int>(height & 0xff), writer.f.get());
    fputc(static_cast<int>((height >> 8) & 0xff), writer.f.get());

    // there is an unsorted global color table of 2 entries
    fputc(0xf0, writer.f.get());
    fputc(0, writer.f.get()); // background color
    // pixels are square (we need to specify this because it's 1989)
    fputc(0, writer.f.get());

    // now the "global" palette (really just a dummy palette)
    // color 0: black
    fputc(0, writer.f.get());
    fputc(0, writer.f.get());
    fputc(0, writer.f.get());
    // color 1: also black
    fputc(0, writer.f.get());
    fputc(0, writer.f.get());
    fputc(0, writer.f.get());

    if (delay != 0) {
        // animation header
        fputc(0x21, writer.f.get());          // extension
        fputc(0xff, writer.f.get());          // application specific
        fputc(11, writer.f.get());            // length 11
        fputs("NETSCAPE2.0", writer.f.get()); // yes, really
        fputc(3, writer.f.get());             // 3 bytes of NETSCAPE2.0 data

        fputc(1, writer.f.get()); // JUST BECAUSE
        fputc(0, writer.f.get()); // loop infinitely (byte 0)
        fputc(0, writer.f.get()); // loop infinitely (byte 1)

        fputc(0, writer.f.get()); // block terminator
    }

    return true;
}

// Writes out a new frame to a GIF in progress.
// The GIFWriter should have been created by GIFBegin.
// AFAIK, it is legal to use different bit depths for different frames of an
// image - this may be handy to save bits in animations that don't change much.
auto write_frame(Writer &writer, u8 const *image, usize width, usize height,
                 usize delay, int bit_depth = 8, bool dither = false) -> bool {
    if (!writer.f) return false;

    const uint8_t *old_image =
        writer.first_frame ? nullptr : writer.old_image.get();
    writer.first_frame = false;

    Palette pal;
    make_pallete((dither ? nullptr : old_image), image, width, height,
                 bit_depth, dither, pal);

    if (dither)
        dither_image(old_image, image, writer.old_image.get(), width, height,
                     pal);
    else
        threshold_image(old_image, image, writer.old_image.get(), width, height,
                        pal);

    write_lzw_image(writer.f.get(), writer.old_image.get(), 0, 0, width, height,
                    delay, pal);

    return true;
}

// Writes the EOF code, closes the file handle, and frees temp memory used by a
// GIF. Many if not most viewers will still display a GIF properly if the EOF
// code is missing, but it's still a good idea to write it out.
auto end(Writer &writer) -> bool {
    if (!writer.f) return false;

    fputc(0x3b, writer.f.get()); // end of file

    writer.f = nullptr;
    writer.old_image = nullptr;

    return true;
}
} // namespace uppr::gif
