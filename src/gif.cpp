#include "gif.hpp"

#include <optional>
#include <span>

namespace uppr::gif {

// === prototypes ===

/// Finds all pixels that have changed from the previous image and moves them to
/// the fromt of th buffer. This allows us to build a palette optimized for the
/// colors of the changed pixels only.
auto pick_changed_pixels(u8 const *last_frame, u8 *frame, usize num_pixels)
    -> int;

/// Implements Floyd-Steinberg dithering, writes palette value to alpha
void dither_image(u8 const *last_frame, u8 const *next_frame, u8 *out_frame,
                  usize width, usize height, Palette &pal);

/// Picks palette colors for the image using simple thresholding, no dithering
void threshold_image(u8 const *last_frame, u8 const *next_frame, u8 *out_frame,
                     usize width, usize height, Palette &pal);

/// Makes a copy of the given image.
auto copy_image(u8 const *src, usize image_size) -> std::unique_ptr<u8[]> {
    auto destroyable_image = std::make_unique<u8[]>(image_size);
    std::ranges::copy(std::span{src, image_size}, destroyable_image.get());

    return destroyable_image;
}

// === pallete methods ===

Palette::Palette(u8 const *last_frame, u8 const *next_frame, usize width,
                 usize height, int bit_depth, bool build_for_dither)
    : bit_depth{bit_depth} {
    // split_palette is destructive (it sorts the pixels by color) so we must
    // create a copy of the image for it to destroy
    auto const image_size = width * height * 4;
    auto destroyable_image = copy_image(next_frame, image_size);

    auto num_pixels = width * height;
    if (last_frame)
        num_pixels = pick_changed_pixels(last_frame, destroyable_image.get(),
                                         num_pixels);

    const auto last_elt = 1 << bit_depth;
    const auto split_elt = last_elt / 2;
    const auto split_dist = split_elt / 2;

    split(destroyable_image.get(), num_pixels, 1, last_elt, split_elt,
          split_dist, 1, build_for_dither);

    // add the bottom node for the transparency index
    tree_split[1 << (bit_depth - 1)] = 0;
    tree_split_elt[1 << (bit_depth - 1)] = 0;

    r[0] = g[0] = b[0] = 0;
}

void Palette::write(FILE *f) const {
    fputc(0, f); // first color: transparency
    fputc(0, f);
    fputc(0, f);

    for (usize i{1}; i < (1 << bit_depth); ++i) {
        fputc(static_cast<int>(r[i]), f);
        fputc(static_cast<int>(g[i]), f);
        fputc(static_cast<int>(b[i]), f);
    }
}

// === implementations ===

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
            pal.get_closest_pallete_color(rr, gg, bb, best_ind, best_diff, 1);

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
            pal.get_closest_pallete_color(next_frame[0], next_frame[1],
                                          next_frame[2], best_ind, best_diff,
                                          1);

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

// === BitStatus methods ===

void BitStatus::write_chunk(FILE *f) {
    fputc(static_cast<int>(chunk_index), f);
    fwrite(chunk.data(), 1, chunk_index, f);

    bit_index = 0;
    byte = 0;
    chunk_index = 0;
}

void BitStatus::write_code(FILE *f, u32 code, u32 length) {
    for (usize i{}; i < length; ++i) {
        write_bit(code);
        code = code >> 1;

        if (chunk_index == 255) { write_chunk(f); }
    }
}

// === compression handling ===

/// The LZW dictionary is a 256-ary tree constructed as the file is encoded,
/// this is one node of it.
struct GifLzwNode {
    array<u16, 256> next;
};

/// write the image header, LZW-compress and write out the image
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
    pal.write(f);

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
    stat.write_code(f, clear_code, code_size);

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
                stat.write_code(f, curr_code, code_size);

                // insert the new run into the dictionary
                codetree[curr_code].next[next_value] = ++max_code;

                if (max_code >= (1UL << code_size)) {
                    // dictionary entry count has broken a size barrier,
                    // we need more bits for codes
                    code_size++;
                }
                if (max_code == 4095) {
                    // the dictionary is full, clear it out and begin anew
                    stat.write_code(f, clear_code, code_size); // clear tree

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
    stat.write_code(f, curr_code, code_size);
    stat.write_code(f, clear_code, code_size);
    stat.write_code(f, clear_code + 1, min_code_size + 1);

    // write out the last partial chunk
    while (stat.bit_index)
        stat.write_bit(0);
    if (stat.chunk_index) stat.write_chunk(f);

    fputc(0, f); // image block terminator
}

// === Writer methods ===

auto Writer::open(std::string const &filename, usize width, usize height,
                  usize delay, int bit_depth, bool dither)
    -> std::optional<Writer> {
    Writer w;

    w.f = nullptr;
    FILE *f{};

#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    fopen_s(&f, filename.c_str(), "wb");
#else
    f = fopen(filename.c_str(), "wb");
#endif
    if (!f) return std::nullopt;

    // allocate
    w.old_image = std::make_unique<u8[]>(width * height * 4);
    w.f = Writer::File{
        f,
        [](FILE *f) {
            // put this here to make shure that it is added even
            // when `Writer::close` is not called.
            fputc(0x3b, f); // end of file
            fclose(f);
        },
    };

    fputs("GIF89a", w.f.get());

    // screen descriptor
    fputc(static_cast<int>(width & 0xff), w.f.get());
    fputc(static_cast<int>((width >> 8) & 0xff), w.f.get());
    fputc(static_cast<int>(height & 0xff), w.f.get());
    fputc(static_cast<int>((height >> 8) & 0xff), w.f.get());

    // there is an unsorted global color table of 2 entries
    fputc(0xf0, w.f.get());
    fputc(0, w.f.get()); // background color
    // pixels are square (we need to specify this because it's 1989)
    fputc(0, w.f.get());

    // now the "global" palette (really just a dummy palette)
    // color 0: black
    fputc(0, w.f.get());
    fputc(0, w.f.get());
    fputc(0, w.f.get());
    // color 1: also black
    fputc(0, w.f.get());
    fputc(0, w.f.get());
    fputc(0, w.f.get());

    if (delay != 0) {
        // animation header
        fputc(0x21, w.f.get());          // extension
        fputc(0xff, w.f.get());          // application specific
        fputc(11, w.f.get());            // length 11
        fputs("NETSCAPE2.0", w.f.get()); // yes, really
        fputc(3, w.f.get());             // 3 bytes of NETSCAPE2.0 data

        fputc(1, w.f.get()); // JUST BECAUSE
        fputc(0, w.f.get()); // loop infinitely (byte 0)
        fputc(0, w.f.get()); // loop infinitely (byte 1)

        fputc(0, w.f.get()); // block terminator
    }

    return w;
}

auto Writer::write_frame(u8 const *image, usize width, usize height,
                         usize delay, int bit_depth, bool dither) -> bool {
    if (!f) return false;

    const uint8_t *old_image = first_frame ? nullptr : this->old_image.get();
    first_frame = false;

    // make_pallete((dither ? nullptr : old_image), image, width, height,
    //              bit_depth, dither, pal);
    Palette pal{
        dither ? nullptr : old_image, image, width, height, bit_depth, dither,
    };

    if (dither)
        dither_image(old_image, image, this->old_image.get(), width, height,
                     pal);
    else
        threshold_image(old_image, image, this->old_image.get(), width, height,
                        pal);

    write_lzw_image(f.get(), this->old_image.get(), 0, 0, width, height, delay,
                    pal);

    return true;
}

auto Writer::close() -> bool {
    if (!f) return false;

    f = nullptr;
    old_image = nullptr;

    return true;
}
} // namespace uppr::gif
