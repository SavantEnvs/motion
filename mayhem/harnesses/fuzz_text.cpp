// Fuzz motion's text overlay: the `text_left`, `text_right` and `text_params` config values, expanded
// by mystrftime (motion's conversion specifiers) and drawn into the frame by cls_draw::text() with the
// legacy bitmap font (the path of upstream fix 10b6d0f, "Check draw array bounds").
// Input: up to three '\n'-separated fields: text_left, text_right, text_params. They are written as
// those three lines of a motion config file and loaded with cls_config::init(), so the camera's cfg
// (and its parsed text_params) comes from motion's own config path; then, as cls_camera::init does,
// the camera gets its algsec and draw objects and the overlay is drawn on a 320x240 frame.
#include "fuzz_common.hpp"
#include "alg_sec.hpp"
#include "draw.hpp"
#include <vector>

static std::string next_field(const uint8_t *&p, const uint8_t *end)
{
    const uint8_t *nl = (const uint8_t *)memchr(p, '\n', (size_t)(end - p));
    const uint8_t *stop = nl ? nl : end;
    std::string s((const char *)p, (size_t)(stop - p));
    p = nl ? nl + 1 : end;
    return s;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fz_init();
    fz_quiet_log();

    const uint8_t *p = data, *end = data + size;
    std::string left = next_field(p, end);
    std::string right = next_field(p, end);

    std::string tparams_line = next_field(p, end);
    std::string conf = "text_left " + left + "\ntext_right " + right +
                       "\ntext_params " + tparams_line + "\n";
    std::string path = fz_write_file((const uint8_t *)conf.data(), conf.size(), ".conf");
    char a0[] = "motion", a1[] = "-c";
    char *a2 = strdup(path.c_str());
    char *argv[] = {a0, a1, a2, nullptr};
    fz_app->argc = 3;
    fz_app->argv = argv;
    fz_app->cam_cnt = 0;
    fz_app->snd_cnt = 0;
    fz_app->conf_src = new cls_config(fz_app);
    fz_app->cfg = new cls_config(fz_app);
    fz_app->conf_src->init();       // no `camera` line: adds one camera with these settings
    fz_app->cfg->config_copy(fz_app->conf_src);
    fz_quiet_log();

    const int w = 320, h = 240;
    cls_camera *cam = fz_app->cam_list[0];
    cam->imgs.width = w;
    cam->imgs.height = h;
    cam->imgs.size_norm = (w * h * 3) / 2;
    cam->movie_nbr = 0;
    cam->movie_fps = 0;
    cam->event_curr_nbr = 1;
    cam->noise = 0;
    cam->threshold = 0;

    std::vector<u_char> frame((size_t)cam->imgs.size_norm, 0x80);
    ctx_image_data img;
    memset(&img, 0, sizeof(img));
    img.image_norm = frame.data();
    img.imgts.tv_sec = 1700000000;   // fixed timestamp: same input, same expansion
    cam->current_image = &img;

    cam->algsec = new cls_algsec(cam);
    cam->draw = new cls_draw(cam);
    cam->draw->text();

    delete cam->draw;
    delete cam->algsec;
    cam->draw = nullptr;
    cam->algsec = nullptr;
    cam->current_image = nullptr;
    for (auto *c : fz_app->cam_list) delete c;
    fz_app->cam_list.clear();
    for (auto *snd : fz_app->snd_list) delete snd;
    fz_app->snd_list.clear();
    fz_app->cam_cnt = 0;
    fz_app->snd_cnt = 0;
    delete fz_app->cfg;
    delete fz_app->conf_src;
    fz_app->cfg = nullptr;
    fz_app->conf_src = nullptr;
    fz_app->argv = nullptr;
    free(a2);
    return 0;
}
