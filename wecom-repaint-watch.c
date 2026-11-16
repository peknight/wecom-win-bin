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
 * transition, asks the main window to repaint the area the panel used to
 * cover.  Invalidating it without RDW_ERASE makes WeCom redraw the real
 * Messages content instead of clearing to a white rectangle.
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
#define REPAINT_FIRST_MS 250
#define REPAINT_STEP_MS 450
#define REPAINT_SHOTS 4
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
static HWND hidden_panel;

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

/* 面板整个盖住了内容区，收起后那块地方全都是陈的：不只是顶部工具栏，
 * 左边的会话列表也是。所以按面板原位置整块重画，而不是只打一条窄带。
 * 面板句柄拿不到时退回整块客户区。 */
static void repaint_panel_area(HWND panel)
{
    RECT client;
    RECT panel_rc;
    POINT top_left;
    POINT bottom_right;
    RECT area;
    BOOL ok;

    if (!GetClientRect(main_wnd, &client)) {
        logmsg("GetClientRect failed");
        return;
    }
    area = client;
    if (panel && GetWindowRect(panel, &panel_rc)) {
        top_left.x = panel_rc.left;
        top_left.y = panel_rc.top;
        bottom_right.x = panel_rc.right;
        bottom_right.y = panel_rc.bottom;
        ScreenToClient(main_wnd, &top_left);
        ScreenToClient(main_wnd, &bottom_right);
        if (bottom_right.x > client.left && top_left.x < client.right &&
            bottom_right.y > client.top && top_left.y < client.bottom) {
            area.left = top_left.x > client.left ? top_left.x : client.left;
            area.top = top_left.y > client.top ? top_left.y : client.top;
            area.right = bottom_right.x < client.right ? bottom_right.x : client.right;
            area.bottom = bottom_right.y < client.bottom ? bottom_right.y : client.bottom;
        }
    }
    if (area.right <= area.left || area.bottom <= area.top) {
        return;
    }
    ok = RedrawWindow(main_wnd, &area, NULL,
                      RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE |
                          RDW_ALLCHILDREN);
    logmsg("repaint %ld,%ld-%ld,%ld ok=%d", area.left, area.top,
           area.right, area.bottom, ok);
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
         * 冷启动时抓不到，所以要一直重扫到两个句柄都拿到为止。
         * 切走的时候它还可能被整个销毁，所以"句柄没了"必须当成收起，
         * 不能顺手把基线重置掉——否则这次切换就静默漏过了。 */
        if (!IsWindow(mail_wnd) || !IsWindow(doc_wnd)) {
            HWND old_mail = mail_wnd;
            HWND old_doc = doc_wnd;

            mail_wnd = NULL;
            doc_wnd = NULL;
            EnumChildWindows(main_wnd, find_panels, 0);
            if (mail_wnd != old_mail || doc_wnd != old_doc) {
                logmsg("panel handles mail=%p->%p doc=%p->%p", old_mail,
                       mail_wnd, old_doc, doc_wnd);
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
            hidden_panel = (mail_visible && !mail_now) ? mail_wnd : doc_wnd;
            repaints_left = REPAINT_SHOTS;
            next_repaint = GetTickCount() + REPAINT_FIRST_MS;
        }
        mail_visible = mail_now;
        doc_visible = doc_now;

        if (repaints_left && (LONG)(GetTickCount() - next_repaint) >= 0) {
            if (IsWindow(main_wnd) && !IsIconic(main_wnd)) {
                repaint_panel_area(hidden_panel);
            }
            repaints_left--;
            next_repaint = GetTickCount() + REPAINT_STEP_MS;
        }

        Sleep(POLL_MS);
    }
}
