#include "core.hpp"
#include <cassert>
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
}
