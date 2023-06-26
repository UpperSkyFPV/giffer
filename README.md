# Giffer

Create GIFs from images.

```
giffer GIF maker
Usage: ./build/giffer [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  -i,--input-files TEXT ...   Name of the file to use as input in the conversion
  -o,--output-file TEXT [out.gif] 
                              Name of the file to generate
  --delay INT [2]             Delay in between GIF frames
  --bit-depth INT [8]         Bit depth to use in the output image
  --dither [0]                Dither the image instead of performing a threshold
  --gen-example [0]           Generate an example GIF file
  --numeric-sort [0]          Try to find a number in all filenames and sort the list by it
```

Running `./build/giffer --gen-example` will generate a 512x512 image to test the
algorithms.

## How it works

GIF is limited to only 256 colors per frame, so before processing each frame we
generate a palette that best fits the colors in the image. At this point, we
already choose if we want to use a dithering algorithm when generating the
image or a _much_ simpler threshold algorithm. It is a lot faster and is better
for images with lots of small lines, but does not look very good on images with
a lot of gradients. Try both on your image sequence and see witch one looks
better.

The `--numeric-sort` flag is used in order to allow using a wildcard pattern on
folders and have the frames going in the right order. For example, if you have a
folder with hundreds of frames from a video, labeled `frame-<n>.png`, where `n`
is the frame number, `--numeric-sort` will find the `n` and use it to sort the
frames. Please dont try to use it if the filename does not have a number in it.

For example:

```bash
./build/giffer -i frames/* -o out.gif --numeric-sort
```
