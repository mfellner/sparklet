#include "core.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using namespace spark;
int main() {
    char e[128]{}, text[400];
    Url u;
    assert(parse_url("http://dgx01.local:5555/prefix/", u, e, sizeof e));
    assert(u.port == 5555 && !strcmp(u.prefix, "/prefix"));
    for (auto *s : {"https://x", "http://user@x", "http://x:0", "http://x:99999", "http://x?a",
                    "http://x/#a", "http://x\r\nInjected", "http://[::1]"})
        assert(!parse_url(s, u, e, sizeof e));
    Connection c;
    copy_text(c.ssid, sizeof c.ssid, "synthetic");
    copy_text(c.password, sizeof c.password, "test-only-123");
    assert(validate_connection(c, e, sizeof e));
    auto corrupt = c;
    memset(corrupt.url, 'x', sizeof corrupt.url);
    assert(!validate_connection(corrupt, e, sizeof e));
    corrupt = c;
    corrupt.version = 99;
    assert(!validate_connection(corrupt, e, sizeof e));
    Cache cache;
    const char *list = R"({"sparks":[{"id":"a","role":"head"},{"id":"b","workerNode":true}]})";
    assert(parse_list(list, strlen(list), cache, e, sizeof e));
    assert(cache.count == 2 && cache.nodes[1].role == Role::Worker);
    Node node;
    const char *s =
        R"({"id":"a","online":true,"role":"head","metrics":{"gpu":{"usage":0,"vram":{"available":0}},"unifiedMemory":{"used":1024,"total":2048},"storage":[{"device":"nvme0n1p2","used":1},{"label":"/","used":2,"total":3}],"network":{"primaryInterface":"eth0","interfaces":[{"name":"eth1","operstate":"up","rxSpeed":99},{"name":"eth0","rxSpeed":1024,"txSpeed":2048}]},"llm":[{"available":false},{"available":true,"modelId":"first","generationTps":0,"prefillTps":2},{"available":true,"modelId":"second"}]}})";
    assert(parse_node(s, strlen(s), "a", node, e, sizeof e));
    assert(node.usage.valid && node.usage.value == 0);
    assert(!node.temperature.valid);
    assert(node.available.valid && node.available.value == 0);
    assert(node.used.value == 1024);
    assert(node.disk_used.value == 2);
    assert(node.rx.value == 1024);
    assert(!strcmp(node.model, "first"));
    assert(node.generation.valid && node.generation.value == 0);
    Node before = node;
    assert(!parse_node(s, strlen(s), "b", node, e, sizeof e));
    assert(!strcmp(node.model, before.model));
    std::string worker = s;
    worker.replace(worker.find("head"), 4, "worker");
    assert(parse_node(worker.data(), worker.size(), "a", node, e, sizeof e));
    assert(!node.generation.valid && !*node.model);
    cache.nodes[0] = before;
    cache.nodes[0].received = true;
    cache.selected = 0;
    Cache reorder;
    std::string r = R"({"sparks":[{"id":"b"},{"id":"a","name":"renamed"}]})";
    assert(parse_list(r.data(), r.size(), reorder, e, sizeof e));
    reconcile(cache, reorder);
    assert(cache.selected == 1 && cache.nodes[1].received &&
           !strcmp(cache.nodes[1].name, "renamed"));
    move(cache, 1);
    assert(cache.selected == 0);
    move(cache, -1);
    assert(cache.selected == 1);
    auto embedded_nul = R"({"sparks":[{"id":"a\u0000b"}]})";
    assert(!parse_list(embedded_nul, strlen(embedded_nul), cache, e, sizeof e));
    auto prior = cache;
    for (auto *bad :
         {"{}", "{broken", R"({"sparks":[{"id":"a"},{"id":"a"}]})", R"({"sparks":[{"id":""}]})"}) {
        assert(!parse_list(bad, strlen(bad), cache, e, sizeof e));
        assert(cache.count == prior.count);
    }
    std::string many = "{\"sparks\":[";
    for (int i = 0; i < 17; i++) {
        if (i)
            many += ",";
        many += "{\"id\":\"n" + std::to_string(i) + "\"}";
    }
    many += "]}";
    assert(parse_list(many.data(), many.size(), cache, e, sizeof e) && cache.count == 16 &&
           cache.overflow);
    std::string deep(17, '[');
    deep += std::string(17, ']');
    assert(!parse_list(deep.data(), deep.size(), cache, e, sizeof e));
    std::string big(BodyLimit + 1, ' ');
    assert(!parse_list(big.data(), big.size(), cache, e, sizeof e));
    std::string arena = "{\"sparks\":[],\"unused\":[";
    for (int i = 0; i < 2000; i++)
        arena += (i ? ",0" : "0");
    arena += "]}";
    assert(arena.size() < BodyLimit);
    assert(!parse_list(arena.data(), arena.size(), cache, e, sizeof e));
    assert(parse_list(list, strlen(list), cache, e, sizeof e));
    char utf[5];
    copy_text(utf, sizeof utf, "ab€x");
    assert(!strcmp(utf, "ab"));
    format_memory(text, sizeof text, {1024, true});
    assert(!strcmp(text, "1.0 GiB"));
    format_rate(text, sizeof text, {1024, true});
    assert(!strcmp(text, "1.0 KiB/s"));
    assert(percent_encode("a/b ?", text, sizeof text) && !strcmp(text, "a%2Fb%20%3F"));
    assert(decode_form("ssid=a+b&password=test%21", "ssid", text, sizeof text) &&
           !strcmp(text, "a b"));
    assert(!decode_form("ssid=%00", "ssid", text, sizeof text));
    display_text(text, sizeof text, "dgx01 — Head ‘test’ – 65°C");
    assert(!strcmp(text, "dgx01 - Head 'test' - 65°C"));
    display_text(text, sizeof text, "“ready”…\xc2\xa0−1");
    assert(!strcmp(text, "\"ready\"... -1"));
    char small[5];
    display_text(small, sizeof small, "ab€x");
    assert(!strcmp(small, "ab"));
    display_text(small, sizeof small, "a…x");
    assert(!strcmp(small, "a..."));
    display_text(small, 1, "test");
    assert(!*small);
    // Contract cases beyond the deployed server's current shapes.
    for (auto *role_name : {"standalone", "future-role"}) {
        std::string role_json =
            std::string(R"({"id":"x","role":")") + role_name + R"(","online":false,"metrics":{}})";
        assert(parse_node(role_json.data(), role_json.size(), "x", node, e, sizeof e));
        assert(node.role == Role::Standalone && node.online == Online::Offline);
        assert(!node.used.valid && !node.generation.valid);
    }
    std::string compatibility =
        R"({"id":"x","name":"Worker name","kind":"spark","role":"future-role","workerNode":true,"workerLabel":"Cluster worker","workerHeadId":"head1","online":true,"metrics":{}})";
    assert(parse_node(compatibility.data(), compatibility.size(), "x", node, e, sizeof e));
    assert(node.role == Role::Worker && !strcmp(node.worker, "Cluster worker") &&
           !strcmp(node.head, "head1") && !strcmp(node.kind, "spark"));
    std::string fallback_json =
        R"({"id":"x","online":true,"metrics":{"gpu":{"vram":{"used":0}},"unifiedMemory":{"used":5,"total":9,"available":0,"percentage":10},"storage":[{"device":"nvme0n1p2","used":3,"total":7}],"network":{"interfaces":[{"name":"disabled","disabled":true,"operstate":"up","rxSpeed":3},{"name":"down","operstate":"down","rxSpeed":4},{"name":"enabled","operstate":"up","rxSpeed":5}]}}})";
    assert(parse_node(fallback_json.data(), fallback_json.size(), "x", node, e, sizeof e));
    assert(node.used.valid && node.used.value == 0 && node.total.value == 9 &&
           node.available.valid && node.available.value == 0 && node.percent.value == 10);
    assert(node.disk_used.value == 3 && !strcmp(node.iface, "enabled") && node.rx.value == 5);
    std::string long_name(94, 'a'), long_model(159, 'm');
    std::string labels = R"({"id":"x","name":")" + long_name + "€" +
                         R"(","online":true,"metrics":{"llm":[{"available":true,"modelId":")" +
                         long_model + "€" + R"("}]}})";
    assert(parse_node(labels.data(), labels.size(), "x", node, e, sizeof e));
    assert(!strcmp(node.name, long_name.c_str()) && !strcmp(node.model, long_model.c_str()));
    Cache changes;
    std::string first = R"({"sparks":[{"id":"a","kind":"old"},{"id":"b"}]})";
    assert(parse_list(first.data(), first.size(), changes, e, sizeof e));
    changes.selected = 1;
    std::string added = R"({"sparks":[{"id":"a","kind":"new"},{"id":"b"},{"id":"c"}]})";
    Cache next_list;
    assert(parse_list(added.data(), added.size(), next_list, e, sizeof e));
    reconcile(changes, next_list);
    assert(changes.count == 3 && changes.selected == 1 && !changes.nodes[2].received);
    assert(!strcmp(changes.nodes[0].kind, "new"));
    std::string removed = R"({"sparks":[{"id":"a"},{"id":"c"}]})";
    assert(parse_list(removed.data(), removed.size(), next_list, e, sizeof e));
    reconcile(changes, next_list);
    assert(changes.selected == 1 && !strcmp(changes.nodes[1].id, "c"));
    std::string empty = R"({"sparks":[]})";
    assert(parse_list(empty.data(), empty.size(), next_list, e, sizeof e));
    reconcile(changes, next_list);
    assert(changes.count == 0 && changes.selected == 0);
    std::string overlong_id = R"({"sparks":[{"id":")" + std::string(65, 'i') + R"("}]})";
    assert(!parse_list(overlong_id.data(), overlong_id.size(), changes, e, sizeof e));
    std::string duplicated_overflow = many;
    duplicated_overflow.replace(duplicated_overflow.rfind("n16"), 3, "n0");
    assert(
        !parse_list(duplicated_overflow.data(), duplicated_overflow.size(), changes, e, sizeof e));
    assert(changes.count == 0);
    Scheduler q;
    auto w = q.next(0, 2, 0);
    assert(w.kind == Scheduler::Kind::List);
    q.completed(w, 0, 2);
    w = q.next(0, 2, 0);
    assert(w.kind == Scheduler::Kind::Foreground);
    q.completed(w, 100, 2);
    w = q.next(100, 2, 0);
    assert(w.kind == Scheduler::Kind::Background && w.index == 1);
    q.completed(w, 200, 2);
    assert(q.next(300, 2, 0).kind == Scheduler::Kind::None);
    q.select();
    assert(q.next(300, 2, 1).index == 1);
    q.failed(1000);
    q.failed(1000);
    q.failed(1000);
    assert(q.retry_due == 3000);
    assert(q.next(2999, 2, 1).kind == Scheduler::Kind::None);
    q.success();
    assert(q.retry_due == 0);
    q.completed({Scheduler::Kind::Foreground, 1}, 10000, 2);
    assert(q.foreground_due == 12000);
    std::cout << "All core assertions passed. Node=" << sizeof(Node) << " Cache=" << sizeof(Cache)
              << " bytes\n";
}
