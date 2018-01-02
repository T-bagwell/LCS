# LCS
Read file loop and remux the file to the other file.

Read one input file, and loop read the streams of the files, read from the head when read end of the file.
remuxing frames to a new format from the input file.

# Depend
FFmpeg - code base libav*

# Build on OS X
make

# Run
./lcs a.mp4 output.flv
