// Fuzz motion's raw camera-frame conversion, cls_convert::process(): the path every V4L2 frame takes
// (MJPEG via jpgutl_decode_jpeg, sonix decompression, bayer, YUYV/UYVY/422P, Y10/Y12, grey, RGB/BGR).
// Input layout: byte 0 picks the pixel format, bytes 1-2 the frame size (multiples of 16, as motion
// requires), the rest is the frame as the driver would deliver it.
#include "fuzz_common.hpp"
#include "video_convert.hpp"
#include <vector>
#include <algorithm>

static const char *const kFormats[] = {
    "MJPG", "JPEG", "PJPG", "S561", "S910", "BA81", "BYR2", "GBRG", "GRBG", "RGGB",
    "YUYV", "UYVY", "422P", "RGB3", "BGR3", "GREY", "Y10 ", "Y12 ", "YU12", "I420", "IYUV",
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 3) return -1;
    fz_init();
    fz_quiet_log();

    std::string fmt = kFormats[data[0] % (sizeof(kFormats) / sizeof(kFormats[0]))];
    int width = 16 * (1 + (data[1] % 20));
    int height = 16 * (1 + (data[2] % 15));
    data += 3; size -= 3;

    // The capture buffer: the driver's buffer is at least one full frame of the largest raw layout
    // (3 bytes/pixel); `clen` is bytesused. For the planar YUV420 formats motion memcpy's clen bytes
    // into a frame-sized destination, and the driver's bytesused there is exactly one frame.
    size_t frame = (size_t)width * height * 3;
    std::vector<u_char> src(std::max(frame, size) + 16, 0);
    if (size) memcpy(src.data(), data, size);
    int clen = (int)size;
    if (fmt == "YU12" || fmt == "I420" || fmt == "IYUV") clen = (width * height * 3) / 2;

    std::vector<u_char> dst((size_t)width * height * 3 / 2, 0);
    cls_convert conv(nullptr, fmt, width, height);
    conv.process(dst.data(), src.data(), clen);
    return 0;
}
