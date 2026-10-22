/* particle_desktop.c - fullscreen transparent particle trail + heart gather
 *
 * A click-through, topmost, full-screen transparent overlay. The desktop stays
 * fully visible and clickable while colorful particles follow the mouse cursor.
 *
 * Controls:
 *   move mouse    : particle trail follows the cursor (desktop-wide)
 *   left click    : gather into a heart at the cursor
 *   left click    : (while gathered) burst then re-gather
 *   right click   : back to trail mode
 *   Esc           : exit
 *
 * Build (MSVC):
 *   cl particle_desktop.c /Fe:particle_desktop.exe /O2 /link user32.lib gdi32.lib /SUBSYSTEM:WINDOWS
 * Build (MinGW-w64 / MSYS2):
 *   gcc particle_desktop.c -o particle_desktop.exe -lgdi32 -luser32 -mwindows -O2
 */

#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define COUNT    2000
#define MAX_GLOW 22.0f
#define TAU      6.28318530717958647692f

typedef struct { float r, g, b; } RGBf;

/* hot pink / magenta / coral with a few gold + white + purple sparkles */
static const RGBf PAL[] = {
    {255,  45, 120},
    {255,  90, 160},
    {255,  30,  90},
    {255, 140, 190},
    {255, 200, 230},
    {255, 220, 140},
    {255, 255, 255},
    {180, 120, 255},
    {255,  70,  95},
};
static const int PW[] = { 34, 28, 20, 8, 3, 2, 2, 2, 1 };

typedef struct {
    float x, y, vx, vy;
    float life, maxlife, alpha;
    float size, phase, delay;
    float hx, hy;          /* heart target: curve point * radius factor */
    int   color;
} P;

typedef struct {
    P     p[COUNT];
    int   w, h;
    int   mode;            /* 0 = trail, 1 = heart */
    int   spawn;
    float emit;
    float cx, cy, pcx, pcy;/* cursor pos (buffer coords), current + previous */
    float hx, hy;          /* heart center (buffer coords) */
    float scale;
    float clock, age, burst;
    int   wasLB, wasRB, wasEsc;
} App;

static App    app;
static HWND   g_hwnd;
static HDC    g_memDC; static HBITMAP g_bmp, g_oldBmp;
static BYTE  *g_bits;
static int    g_vsx, g_vsy, g_vsw, g_vsh;
static double g_qfreq;

/* ---------------- helpers ---------------- */

static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static float rrange(float lo, float hi) { return lo + (hi - lo) * frand(); }

static float gauss(float mu, float sigma) {
    float u1 = frand(); if (u1 < 1e-6f) u1 = 1e-6f;
    float u2 = frand();
    return mu + sigma * sqrtf(-2.0f * logf(u1)) * cosf(TAU * u2);
}

