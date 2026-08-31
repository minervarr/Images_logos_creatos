#include <android_native_app_glue.h>
#include <android/log.h>

#include <exception>
#include <memory>

#include "android_host.hh"  // app_shell: the Android Host
#include "logo_view.hh"

#define LOG_TAG "ILCMain"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// The Android entry point, and the sibling of gui/src/main.cc: construct the
// app, hand it a Host, run it. There is no Android-specific UI - logo_view.cc
// is the same file the desktop exe runs, and every failure here logs a phase
// line because when android_main() returns, the activity is finished and the
// outside world cannot tell "refused" from "threw" from "ran and quit".
//
// Debug builds can be launched with the "il_capture" intent extra set to
// "capture": the app then renders its UI states off-screen (present
// suppressed - see Renderer::setPresentEnabled), writes the PNGs to its
// external files dir and exits. scripts/android/capture.sh drives it.
void android_main(android_app* state) {
    LOGI("phase 1/4: entry -- constructing LogoWindow");
    try {
        LogoWindow win;

        LOGI("phase 2/4: create() -- host init, Vulkan, fonts");
        // "il_capture" names the intent extra launchArgument() should answer;
        // app_shell knows neither the key nor what its value means.
        if (!win.create(std::make_unique<AndroidHost>(state, "il_capture", ""))) {
            LOGE("phase 2/4 FAILED: create() returned false -- the activity "
                 "will now finish. The line above this one is the reason.");
            return;
        }

        LOGI("phase 3/4: create() OK -- entering run()");
        win.run();

        LOGI("phase 4/4: run() returned -- shutting down");
        win.shutdown();
    } catch (const std::exception& e) {
        LOGE("FATAL: unhandled exception: %s", e.what());
    } catch (...) {
        LOGE("FATAL: unhandled non-standard exception");
    }
}
