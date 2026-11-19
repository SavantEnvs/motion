// Fuzz motion's configuration parser through its real entry point: `motion -c <file>` handled by
// cls_config::init() (cmdline getopt, file parsing, edit_set on every parameter, camera/sound
// sections, *_params strings via util_parms_parse), followed by config_copy as cls_motapp::init does.
#include "fuzz_common.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fz_init();
    fz_quiet_log();

    std::string path = fz_write_file(data, size, ".conf");
    char a0[] = "motion", a1[] = "-c";
    char *a2 = strdup(path.c_str());
    char *argv[] = {a0, a1, a2, nullptr};

    fz_app->argc = 3;
    fz_app->argv = argv;
    fz_app->cam_cnt = 0;
    fz_app->snd_cnt = 0;
    fz_app->conf_src = new cls_config(fz_app);
    fz_app->cfg = new cls_config(fz_app);   // the logger reads app->cfg when log settings change
    fz_app->conf_src->init();
    fz_app->cfg->config_copy(fz_app->conf_src);
    fz_quiet_log();

    for (auto *cam : fz_app->cam_list) delete cam;
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
