#include "core.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
namespace spark {
namespace {
alignas(std::max_align_t) unsigned char arena[ArenaLimit];
size_t arena_used;
std::mutex parse_mutex;
void *allocate(size_t n) {
    const size_t a = alignof(std::max_align_t);
    if (n > ArenaLimit)
        return nullptr;
    n = (n + a - 1) & ~(a - 1);
    if (n > ArenaLimit - arena_used)
        return nullptr;
    void *p = arena + arena_used;
    arena_used += n;
    return p;
}
void release(void *) {}
bool fail(char *e, size_t c, const char *s) {
    copy_text(e, c, s);
    return false;
}
bool depth_ok(const char *s, size_t n) {
    unsigned depth = 0;
    bool str = false, escape = false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (c == 0)
            return false;
        if (str) {
            if (escape) {
                if (c == 'u' && i + 4 < n && !memcmp(s + i + 1, "0000", 4))
                    return false; // Embedded NUL would silently shorten C-string IDs.
                escape = false;
            } else if (c == '\\')
                escape = true;
            else if (c == '"')
                str = false;
            else if (c < 32)
                return false;
        } else if (c == '"')
            str = true;
        else if (c == '{' || c == '[') {
            if (++depth > 16)
                return false;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            --depth;
        }
    }
    return !str && depth == 0;
}
cJSON *parse(const char *s, size_t n) {
    if (!s || !n || n > BodyLimit || !depth_ok(s, n))
        return nullptr;
    cJSON_Hooks hooks{allocate, release};
    cJSON_InitHooks(&hooks);
    arena_used = 0;
    const char *end = nullptr;
    auto *j = cJSON_ParseWithLengthOpts(s, n, &end, false);
    if (!j)
        return nullptr;
    while (end < s + n && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    return end == s + n ? j : nullptr;
}
const cJSON *field(const cJSON *o, const char *k) {
    return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, k) : nullptr;
}
const char *str(const cJSON *o, const char *k) {
    auto *v = field(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
bool yes(const cJSON *o, const char *k) {
    return cJSON_IsTrue(field(o, k));
}
Value number(const cJSON *o, const char *k) {
    auto *v = field(o, k);
    return cJSON_IsNumber(v) && std::isfinite(v->valuedouble) && v->valuedouble >= 0
               ? Value{v->valuedouble, true}
               : Value{};
}
Value fallback(Value a, Value b) {
    return a.valid ? a : b;
}
bool id_valid(const char *s) {
    if (!s || !*s || strlen(s) > 64)
        return false;
    for (auto *p = s; *p; p++)
        if (static_cast<unsigned char>(*p) < 32)
            return false;
    return true;
}
void identity(const cJSON *j, Node &n) {
    copy_text(n.id, sizeof n.id, str(j, "id"));
    copy_text(n.name, sizeof n.name, str(j, "name"));
    if (!*n.name)
        copy_text(n.name, sizeof n.name, n.id);
    copy_text(n.kind, sizeof n.kind, str(j, "kind"));
    copy_text(n.worker, sizeof n.worker, str(j, "workerLabel"));
    copy_text(n.head, sizeof n.head, str(j, "workerHeadId"));
    const char *r = str(j, "role");
    n.role = strcmp(r, "head") == 0                             ? Role::Head
             : strcmp(r, "worker") == 0 || yes(j, "workerNode") ? Role::Worker
                                                                : Role::Standalone;
}
int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    c = static_cast<char>(tolower(c));
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
} // namespace
void display_text(char *dst, size_t cap, const char *src) {
    if (!cap)
        return;
    struct Replacement {
        const char *from, *to;
    };
    static constexpr Replacement replacements[] = {
        {"—", "-"}, {"–", "-"},  {"‑", "-"},  {"−", "-"},   {"‘", "'"},
        {"’", "'"}, {"“", "\""}, {"”", "\""}, {"…", "..."}, {"\xc2\xa0", " "}};
    size_t used = 0;
    size_t remaining = strlen(src);
    while (remaining) {
        const char *value = src;
        unsigned char lead = static_cast<unsigned char>(*src);
        size_t consumed = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        size_t bytes = consumed;
        for (const auto &replacement : replacements) {
            size_t length = strlen(replacement.from);
            if (remaining >= length && !memcmp(src, replacement.from, length)) {
                value = replacement.to;
                consumed = length;
                bytes = strlen(value);
                break;
            }
        }
        if (consumed > remaining || bytes >= cap - used)
            break;
        memcpy(dst + used, value, bytes);
        used += bytes;
        src += consumed;
        remaining -= consumed;
    }
    dst[used] = 0;
}
void copy_text(char *d, size_t cap, const char *s) {
    if (!cap)
        return;
    if (!s)
        s = "";
    size_t n = std::min(cap - 1, strlen(s));
    if (s[n])
        while (n && (static_cast<unsigned char>(s[n]) & 0xc0) == 0x80)
            --n;
    memcpy(d, s, n);
    d[n] = 0;
}
bool parse_url(const char *s, Url &out, char *e, size_t cap) {
    if (!s || strncmp(s, "http://", 7))
        return fail(e, cap, "Use http:// (HTTPS is not supported in v1)");
    if (strlen(s) >= 320)
        return fail(e, cap, "Server URL is too long");
    for (auto *p = s; *p; p++)
        if (static_cast<unsigned char>(*p) <= 32 || *p == '@' || *p == '?' || *p == '#' ||
            *p == '\\')
            return fail(e, cap, "URL contains unsupported characters");
    Url u;
    const char *start = s + 7;
    const char *slash = strchr(start, '/');
    const char *end = slash ? slash : s + strlen(s);
    const char *colon = static_cast<const char *>(memchr(start, ':', end - start));
    size_t hn = (colon ? colon : end) - start;
    if (!hn || hn >= sizeof u.host)
        return fail(e, cap, "Invalid hostname");
    for (size_t i = 0; i < hn; i++)
        if (!isalnum(static_cast<unsigned char>(start[i])) && start[i] != '.' && start[i] != '-')
            return fail(e, cap, "Use a hostname or IPv4 address");
    memcpy(u.host, start, hn);
    if (colon) {
        unsigned long p = 0;
        if (colon + 1 == end)
            return fail(e, cap, "Missing port");
        for (auto *q = colon + 1; q < end; q++) {
            if (!isdigit(*q) || p > 65535)
                return fail(e, cap, "Invalid port");
            p = p * 10 + (*q - '0');
        }
        if (!p || p > 65535)
            return fail(e, cap, "Invalid port");
        u.port = p;
    }
    if (slash) {
        if (strlen(slash) >= sizeof u.prefix)
            return fail(e, cap, "Base path too long");
        copy_text(u.prefix, sizeof u.prefix, slash);
        size_t n = strlen(u.prefix);
        while (n && u.prefix[n - 1] == '/')
            u.prefix[--n] = 0;
    }
    out = u;
    return true;
}
bool validate_connection(const Connection &c, char *e, size_t cap) {
    if (c.version != 1 || !memchr(c.ssid, 0, sizeof c.ssid) ||
        !memchr(c.password, 0, sizeof c.password) || !memchr(c.url, 0, sizeof c.url))
        return fail(e, cap, "Invalid connection record");
    if (!*c.ssid)
        return fail(e, cap, "Enter the Wi-Fi network name");
    size_t n = strlen(c.password);
    if (n < 8 || n > 63)
        return fail(e, cap, "Wi-Fi password must be 8-63 characters");
    Url u;
    return parse_url(c.url, u, e, cap);
}
bool parse_list(const char *s, size_t len, Cache &out, char *e, size_t cap) {
    std::lock_guard<std::mutex> lock(parse_mutex);
    auto *j = parse(s, len);
    auto *a = field(j, "sparks");
    if (!cJSON_IsArray(a))
        return fail(e, cap, "Invalid node list or JSON limits exceeded");
    // Validate every ID, including entries beyond the display limit, without a second allocation.
    const cJSON *item = nullptr;
    size_t count = 0;
    cJSON_ArrayForEach(item, a) {
        if (!cJSON_IsObject(item) || !id_valid(str(item, "id")))
            return fail(e, cap, "Invalid node ID");
        for (auto *prev = a->child; prev != item; prev = prev->next)
            if (!strcmp(str(prev, "id"), str(item, "id")))
                return fail(e, cap, "Duplicate node ID");
        ++count;
    }
    Cache &result = out;
    result.count = 0;
    result.selected = 0;
    result.overflow = count > MaxNodes;
    cJSON_ArrayForEach(item, a) {
        if (result.count == MaxNodes)
            break;
        result.nodes[result.count] = Node{};
        identity(item, result.nodes[result.count++]);
    }
    return true;
}
bool parse_node(const char *s, size_t len, const char *expected, Node &out, char *e, size_t cap) {
    std::lock_guard<std::mutex> lock(parse_mutex);
    auto *j = parse(s, len);
    auto *m = field(j, "metrics");
    if (!cJSON_IsObject(m) || !id_valid(str(j, "id")) || strcmp(str(j, "id"), expected) ||
        !cJSON_IsBool(field(j, "online")))
        return fail(e, cap, "Invalid metrics, node ID, or JSON limits exceeded");
    Node n;
    identity(j, n);
    n.online = yes(j, "online") ? Online::Online : Online::Offline;
    auto *g = field(m, "gpu");
    auto *v = field(g, "vram");
    auto *u = field(m, "unifiedMemory");
    auto *p = field(g, "power");
    auto *c = field(m, "cpu");
    n.temperature = number(g, "temperature");
    n.usage = number(g, "usage");
    n.used = fallback(number(v, "used"), number(u, "used"));
    n.total = fallback(number(v, "total"), number(u, "total"));
    n.percent = fallback(number(v, "percentage"), number(u, "percentage"));
    n.available = fallback(number(v, "available"), number(u, "available"));
    n.power = number(p, "draw");
    n.power_limit = number(p, "limit");
    n.cpu_usage = number(c, "usage");
    n.cpu_temp = number(c, "temperature");
    auto *storage = field(m, "storage");
    const cJSON *d = nullptr;
    const cJSON *root = nullptr;
    cJSON_ArrayForEach(d, storage) {
        if (!strcmp(str(d, "label"), "/")) {
            root = d;
            break;
        }
        if (!root && !strcmp(str(d, "device"), "nvme0n1p2"))
            root = d;
    }
    n.disk_used = number(root, "used");
    n.disk_total = number(root, "total");
    auto *net = field(m, "network");
    auto *interfaces = field(net, "interfaces");
    const cJSON *ni = nullptr;
    cJSON_ArrayForEach(d, interfaces) {
        if (!strcmp(str(d, "name"), str(net, "primaryInterface")) &&
            *str(net, "primaryInterface")) {
            ni = d;
            break;
        }
        if (!ni && !yes(d, "disabled") && !strcmp(str(d, "operstate"), "up"))
            ni = d;
    }
    copy_text(n.iface, sizeof n.iface, str(ni, "name"));
    n.rx = number(ni, "rxSpeed");
    n.tx = number(ni, "txSpeed");
    if (n.role != Role::Worker) {
        const cJSON *l = nullptr;
        cJSON_ArrayForEach(l, field(m, "llm")) {
            if (yes(l, "available")) {
                copy_text(n.model, sizeof n.model, str(l, "modelId"));
                copy_text(n.backend, sizeof n.backend, str(l, "backend"));
                n.generation = number(l, "generationTps");
                n.prefill = number(l, "prefillTps");
                break;
            }
        }
    }
    out = n;
    return true;
}
void reconcile(Cache &current, const Cache &incoming) {
    char selected[65]{};
    size_t old_index = current.selected;
    if (current.count)
        copy_text(selected, sizeof selected, current.nodes[current.selected].id);
    static Cache result;
    result = incoming;
    for (size_t i = 0; i < result.count; i++) {
        for (size_t j = 0; j < current.count; j++)
            if (!strcmp(result.nodes[i].id, current.nodes[j].id)) {
                Node fresh = result.nodes[i];
                result.nodes[i] = current.nodes[j];
                copy_text(result.nodes[i].name, sizeof fresh.name, fresh.name);
                result.nodes[i].role = fresh.role;
                copy_text(result.nodes[i].kind, sizeof fresh.kind, fresh.kind);
                copy_text(result.nodes[i].worker, sizeof fresh.worker, fresh.worker);
                copy_text(result.nodes[i].head, sizeof fresh.head, fresh.head);
                break;
            }
    }
    result.selected = result.count ? std::min(old_index, result.count - 1) : 0;
    for (size_t i = 0; i < result.count; i++)
        if (!strcmp(result.nodes[i].id, selected))
            result.selected = i;
    result.revision = current.revision + 1;
    current = result;
}
void move(Cache &c, int dir) {
    if (c.count)
        c.selected = (c.selected + c.count + (dir < 0 ? -1 : 1)) % c.count;
}
void format_value(char *d, size_t cap, Value v, const char *suffix, int decimals) {
    if (!v.valid)
        snprintf(d, cap, "--");
    else if (v.value > 999999999)
        snprintf(d, cap, ">1e9%s", suffix);
    else
        snprintf(d, cap, "%.*f%s", decimals, v.value, suffix);
}
void format_memory(char *d, size_t cap, Value v) {
    if (v.valid && v.value >= 1024) {
        v.value /= 1024;
        format_value(d, cap, v, " GiB", 1);
    } else
        format_value(d, cap, v, " MiB", 1);
}
void format_rate(char *d, size_t cap, Value v) {
    if (!v.valid) {
        copy_text(d, cap, "--");
        return;
    }
    const char *unit = " B/s";
    if (v.value >= 1048576) {
        v.value /= 1048576;
        unit = " MiB/s";
    } else if (v.value >= 1024) {
        v.value /= 1024;
        unit = " KiB/s";
    }
    format_value(d, cap, v, unit, 1);
}
bool percent_encode(const char *s, char *d, size_t cap) {
    size_t n = 0;
    for (; *s; s++) {
        unsigned char c = *s;
        bool safe = isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
        if (n + (safe ? 1 : 3) >= cap)
            return false;
        if (safe)
            d[n++] = c;
        else {
            static const char *h = "0123456789ABCDEF";
            d[n++] = '%';
            d[n++] = h[c >> 4];
            d[n++] = h[c & 15];
        }
    }
    d[n] = 0;
    return true;
}
bool decode_form(const char *b, const char *key, char *d, size_t cap) {
    size_t k = strlen(key);
    for (auto *p = b; *p;) {
        const char *end = strchr(p, '&');
        if (!end)
            end = p + strlen(p);
        if (static_cast<size_t>(end - p) > k && p[k] == '=' && !strncmp(p, key, k)) {
            size_t n = 0;
            for (auto *q = p + k + 1; q < end; q++) {
                unsigned char c = *q;
                if (c == '+')
                    c = ' ';
                else if (c == '%') {
                    if (q + 2 >= end || hex(q[1]) < 0 || hex(q[2]) < 0)
                        return false;
                    c = hex(q[1]) * 16 + hex(q[2]);
                    q += 2;
                }
                if (!c || n + 1 >= cap)
                    return false;
                d[n++] = c;
            }
            d[n] = 0;
            return true;
        }
        p = *end ? end + 1 : end;
    }
    return false;
}
Scheduler::Work Scheduler::next(uint64_t now, size_t count, size_t selected) const {
    if (now < retry_due)
        return {};
    if (list_required)
        return {Kind::List, 0};
    if (count && (urgent || now >= foreground_due))
        return {Kind::Foreground, selected};
    if (now >= list_due)
        return {Kind::List, 0};
    if (count > 1 && now >= background_due) {
        size_t idx = round_robin % count;
        if (idx == selected)
            idx = (idx + 1) % count;
        return {Kind::Background, idx};
    }
    return {};
}
void Scheduler::completed(Work w, uint64_t now, size_t count) {
    switch (w.kind) {
    case Kind::List:
        list_required = false;
        list_due = now + 60000;
        break;
    case Kind::Foreground:
        urgent = false;
        foreground_due = now + 2000;
        break;
    case Kind::Background:
        background_due = now + 2000;
        round_robin = count ? (w.index + 1) % count : 0;
        break;
    default:
        break;
    }
}
void Scheduler::failed(uint64_t now, uint32_t jitter) {
    ++failures;
    retry_due = now + 2000;
    if (failures >= 3) {
        const unsigned sec = retry_step < 4 ? (2u << retry_step) : 30u;
        retry_step = std::min(retry_step + 1, 5u);
        retry_due = now + sec * 1000 + (jitter % 251);
    }
}
} // namespace spark
