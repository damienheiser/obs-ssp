#undef NDEBUG
#include <QApplication>
#include <QTimer>
#include <obs.h>
#include <cassert>
#include <cstdio>

static void load(const char *name)
{
    QString root = QString("/Applications/OBS.app/Contents/PlugIns/%1.plugin/Contents/").arg(name);
    obs_module_t *module = nullptr;
    auto binary = (root + "MacOS/" + name).toUtf8();
    auto resources = (root + "Resources").toUtf8();
    assert(obs_open_module(&module, binary.constData(), resources.constData()) == MODULE_SUCCESS);
    assert(obs_init_module(module));
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    assert(obs_startup("en-US", "/Users/agent-codex/obs-ssp-diagnostics/obs-engine", nullptr));
    obs_add_data_path("/Applications/OBS.app/Contents/Frameworks/libobs.framework/Resources");
    obs_video_info video = {};
    video.graphics_module = "/Applications/OBS.app/Contents/Frameworks/libobs-opengl.dylib";
    video.fps_num = 30000;
    video.fps_den = 1001;
    video.base_width = video.output_width = 320;
    video.base_height = video.output_height = 180;
    video.output_format = VIDEO_FORMAT_NV12;
    video.colorspace = VIDEO_CS_709;
    video.range = VIDEO_RANGE_PARTIAL;
    video.gpu_conversion = true;
    assert(obs_reset_video(&video) == OBS_VIDEO_SUCCESS);
    obs_audio_info audio = {};
    audio.samples_per_sec = 48000;
    audio.speakers = SPEAKERS_STEREO;
    assert(obs_reset_audio(&audio));
    load("image-source");
    load("obs-x264");
    load("obs-ffmpeg");
    obs_module_t *module = nullptr;
    assert(obs_open_module(&module, SSP_TEST_MODULE, SSP_TEST_RESOURCES) == MODULE_SUCCESS);
    assert(obs_init_module(module));
    obs_post_load_modules();
    auto output = obs_output_create("ssp_output", "real peer transport test", nullptr, nullptr);
    assert(output && "SSP output registration is required");
    obs_output_release(output);
    obs_shutdown();
    puts("PASS actual OBS engine and SSP output registration");
}
