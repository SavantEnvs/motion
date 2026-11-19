// Fuzz motion's PGM mask loader through the camera's picture setup: cls_picture's constructor calls
// init_mask() (config `mask_file`) and init_privacy() (config `mask_privacy`), which open the
// configured PGM file and parse/resize it with load_pgm(). Byte 0 picks which option names the file
// and the camera size; the rest is the PGM file.
#include "fuzz_common.hpp"
#include "picture.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) return -1;
    fz_init();
    fz_quiet_log();

    uint8_t sel = data[0];
    std::string path = fz_write_file(data + 1, size - 1, ".pgm");

    cls_camera *cam = new cls_camera(fz_app);
    cam->cfg = new cls_config(fz_app);
    cam->cfg->mask_file = (sel & 1) ? "" : path;
    cam->cfg->mask_privacy = (sel & 1) ? path : "";
    cam->imgs.width = 8 * (8 + ((sel >> 1) & 31));     // 64..312, multiple of 8 like motion requires
    cam->imgs.height = 8 * (6 + ((sel >> 6) & 3) * 8); // 48..240
    cam->imgs.size_norm = (cam->imgs.width * cam->imgs.height * 3) / 2;
    cam->imgs.width_high = 0;
    cam->imgs.height_high = 0;
    cam->imgs.size_high = 0;
    cam->imgs.mask = nullptr;

    cls_picture *pic = new cls_picture(cam);

    free(cam->imgs.mask);
    free(cam->imgs.mask_privacy);
    free(cam->imgs.mask_privacy_uv);
    delete pic;
    delete cam;
    return 0;
}