static float smooth(float x) {
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static int weighted_color(void) {
    int r = rand() % 100, c = 0;
    for (int i = 0; i < 9; i++) { c += PW[i]; if (r < c) return i; }
    return 8;
}

static void heart_curve(float t, float *hx, float *hy) {
    float s = sinf(t);
    *hx = 16.0f * s * s * s;
    *hy = -(13.0f * cosf(t) - 5.0f * cosf(2.0f * t)
            - 2.0f * cosf(3.0f * t) - cosf(4.0f * t));
}

/* ---------------- particles ---------------- */

static void respawn(P *p, float x, float y) {
    p->x = x + gauss(0.0f, 3.0f);
    p->y = y + gauss(0.0f, 3.0f);
    p->vx = rrange(-40.0f, 40.0f);
    p->vy = rrange(-40.0f, 40.0f);
    p->maxlife = rrange(0.6f, 1.5f);
    p->life = p->maxlife;
    p->size = rrange(1.3f, 3.8f);
    p->phase = rrange(0.0f, TAU);
    p->color = weighted_color();
    if (p->color == 5 || p->color == 6 || p->color == 7) p->size *= 1.3f;
    p->alpha = 0.0f;
}

static void trail_update(App *a, float dt) {
    float vx = 0.0f, vy = 0.0f;
    if (dt > 1e-4f) { vx = (a->cx - a->pcx) / dt; vy = (a->cy - a->pcy) / dt; }
    float speed = sqrtf(vx * vx + vy * vy);

    float rate = 130.0f + 0.55f * speed;
    if (rate > 950.0f) rate = 950.0f;
    a->emit += rate * dt;

    int n = 0;
    while (a->emit >= 1.0f && n < 80) {
        a->emit -= 1.0f; n++;
        P *p = &a->p[a->spawn];
        a->spawn = (a->spawn + 1) % COUNT;
        respawn(p, a->cx, a->cy);
        p->vx = -vx * 0.10f + rrange(-35.0f, 35.0f);
        p->vy = -vy * 0.10f + rrange(-35.0f, 35.0f);
    }

    float damp = expf(-3.0f * dt);
    for (int i = 0; i < COUNT; i++) {
        P *p = &a->p[i];
        p->life -= dt;
        p->vx *= damp; p->vy *= damp;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        float l = p->life / p->maxlife;
        p->alpha = smooth(l / 0.8f);
        if (p->life <= 0.0f) p->alpha = 0.0f;
    }
}

static void start_heart(App *a, float x, float y) {
    a->mode = 1;
    a->age = 0.0f; a->burst = 0.0f;
    a->hx = x;
    a->hy = y - 8.0f * a->scale;

    for (int i = 0; i < COUNT; i++) {
        P *p = &a->p[i];
        float t = rrange(0.0f, TAU), hx, hy;
        heart_curve(t, &hx, &hy);

        float kind = frand(), rf;
        if (kind < 0.45f)      rf = gauss(1.0f, 0.03f);
        else if (kind < 0.90f) rf = powf(frand(), 0.55f);
        else                   rf = rrange(1.04f, 1.20f);

        p->hx = hx * rf + gauss(0.0f, 0.12f);
        p->hy = (hy - 2.5f) * rf + gauss(0.0f, 0.12f);
        p->delay = rrange(0.0f, 0.8f);

        if (p->life <= 0.0f || p->alpha < 0.02f) {
            p->x = x + gauss(0.0f, 25.0f);
            p->y = y + gauss(0.0f, 25.0f);
            p->alpha = 0.0f;
            p->life = 0.5f; p->maxlife = 0.5f;
            p->size = rrange(1.3f, 3.8f);
            p->color = weighted_color();
        }
        p->vx *= 0.2f; p->vy *= 0.2f;
    }
}

static void burst(App *a, float x, float y) {
    for (int i = 0; i < COUNT; i++) {
        P *p = &a->p[i];
        float dx = p->x - x, dy = p->y - y;
        float d = sqrtf(dx * dx + dy * dy);
        float ux, uy;
        if (d > 1.0f) { ux = dx / d; uy = dy / d; }
        else { ux = cosf(rrange(0.0f, TAU)); uy = sinf(rrange(0.0f, TAU)); }
        float ang = atan2f(uy, ux) + rrange(-0.35f, 0.35f);
        float sp = rrange(280.0f, 620.0f);
        p->vx = cosf(ang) * sp + rrange(-25.0f, 25.0f);
        p->vy = sinf(ang) * sp + rrange(-25.0f, 25.0f);
    }
    a->burst = 1.35f;
    if (a->age < 1.2f) a->age = 1.2f;
}

static void heart_update(App *a, float dt) {
    a->age += dt;
    if (a->burst > 0.0f) a->burst -= dt; else a->burst = 0.0f;

    float breath = 1.0f + 0.045f * sinf(a->clock * 1.6f)
                       + 0.012f * sinf(a->clock * 3.2f);
    float scale = a->scale * breath;
    float g = smooth((a->age < 1.2f) ? a->age / 1.2f : 1.0f);
    float spring = (4.0f + 42.0f * g) * (1.0f - smooth(a->burst / 1.15f));
    float damp = expf(-((a->burst > 1.0f) ? 0.65f : 7.5f) * dt);
    float blend = 1.0f - expf(-7.0f * dt);

    for (int i = 0; i < COUNT; i++) {
        P *p = &a->p[i];
        float tx = a->hx + p->hx * scale;
        float ty = a->hy + p->hy * scale;
        p->vx += (tx - p->x) * spring * dt;
        p->vy += (ty - p->y) * spring * dt;
        p->vx *= damp; p->vy *= damp;
        p->x += p->vx * dt; p->y += p->vy * dt;

        float gg = smooth((a->age - p->delay) / 1.8f);
        p->alpha += (gg - p->alpha) * blend;
        p->life = 0.5f;
    }
}

static void reset_trail(App *a) {
    a->mode = 0;
    a->emit = 0.0f;
    for (int i = 0; i < COUNT; i++) {
        P *p = &a->p[i];
        p->life = 0.0f; p->alpha = 0.0f;
        p->x = a->cx; p->y = a->cy;
    }
}

/* ---------------- rendering (per-pixel alpha) ---------------- */

static void stamp(BYTE *bits, int w, int h, float fx, float fy,
                  float size, float I, const RGBf *col) {
    float gR = size * 3.2f + 2.0f;
    if (gR > MAX_GLOW) gR = MAX_GLOW;
    if (gR < 2.0f) gR = 2.0f;
    float cR = size * 1.15f + 0.5f;

    int r = (int)(gR + 1.0f);
    int x0 = (int)fx - r, x1 = (int)fx + r;
    int y0 = (int)fy - r, y1 = (int)fy + r;
    if (x0 < 0) x0 = 0; if (x1 > w - 1) x1 = w - 1;
    if (y0 < 0) y0 = 0; if (y1 > h - 1) y1 = h - 1;
    if (x1 < x0 || y1 < y0) return;

    float gR2 = gR * gR, cR2 = cR * cR;
    float cr = col->r * I, cg = col->g * I, cb = col->b * I;
    float aM = I * 255.0f;

    for (int y = y0; y <= y1; y++) {
        int row = y * w;
        float dy = (float)y - fy;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - fx;
            float q = dx * dx + dy * dy;
            if (q > gR2) continue;

            float f = 1.0f - q / gR2; f *= f;
            float a = f * aM;
            if (a < 0.5f) continue;

            int core = 0;
            if (q < cR2) {
                float fc = 1.0f - q / cR2; fc *= fc;
                core = (int)(fc * I * 220.0f);   /* white-hot center */
            }

            int idx = (row + x) * 4;
            int bb = bits[idx]   + (int)(cb * f) + core;
            int gg = bits[idx+1] + (int)(cg * f) + core;
            int rr = bits[idx+2] + (int)(cr * f) + core;
            int aa = bits[idx+3] + (int)a;
            if (bb > 255) bb = 255; if (gg > 255) gg = 255;
            if (rr > 255) rr = 255; if (aa > 255) aa = 255;
            if (rr > aa) rr = aa; if (gg > aa) gg = aa; if (bb > aa) bb = aa;
            bits[idx]   = (BYTE)bb;
            bits[idx+1] = (BYTE)gg;
            bits[idx+2] = (BYTE)rr;
            bits[idx+3] = (BYTE)aa;
        }
    }
}

