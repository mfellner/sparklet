#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace spark {
constexpr size_t MaxNodes = 16, BodyLimit = 16384, ArenaLimit = 32768;
struct Value {
    double value = 0;
    bool valid = false;
};
enum class Role { Head, Worker, Standalone };
enum class Online { Unknown, Offline, Online };
struct Node {
    char id[65]{}, name[97]{}, kind[16]{}, worker[161]{}, head[65]{};
    char model[161]{}, backend[24]{}, iface[33]{}, error[80]{};
    Role role = Role::Standalone;
    Online online = Online::Unknown;
    Value temperature, usage, used, total, percent, available, power, power_limit;
    Value cpu_usage, cpu_temp, disk_used, disk_total, rx, tx, generation, prefill;
    uint64_t received_ms = 0;
    bool received = false;
};
struct Cache {
    std::array<Node, MaxNodes> nodes{};
    size_t count = 0, selected = 0;
    bool overflow = false;
    uint32_t revision = 0;
};
struct Url {
    char host[128]{}, prefix[192]{};
    uint16_t port = 80;
};
struct Connection {
    uint32_t version = 1;
    char ssid[33]{}, password[65]{}, url[320] = "http://dgx01.local:5555";
};
struct Preferences {
    uint32_t version = 2;
    uint8_t brightness = 60;
    uint16_t dim_seconds = 120;
    bool auto_rotate = true;
};
// Explicit 8-byte little-endian record; v1 byte 5 was padding, never a boolean.
using PreferencesRecord = std::array<uint8_t, 8>;
PreferencesRecord encode_preferences(const Preferences &);
bool decode_preferences(const uint8_t *, size_t, Preferences &);
enum class Orientation : uint8_t { Upright, Clockwise90, UpsideDown, Clockwise270 };
struct Point {
    int x, y;
};
struct Rect {
    int x1, y1, x2, y2;
}; // exclusive upper bounds
struct Acceleration {
    float x, y, z;
}; // g; x right, y down in upright display coordinates
Point rotate_point(Point, Orientation, int side = 480);
Point unrotate_point(Point, Orientation, int side = 480);
Rect rotate_rect(Rect, Orientation, int side = 480);
// Non-overlapping tightly packed buffers; preserves both bytes of every RGB565 pixel.
void rotate_pixels(const uint16_t *, uint16_t *, int width, int height, Orientation);
struct OrientationDetector {
    Orientation current = Orientation::Upright, candidate = Orientation::Upright;
    uint64_t since = 0, last_sample = 0;
    bool pending = false;
    Orientation update(Acceleration, bool valid, uint64_t now);
};
void copy_text(char *dst, size_t cap, const char *src);
// Display-only punctuation substitutions; stored IDs and API values stay untouched.
void display_text(char *dst, size_t cap, const char *src);
bool parse_url(const char *, Url &, char *error, size_t cap);
bool validate_connection(const Connection &, char *error, size_t cap);
bool parse_list(const char *, size_t, Cache &, char *error, size_t cap);
bool parse_node(const char *, size_t, const char *expected, Node &, char *error, size_t cap);
void reconcile(Cache &, const Cache &);
void move(Cache &, int direction);
void format_value(char *, size_t, Value, const char *suffix, int decimals = 0);
void format_memory(char *, size_t, Value);
void format_rate(char *, size_t, Value);
bool percent_encode(const char *, char *, size_t);
bool decode_form(const char *body, const char *key, char *dst, size_t cap);
// Changes only on completed requests; deadlines are monotonic milliseconds.
struct Scheduler {
    enum class Kind { None, List, Foreground, Background };
    struct Work {
        Kind kind = Kind::None;
        size_t index = 0;
    };
    uint64_t list_due = 0, foreground_due = 0, background_due = 0, retry_due = 0;
    size_t round_robin = 0;
    unsigned failures = 0, retry_step = 0;
    bool list_required = true, urgent = false;
    Work next(uint64_t now, size_t count, size_t selected) const;
    void completed(Work, uint64_t now, size_t count);
    void select() {
        urgent = true;
        foreground_due = 0;
    }
    void reconnect() {
        list_required = true;
        retry_due = 0;
        failures = retry_step = 0;
    }
    void failed(uint64_t now, uint32_t jitter = 0);
    void success() {
        failures = retry_step = 0;
        retry_due = 0;
    }
};
} // namespace spark
