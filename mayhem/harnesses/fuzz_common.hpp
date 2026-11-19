// Shared setup for motion's fuzz harnesses. motion keeps its state in a cls_motapp and logs through
// the global `motlog`; both must exist before any motion code runs (MOTION_LOG dereferences motlog).
#ifndef MAYHEM_FUZZ_COMMON_HPP
#define MAYHEM_FUZZ_COMMON_HPP
#include "motion.hpp"
#include "util.hpp"
#include "logger.hpp"
#include "conf.hpp"
#include "camera.hpp"
#include "sound.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static cls_motapp *fz_app = nullptr;

// Kept across inputs on purpose: the app object and the logger. The logger's level is reset before
// every input (a config input may change it), and nothing an input writes into the app survives it
// (each harness clears cam_list/snd_list/conf_src/cfg after the input).
static void fz_init()
{
    if (fz_app != nullptr) return;
    fz_app = new cls_motapp();
    fz_app->cam_cnt = 0;
    fz_app->snd_cnt = 0;
    fz_app->conf_src = nullptr;
    fz_app->cfg = nullptr;
    motlog = new cls_log(fz_app);
}

static void fz_quiet_log()
{
    motlog->conf_chg = false;
    motlog->log_level = 0;      // below EMG: no message is formatted or written
    motlog->log_fflevel = 0;
}

// Write the input to one scratch file under $TMPDIR (created once per process, rewritten per input).
static std::string fz_write_file(const uint8_t *data, size_t size, const char *suffix)
{
    static std::string path;
    if (path.empty()) {
        const char *tmp = getenv("TMPDIR");
        std::string tmpl = std::string((tmp && *tmp) ? tmp : "/tmp") + "/motion-fuzz-XXXXXX";
        std::string full = tmpl + suffix;
        char *buf = strdup(full.c_str());
        int fd = mkstemps(buf, (int)strlen(suffix));
        if (fd < 0) { perror("mkstemps"); abort(); }
        close(fd);
        path = buf;
        free(buf);
    }
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) { perror("fopen"); abort(); }
    if (size) fwrite(data, 1, size, f);
    fclose(f);
    return path;
}
#endif