static void render(App *a, BYTE *bits, int w, int h) {
    memset(bits, 0, (size_t)w * h * 4);
    float pulse = 1.0f + 0.12f * sinf(a->clock * 2.5f);
    for (int i = 0; i < COUNT; i++) {
        P *p = &a->p[i];
        if (p->alpha <= 0.02f) continue;
        float tw = 0.82f + 0.18f * sinf(a->clock * 3.0f + p->phase);
        float I = p->alpha * tw;
        if (I <= 0.02f) continue;
        stamp(bits, w, h, p->x, p->y, p->size * pulse, I, &PAL[p->color]);
    }
}

/* ---------------- window / buffer ---------------- */

static void update_buffer(void) {
    if (g_memDC) {
        SelectObject(g_memDC, g_oldBmp);
        DeleteObject(g_bmp);
        DeleteDC(g_memDC);
    }
    BITMAPINFO bi; memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_vsw;
    bi.bmiHeader.biHeight = -g_vsh;         /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(NULL);
    g_memDC = CreateCompatibleDC(dc);
    g_bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&g_bits, NULL, 0);
    ReleaseDC(NULL, dc);
    g_oldBmp = (HBITMAP)SelectObject(g_memDC, g_bmp);

    app.w = g_vsw; app.h = g_vsh;
    app.scale = (float)(g_vsw < g_vsh ? g_vsw : g_vsh) * 0.012f;
}

