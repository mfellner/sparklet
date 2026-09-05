#include "core.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
using namespace spark;
int main() {
    PreferencesRecord old{1, 0, 0, 0, 73, 0xff, 0x2c, 1};
    Preferences p;
    assert(decode_preferences(old.data(), old.size(), p));
    assert(p.version == 2 && p.brightness == 73 && p.dim_seconds == 300 && p.auto_rotate);
    for (unsigned padding = 0; padding <= 255; ++padding) {
        old[5] = padding;
        assert(decode_preferences(old.data(), old.size(), p) && p.auto_rotate);
    }
    p.auto_rotate = false;
    auto encoded = encode_preferences(p);
    const PreferencesRecord expected{2, 0, 0, 0, 73, 0, 0x2c, 1};
    assert(encoded == expected);
    Preferences restored;
    assert(decode_preferences(encoded.data(), 8, restored) && !restored.auto_rotate);
    for (bool enabled : {false, true})
        for (unsigned brightness : {10, 100})
            for (unsigned dim : {30, 600}) {
                p.brightness = brightness;
                p.dim_seconds = dim;
                p.auto_rotate = enabled;
                auto r = encode_preferences(p);
                assert(decode_preferences(r.data(), r.size(), restored));
                assert(restored.brightness == brightness && restored.dim_seconds == dim &&
                       restored.auto_rotate == enabled);
            }
    for (size_t n : {size_t(0), size_t(7), size_t(9)})
        assert(!decode_preferences(encoded.data(), n, restored));
    for (unsigned index : {0, 1, 2, 3, 4, 5, 7}) {
        auto bad = encoded;
        bad[index] = 255;
        assert(!decode_preferences(bad.data(), bad.size(), restored));
    }
    auto bad_dim = encoded;
    bad_dim[6] = bad_dim[7] = 0;
    assert(!decode_preferences(bad_dim.data(), bad_dim.size(), restored));
    const uint16_t src[]{0x1234, 0x5678, 0x9abc, 0xdef0, 0x1357, 0x2468};
    const uint16_t expected_pixels[4][6] = {{0x1234, 0x5678, 0x9abc, 0xdef0, 0x1357, 0x2468},
                                            {0xdef0, 0x1234, 0x1357, 0x5678, 0x2468, 0x9abc},
                                            {0x2468, 0x1357, 0xdef0, 0x9abc, 0x5678, 0x1234},
                                            {0x9abc, 0x2468, 0x5678, 0x1357, 0x1234, 0xdef0}};
    const Rect expected_rects[] = {
        {24, 60, 28, 62}, {418, 24, 420, 28}, {452, 418, 456, 420}, {60, 452, 62, 456}};
    for (unsigned o = 0; o < 4; ++o) {
        uint16_t out[8]{0xbeef, 0, 0, 0, 0, 0, 0, 0xbeef};
        rotate_pixels(src, out + 1, 3, 2, Orientation(o));
        assert(out[0] == 0xbeef && out[7] == 0xbeef);
        assert(!memcmp(out + 1, expected_pixels[o], sizeof src));
        auto r = rotate_rect({24, 60, 28, 62}, Orientation(o));
        auto e = expected_rects[o];
        assert(r.x1 == e.x1 && r.y1 == e.y1 && r.x2 == e.x2 && r.y2 == e.y2);
        for (int x = 0; x < 480; ++x)
            for (int y = 0; y < 480; ++y) {
                auto physical = rotate_point({x, y}, Orientation(o));
                assert(physical.x >= 0 && physical.x < 480 && physical.y >= 0 && physical.y < 480);
                auto logical = unrotate_point(physical, Orientation(o));
                assert(logical.x == x && logical.y == y);
            }
        for (int y = 0; y < 480; y += 12) {
            auto stripe = rotate_rect({0, y, 480, y + 12}, Orientation(o));
            assert(stripe.x1 % 2 == 0 && stripe.y1 % 2 == 0 && stripe.x2 % 2 == 0 &&
                   stripe.y2 % 2 == 0);
            assert((stripe.x2 - stripe.x1) * (stripe.y2 - stripe.y1) == 480 * 12);
        }
    }
    OrientationDetector d;
    uint64_t now = 0;
    auto settle = [&](Acceleration a, Orientation target) {
        auto before = d.current;
        for (int i = 0; i < 6; ++i, now += 50)
            assert(d.update(a, true, now) == before);
        assert(d.update(a, true, now) == target);
        now += 50;
    };
    settle({-1, 0, 0}, Orientation::Clockwise90);
    settle({0, -1, 0}, Orientation::UpsideDown);
    settle({1, 0, 0}, Orientation::Clockwise270);
    settle({0, 1, 0}, Orientation::Upright);
    for (Acceleration invalid : {Acceleration{0, 0, 1},
                                 {0.7f, 0.7f, 0},
                                 {2, 0, 0},
                                 {0.5f, 0, 0},
                                 {0.64f, 0, 0.77f},
                                 {std::numeric_limits<float>::quiet_NaN(), 0, 0}}) {
        for (int i = 0; i < 10; ++i, now += 50)
            assert(d.update(invalid, true, now) == Orientation::Upright);
        assert(!d.pending);
    }
    for (int i = 0; i < 20; ++i, now += 50) {
        assert(d.update(i % 2 ? Acceleration{-0.7f, 0.7f, 0} : Acceleration{-1, 0, 0}, true, now) ==
               Orientation::Upright);
    }
    d.update({-1, 0, 0}, true, now);
    now += 50;
    d.update({-1, 0, 0}, false, now);
    now += 50;
    settle({-1, 0, 0}, Orientation::Clockwise90);
    d.update({0, -1, 0}, true, now);
    now += 5000;
    assert(d.update({0, -1, 0}, true, now) == Orientation::Clockwise90);
    assert(d.since == now); // A long sample gap does not count as settled.
    assert(d.update({0, -1, 0}, true, 0) == Orientation::Clockwise90);
}
