/*
 * WeCom repaint watchdog for Wine.
 *
 * WeCom draws Messages directly in its main window and puts Emails /
 * Document in child windows on top of it.  When one of those child
 * windows is hidden again, Wine's win32u only erases the exposed parent
 * background (erase_now() sends WM_ERASEBKGND) but never invalidates the
 * parent client area, so the old mail toolbar pixels stay on screen until
 * something else forces a WM_PAINT.
 *
 * This process polls the two panel windows and, on a visible -> hidden
 * transition, asks the main window to repaint its top strip.  The strip
 * holds the stale toolbar; invalidating it without RDW_ERASE makes WeCom
 * redraw the real Messages header and leaves the chat view untouched.
 *
 * One process per session, 250 ms poll of a few window handles.
 * WECOM_WIN_NO_REPAINT_WATCH=1 in the launcher skips it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POLL_MS 250
#define REPAINT_FIRST_MS 700
#define REPAINT_STEP_MS 900
#define REPAINT_SHOTS 3
/* 残留工具栏相对面板顶边的位置，按 96 dpi 量出来的；高 dpi 下按比例放大 */
#define STRIP_TOP 20
#define STRIP_HEIGHT 60
/* 冷启动到出主窗口要十几秒，给足两分钟 */
#define STARTUP_GIVE_UP_MS 120000
/* 主窗口短暂消失（重建）时再等一会儿，超过就认为程序已退出 */
#define LOST_GIVE_UP_MS 5000

static HWND main_wnd;
static HWND mail_wnd;
static HWND doc_wnd;
static BOOL mail_visible;
static BOOL doc_visible;
static BOOL ever_found;
static int repaints_left;
static DWORD next_repaint;

static void logmsg(const char *fmt, ...)
{
    static char path[MAX_PATH];
    static BOOL checked;
    char line[512];
    va_list ap;
    FILE *file;

    if (!checked) {
        checked = TRUE;
        GetEnvironmentVariableA("WECOM_REPAINT_LOG", path, sizeof(path));
    }
    if (!path[0]) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    file = fopen(path, "a");
    if (file) {
        fprintf(file, "%lu %s\n", GetTickCount(), line);
        fclose(file);
    }
}

static BOOL CALLBACK find_main(HWND hwnd, LPARAM param)
{
    char cls[64];

    /* 只认窗口类，不看标题：标题会跟着界面语言变（WeCom / 企业微信） */
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    if (GetWindow(hwnd, GW_OWNER) != NULL) {
        return TRUE;
    }
    if (GetClassNameA(hwnd, cls, sizeof(cls)) == 0 ||
        strcmp(cls, "WeWorkWindow") != 0) {
        return TRUE;
    }
    *(HWND *)param = hwnd;
    return FALSE;
}

static BOOL CALLBACK find_panels(HWND hwnd, LPARAM param)
{
    char cls[128];

    (void)param;
    /* 只认窗口类前缀，不看标题：标题会跟着界面语言变（WeCom / 企业微信） */
    if (GetClassNameA(hwnd, cls, sizeof(cls)) == 0) {
        return TRUE;
    }
    if (strncmp(cls, "WXworkWindow - WeCom-Email", 27) == 0) {
        mail_wnd = hwnd;
    } else if (strncmp(cls, "WXworkWindow - WeCom-Document", 30) == 0) {
        doc_wnd = hwnd;
    }
    return TRUE;
}

static UINT window_dpi(HWND hwnd)
{
    typedef UINT(WINAPI * get_dpi_for_window_t)(HWND);
    static get_dpi_for_window_t get_dpi;
    static BOOL looked_up;
    UINT dpi;

    if (!looked_up) {
        looked_up = TRUE;
        get_dpi = (get_dpi_for_window_t)(void *)GetProcAddress(
            GetModuleHandleA("user32.dll"), "GetDpiForWindow");
    }
    dpi = get_dpi ? get_dpi(hwnd) : 0;
    return dpi ? dpi : 96;
}