static void update_screen(void) {
    g_vsx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_vsy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_vsw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_vsh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (g_hwnd)
        SetWindowPos(g_hwnd, HWND_TOPMOST, g_vsx, g_vsy, g_vsw, g_vsh,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    update_buffer();
}

static void present(void) {
    POINT src = { 0, 0 }, dst = { g_vsx, g_vsy };
    SIZE sz = { g_vsw, g_vsh };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_hwnd, NULL, &dst, &sz, g_memDC, &src, 0, &bf, ULW_ALPHA);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_DISPLAYCHANGE: update_screen(); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd; (void)nShow;
    SetProcessDPIAware();
    srand((unsigned)time(NULL));

    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qfreq = (double)f.QuadPart;

    g_vsx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_vsy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_vsw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_vsh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    WNDCLASS wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "ParticleDesktopWnd";
    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "ParticleDesktopWnd", "Particle Heart",
        WS_POPUP, g_vsx, g_vsy, g_vsw, g_vsh,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    update_buffer();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    for (int i = 0; i < COUNT; i++) {
        app.p[i].maxlife = 1.0f;
        app.p[i].life = 0.0f;
        app.p[i].alpha = 0.0f;
        app.p[i].size = 2.0f;
        app.p[i].color = 0;
    }

    POINT pt; GetCursorPos(&pt);
    app.cx = app.pcx = (float)(pt.x - g_vsx);
    app.cy = app.pcy = (float)(pt.y - g_vsy);

    LARGE_INTEGER c0; QueryPerformanceCounter(&c0);
    double last = (double)c0.QuadPart / g_qfreq;

    MSG msg;
    int running = 1;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        LARGE_INTEGER c; QueryPerformanceCounter(&c);
        double now = (double)c.QuadPart / g_qfreq;
        float dt = (float)(now - last); last = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.0f) dt = 0.0f;

        int lbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        int rbDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        int escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;

        GetCursorPos(&pt);
        app.pcx = app.cx; app.pcy = app.cy;
        app.cx = (float)(pt.x - g_vsx);
        app.cy = (float)(pt.y - g_vsy);

        if (lbDown && !app.wasLB) {
            if (app.mode == 0) start_heart(&app, app.cx, app.cy);
            else burst(&app, app.cx, app.cy);
        }
        if (rbDown && !app.wasRB) reset_trail(&app);
        if (escDown && !app.wasEsc) running = 0;
        app.wasLB = lbDown; app.wasRB = rbDown; app.wasEsc = escDown;

        app.clock += dt;
        if (app.mode == 0) trail_update(&app, dt);
        else heart_update(&app, dt);

        render(&app, g_bits, g_vsw, g_vsh);
        present();
        Sleep(1);
    }

    if (g_memDC) { SelectObject(g_memDC, g_oldBmp); DeleteObject(g_bmp); DeleteDC(g_memDC); }
    if (g_hwnd) DestroyWindow(g_hwnd);
    return 0;
}
