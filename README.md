# Process Linker Repository
This git module contains a linker library which can be used to connect two process for data sharing and passing file descriptor.

- **src**: c source code of process linker library.
- **inc**: public header files of process linker library. User should include these files to use process linker.
- **test**: sample applications. Two sample applications, server and client, are implimented for test and reference purpose.

## How to build
Just run `make` and binaries will be generated in **output** folder.

## How to use
- **libplink.so**: shared library of process link. See API doc for more details of usage.
- **plinkserver**: sample server application
```shell
usage: ./plinkserver [options]

  Available options:
    -l      plink file name (default: /tmp/plink.test)
    -i      input YUV file name (mandatory)
    -f      input color format (default: 3)
                2 - I420
                3 - NV12
                4 - P010
                14 - Bayer Raw 10bit
                15 - Bayer Raw 12bit
    -w      video width (mandatory)
    -h      video height (mandatory)
    -s      video buffer stride in bytes (default: video width)
    -n      number of frames to send (default: 10)
```
- **plinkclient**: sample client application
```shell
./plinkclient [frames] [plink server name] [dump file name]
```

- **plinkstitcher**: sample implementation of stitching filter, which can stitch up to 4 source videos (NV12 or RAW) into one as NV12 output

```
usage: ./plinkstitcher [options]

  Stitch multiple pictures to one. Maximum # of pictures to be stitched is 4
  Available options:
    -i<n>   plink file name of input port #n (default: /tmp/plink.stitch.in<n>). n is 0 based.
    -o      plink file name of output port (default: /tmp/plink.stitch.out)
    -l      layout (default: 0)
                0 - vertical
                1 - horizontal
                2 - matrix
    -f      output color format (default: 3)
                3 - NV12
    -w      output video width (default: 800)
    -h      output video height (default: 1280)
    -s      output video buffer stride (default: video width)
    --help  print this message
```

Please note the sample applications have dependency on **video-memory** module for memory allocating and dma-buf operations. 
