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
  --dither                    Dither the image instead of performing a threshold
  --gen-example [0]           Generate an example GIF file
  --numeric-sort [0]          Try to find a number in all filenames and sort the list by it
```