static void repaint_strip(HWND panel)
{
    RECT client;
    RECT panel_rc;
    POINT top_left;
    LONG offset;
    LONG height;
    BOOL ok;

    if (!GetClientRect(main_wnd, &client)) {
        logmsg("GetClientRect failed");
        return;
    }
    /* 残留就是面板原先盖住的那块地方的最上面一截 */
    top_left.x = 0;
    top_left.y = 0;
    if (panel && GetWindowRect(panel, &panel_rc)) {
        top_left.x = panel_rc.left;
        top_left.y = panel_rc.top;
        ScreenToClient(main_wnd, &top_left);
    }
    offset = MulDiv(STRIP_TOP, window_dpi(main_wnd), 96);
    height = MulDiv(STRIP_HEIGHT, window_dpi(main_wnd), 96);
    client.left = 0;
    client.top = top_left.y + offset;
    client.bottom = client.top + height;
    if (client.top < 0) {
        client.top = 0;
    }
    {
        RECT full;

        GetClientRect(main_wnd, &full);
        if (client.bottom > full.bottom) {
            client.bottom = full.bottom;
        }
    }
    if (client.bottom <= client.top) {
        return;
    }
    ok = RedrawWindow(main_wnd, &client, NULL,
                      RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    logmsg("repaint %ld,%ld-%ld,%ld ok=%d", client.left, client.top,
           client.right, client.bottom, ok);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show)
{
    HANDLE mutex;
    DWORD started;
    DWORD lost_at;
    int attempt;

    (void)instance;
    (void)prev;
    (void)cmdline;
    (void)show;

    /* 上一次启动的实例可能还在收尾，等它释放再接手，别把自己挡在门外。
     * 旧实例在主窗口消失后还要守 LOST_GIVE_UP_MS 才退出，所以等待时间
     * 必须比它长，否则快速重启时会两个都退出、没人接手。 */
    for (attempt = 0; attempt < 32; attempt++) {
        mutex = CreateMutexA(NULL, FALSE, "wecom-repaint-watch");
        if (!mutex) {
            return 0;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            break;
        }
        CloseHandle(mutex);
        mutex = NULL;
        Sleep(250);
    }
    if (!mutex) {
        logmsg("another watcher is still running, giving up");
        return 0;
    }

    started = GetTickCount();
    lost_at = 0;
    for (;;) {
        BOOL mail_now;
        BOOL doc_now;

        if (!IsWindow(main_wnd)) {
            main_wnd = NULL;
            mail_wnd = NULL;
            doc_wnd = NULL;
            EnumWindows(find_main, (LPARAM)&main_wnd);
            if (!main_wnd) {
                if (!lost_at) {
                    lost_at = GetTickCount();
                }
                if (ever_found) {
                    /* 程序退出后主窗口就没了，跟着收工，别留个孤儿进程 */
                    if (GetTickCount() - lost_at > LOST_GIVE_UP_MS) {
                        logmsg("main window gone, exiting");
                        return 0;
                    }
                } else if (GetTickCount() - started > STARTUP_GIVE_UP_MS) {
                    logmsg("main window never appeared, exiting");
                    return 0;
                }
                Sleep(POLL_MS);
                continue;
            }
            lost_at = 0;
            ever_found = TRUE;
            EnumChildWindows(main_wnd, find_panels, 0);
            mail_visible = mail_wnd && IsWindowVisible(mail_wnd);
            doc_visible = doc_wnd && IsWindowVisible(doc_wnd);
            logmsg("main=%p mail=%p(%d) doc=%p(%d)", main_wnd, mail_wnd,
                   mail_visible, doc_wnd, doc_visible);
        }

        /* Emails / Document 面板是第一次点开左侧入口时才建的子窗口，
         * 冷启动时抓不到，所以要一直重扫到两个句柄都拿到为止。 */
        if (!IsWindow(mail_wnd) || !IsWindow(doc_wnd)) {
            HWND old_mail = IsWindow(mail_wnd) ? mail_wnd : NULL;
            HWND old_doc = IsWindow(doc_wnd) ? doc_wnd : NULL;

            mail_wnd = NULL;
            doc_wnd = NULL;
            EnumChildWindows(main_wnd, find_panels, 0);
            /* 新拿到的句柄要先记下当前可见性当基线，老句柄的基线不能动 */
            if (mail_wnd != old_mail) {
                mail_visible = mail_wnd && IsWindowVisible(mail_wnd);
            }
            if (doc_wnd != old_doc) {
                doc_visible = doc_wnd && IsWindowVisible(doc_wnd);
            }
        }

        mail_now = mail_wnd && IsWindowVisible(mail_wnd);
        doc_now = doc_wnd && IsWindowVisible(doc_wnd);
        if ((mail_visible && !mail_now) || (doc_visible && !doc_now)) {
            logmsg("panel hidden mail=%d->%d doc=%d->%d", mail_visible,
                   mail_now, doc_visible, doc_now);
            /* WeCom keeps painting for a while after the switch, so one
             * early repaint can be overwritten.  Spread a few shots out
             * and let the last one win. */
            repaints_left = REPAINT_SHOTS;
            next_repaint = GetTickCount() + REPAINT_FIRST_MS;
        }
        mail_visible = mail_now;
        doc_visible = doc_now;

        if (repaints_left && (LONG)(GetTickCount() - next_repaint) >= 0) {
            if (IsWindow(main_wnd) && !IsIconic(main_wnd)) {
                repaint_strip(!mail_now ? mail_wnd : doc_wnd);
            }
            repaints_left--;
            next_repaint = GetTickCount() + REPAINT_STEP_MS;
        }

        Sleep(POLL_MS);
    }
}
