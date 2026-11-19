// Known-answer functional tests for the motion code the fuzz targets reach (config parsing,
// *_params parsing, raw frame conversion, PGM mask loading). Built with motion's NORMAL flags by
// mayhem/build.sh and run by mayhem/test.sh. Prints one "PASS <name>"/"FAIL <name>" line per check
// and a final "SUMMARY passed=<n> failed=<m>".
#include "fuzz_common.hpp"
#include "video_convert.hpp"
#include "picture.hpp"
#include <vector>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (ok) g_pass++; else g_fail++;
}

static void test_config()
{
    const char conf[] =
        "# comment\n"
        "daemon off\n"
        "framerate 7\n"
        "width=640\n"
        "text_left hello world\n"
        "mask_file /nonexistent.pgm\n"
        "netcam_params decoder=h264,capture_rate=5\n";
    std::string path = fz_write_file((const uint8_t *)conf, sizeof(conf) - 1, ".conf");
    char a0[] = "motion", a1[] = "-c";
    char *a2 = strdup(path.c_str());
    char *argv[] = {a0, a1, a2, nullptr};
    fz_app->argc = 3; fz_app->argv = argv;
    fz_app->cam_cnt = 0; fz_app->snd_cnt = 0;
    fz_app->conf_src = new cls_config(fz_app);
    fz_app->cfg = new cls_config(fz_app);
    fz_app->conf_src->init();
    fz_app->cfg->config_copy(fz_app->conf_src);
    fz_quiet_log();

    check(fz_app->cfg->framerate == 7, "config_framerate");
    check(fz_app->cfg->width == 640, "config_width_equals_sign");
    check(fz_app->cfg->text_left == "hello world", "config_string_with_spaces");
    check(fz_app->cfg->mask_file == "/nonexistent.pgm", "config_string");
    check(fz_app->cam_cnt == 1, "config_default_camera_added");
    check(fz_app->cam_cnt == 1 && fz_app->cam_list[0]->cfg->framerate == 7,
          "config_camera_inherits");

    ctx_params params;
    params.app = fz_app;
    params.params_array.clear();
    params.params_cnt = 0;
    params.params_desc = "netcam_params";
    util_parms_parse(&params, "netcam_params", fz_app->cfg->netcam_params, PARM_PCAT_NET);
    bool found = false;
    for (auto &it : params.params_array) {
        if (it.param_name == "decoder" && it.param_value == "h264") found = true;
    }
    check(found, "params_parse_decoder");
    check(params.params_cnt == (int)params.params_array.size() && params.params_cnt >= 2,
          "params_parse_count");

    for (auto *cam : fz_app->cam_list) delete cam;
    fz_app->cam_list.clear(); fz_app->cam_cnt = 0;
    delete fz_app->cfg; delete fz_app->conf_src;
    fz_app->cfg = nullptr; fz_app->conf_src = nullptr;
    free(a2);
}

static void test_convert()
{
    const int w = 16, h = 16;
    std::vector<u_char> src((size_t)w * h * 3, 0), dst((size_t)w * h * 3 / 2, 0);

    for (int i = 0; i < w * h; i++) src[i] = (u_char)i;
    cls_convert grey(nullptr, "GREY", w, h);
    grey.process(dst.data(), src.data(), w * h);
    bool ok = true;
    for (int i = 0; i < w * h; i++) ok = ok && dst[i] == (u_char)i;
    for (int i = w * h; i < w * h * 3 / 2; i++) ok = ok && dst[i] == 128;
    check(ok, "convert_grey");

    // YUYV with Y=0x50, U=0x30, V=0xC0 everywhere -> planar Y=0x50, U=0x30, V=0xC0.
    for (int i = 0; i < w * h * 2; i += 4) {
        src[i] = 0x50; src[i + 1] = 0x30; src[i + 2] = 0x50; src[i + 3] = 0xC0;
    }
    std::fill(dst.begin(), dst.end(), 0);
    cls_convert yuyv(nullptr, "YUYV", w, h);
    yuyv.process(dst.data(), src.data(), w * h * 2);
    ok = true;
    for (int i = 0; i < w * h; i++) ok = ok && dst[i] == 0x50;
    for (int i = 0; i < w * h / 4; i++) ok = ok && dst[w * h + i] == 0x30;
    for (int i = 0; i < w * h / 4; i++) ok = ok && dst[w * h + w * h / 4 + i] == 0xC0;
    check(ok, "convert_yuyv");

    // MJPG without an SOI marker is rejected (return 1) and leaves the frame untouched.
    std::fill(dst.begin(), dst.end(), 7);
    std::fill(src.begin(), src.end(), 0x11);
    cls_convert mjpg(nullptr, "MJPG", w, h);
    int rc = mjpg.process(dst.data(), src.data(), 64);
    check(rc == 1 && dst[0] == 7, "convert_mjpg_no_soi");
}

static void test_mask()
{
    const int w = 64, h = 48;
    std::string pgm = "P5\n# mask\n64 48\n255\n";
    for (int i = 0; i < w * h; i++) pgm.push_back((char)(i % 2 ? 0 : 255));
    std::string path = fz_write_file((const uint8_t *)pgm.data(), pgm.size(), ".pgm");

    cls_camera *cam = new cls_camera(fz_app);
    cam->cfg = new cls_config(fz_app);
    cam->cfg->mask_file = path;
    cam->cfg->mask_privacy = "";
    cam->imgs.width = w; cam->imgs.height = h;
    cam->imgs.size_norm = w * h * 3 / 2;
    cam->imgs.width_high = cam->imgs.height_high = cam->imgs.size_high = 0;
    cam->imgs.mask = nullptr;
    cls_picture *pic = new cls_picture(cam);
    bool ok = cam->imgs.mask != nullptr;
    for (int i = 0; ok && i < w * h; i++) ok = cam->imgs.mask[i] == (i % 2 ? 0 : 255);
    check(ok, "mask_pgm_same_size");
    free(cam->imgs.mask); cam->imgs.mask = nullptr;
    delete pic;

    // Half-scale mask is resized up to the camera size.
    pgm = "P5\n32 24\n255\n" + std::string(32 * 24, (char)200);
    fz_write_file((const uint8_t *)pgm.data(), pgm.size(), ".pgm");
    pic = new cls_picture(cam);
    ok = cam->imgs.mask != nullptr;
    for (int i = 0; ok && i < w * h; i++) ok = cam->imgs.mask[i] == 200;
    check(ok, "mask_pgm_resized");
    free(cam->imgs.mask); cam->imgs.mask = nullptr;
    delete pic;

    fz_write_file((const uint8_t *)"P6\n1 1\n255\nx", 12, ".pgm");
    pic = new cls_picture(cam);
    check(cam->imgs.mask == nullptr, "mask_rejects_non_p5");
    delete pic;
    delete cam;
}

int main()
{
    fz_init();
    fz_quiet_log();
    test_config();
    test_convert();
    test_mask();
    printf("SUMMARY passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
