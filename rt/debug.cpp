// Debug utilities for test scripts (--script ... eval <expr>)
#include "display.h"
#include <cstdio>
#include <sstream>
#include <vector>
#include "menuui.h"
#include "controls.h"
namespace fl {
static Value evalPath(const std::string& p) {
    Value cur((Object*)vm.root);
    size_t i = 0;
    std::string s = p; if (s.compare(0, 6, "_root.") == 0) s = s.substr(6); else if (s == "_root") return cur;
    while (i <= s.size()) {
        size_t j = s.find('.', i); std::string part = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!part.empty()) cur = getMemK(cur, intern(part));
        if (j == std::string::npos) break; i = j + 1;
    }
    return cur;
}
static void dumpTree(DisplayObject* d, int depth, int maxDepth) {
    if (depth > maxDepth) return;
    logf("%*s[%d] %s id=%d kind=%d vis=%d x=%.1f y=%.1f sx=%.2f fr=%d/%d\n", depth * 2, "", d->depth, d->name ? d->name->s.c_str() : "-", d->charId, d->kind,
         d->visible, d->m.tx, d->m.ty, d->sx, d->kind == OK_CLIP ? ((MovieClip*)d)->cur : 0, d->kind == OK_CLIP ? ((MovieClip*)d)->def->nframes : 0);
    if (d->kind == OK_CLIP) for (auto* k : ((MovieClip*)d)->kids) dumpTree(k, depth + 1, maxDepth);
}
void debugEval(const std::string& e) {
    if (e.compare(0, 5, "call ") == 0) {   // call <path> <method> [numeric args...]
        std::istringstream ss(e.substr(5)); std::string path, method; ss >> path >> method;
        std::vector<Value> args; std::string a; while (ss >> a) { double n = strToNum(a); args.push_back(std::isnan(n) ? Value(mkstr(a)) : Value(n)); }
        Act ac{vm.globalScope, VUNDEF, vm.root, nullptr};
        Value r = callMethodK(ac, evalPath(path), intern(method), (int)args.size(), args.data());
        runActions();
        logf("[call] %s.%s -> %s\n", path.c_str(), method.c_str(), toStr(r)->s.c_str());
        return;
    }
    if (e.compare(0, 7, "dumpbd ") == 0) {   // dumpbd <path> <file>: dumps a BitmapData (premultiplied RGBA, raw)
        std::istringstream ss(e.substr(7)); std::string path, file; ss >> path >> file;
        Value v = evalPath(path);
        if (!v.isObj() || v.o->kind != OK_BITMAPDATA) { logf("[dumpbd] %s is not a BitmapData\n", path.c_str()); return; }
        BitmapData* b = (BitmapData*)v.o; b->ensurePx(); FILE* f = fopen(file.c_str(), "wb"); if (!f) return;
        fprintf(f, "%d %d\n", b->w, b->h); fwrite(b->px.data(), 4, b->px.size(), f); fclose(f);
        logf("[dumpbd] %s %dx%d -> %s\n", path.c_str(), b->w, b->h, file.c_str());
        return;
    }
    if (e.compare(0, 10, "hidedepth ") == 0) {   // hidedepth <path> <depth>
        char path[256] = {0}; int dp = 0; sscanf(e.c_str() + 10, "%255s %d", path, &dp); Value v = evalPath(path);
        if (v.isObj() && v.o->kind == OK_CLIP) for (auto* k : ((MovieClip*)v.o)->kids) if (k->depth == dp) k->visible = false;
        return;
    }
    if (e.compare(0, 10, "shapeinfo ") == 0) { extern void debugShapeSubpaths(uint16_t); debugShapeSubpaths((uint16_t)atoi(e.c_str() + 10)); return; }
    if (e.compare(0, 5, "tree ") == 0) { int md = 3; char path[256] = {0}; sscanf(e.c_str() + 5, "%255s %d", path, &md); Value v = evalPath(path);
        if (v.isObj() && v.o->isDisplay()) dumpTree((DisplayObject*)v.o, 0, md); return; }
    if (e.compare(0, 4, "nav ") == 0) { padNavEvent(atoi(e.c_str() + 4)); return; }   // controller navigation (menuui.cpp)
    if (e.compare(0, 6, "rebind") == 0) {   // rebind [nav]: open the rebinding screen / send it a navigation code
        static RebindScreen w;
        if (e.compare(0, 11, "rebind key ") == 0) w.captureKey(atoi(e.c_str() + 11));
        else if (e.compare(0, 11, "rebind pad ") == 0) w.capturePad(atoi(e.c_str() + 11));
        else if (e == "rebind save") bindingsSave([](const std::string& k, const std::string& v) { logf("[ini] %s = %s\n", k.c_str(), v.c_str()); });
        else if (e.size() > 7) w.nav(atoi(e.c_str() + 7));
        else { bool av[DEV_COUNT] = {true, true, true}; w.open(av, DEV_KEYBOARD, 0); }
        return;
    }
    if (e == "buttons") { extern void debugListButtons(); debugListButtons(); return; }
    if (e.compare(0, 4, "set ") == 0) {
        size_t sp = e.find(' ', 4); std::string path = e.substr(4, sp - 4), val = e.substr(sp + 1);
        size_t dot = path.find_last_of('.'); Value obj = evalPath(path.substr(0, dot));
        double n = strToNum(val); setMemK(obj, intern(path.substr(dot + 1)), std::isnan(n) ? Value(mkstr(val)) : Value(n));
        return;
    }
    Value v = evalPath(e);
    logf("[eval] %s = %s\n", e.c_str(), toStr(v)->s.c_str());
}
}
