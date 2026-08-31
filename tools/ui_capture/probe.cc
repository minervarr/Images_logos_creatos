// Renderer isolation probe: one clear + one rect through vkc::capture_main.
// If this PNG comes out wrong, the problem is below LogoWindow (driver /
// renderer / readback); if it comes out right, it is in the app's frame.
#include "capture.hh"
#include "canvas.hh"
#include "renderer.hh"

#include <cstdio>

int main(int argc, char** argv) {
    vkc::CaptureConfig cfg;
    cfg.default_out = "probe-shots";
    std::vector<vkc::Scenario> scenarios = {
        {"10-rect",
         [](Renderer& r) {
             std::vector<float> shapes, curves, quads;
             std::vector<ImageDraw> images, fg;
             Canvas c(curves, r.width(), r.height(), nullptr, 0, 0, 0, 0);
             c.useShapes(&shapes);
             c.rect(0, 0, (float)r.width(), (float)r.height(),
                    Color{0.1f, 0.2f, 0.4f, 1.0f}, 0.0f);
             c.rect(40, 40, 120, 80, Color{0.9f, 0.3f, 0.2f, 1.0f}, 8.0f);
             r.setPresentEnabled(false);
             r.draw(curves, 0, images, fg, quads, shapes);
         }},
    };
    return vkc::capture_main(argc, argv, scenarios, cfg);
}
