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
 * One process per session: SHOW/HIDE win events drive the repaint queue,
 * while a 250 ms scan keeps track of the lazily created panel handles.
 * WECOM_WIN_NO_REPAINT_WATCH=1 in the launcher skips it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POLL_MS 250
/* WeCom keeps painting for roughly 0.6 s after a panel disappears.  A
 * repaint before that gets covered by the stale page again, which looks
 * like a flash.  Wait until the application has gone quiet, then take
 * two shots so a late paint still gets corrected. */
#define REPAINT_QUIET_MS 700
#define REPAINT_STEP_MS 300
#define REPAINT_SHOTS 2
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
static DWORD last_panel_event;

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

static BOOL is_panel_class(HWND hwnd)
{
    char cls[128];

    if (!GetClassNameA(hwnd, cls, sizeof(cls))) {
        return FALSE;
    }
    return strncmp(cls, "WXworkWindow - WeCom-Email", 27) == 0 ||
           strncmp(cls, "WXworkWindow - WeCom-Document", 30) == 0;
}

static void cancel_repaints(const char *reason)
{
    if (repaints_left) {
        logmsg("%s: cancel %d pending repaint(s)", reason, repaints_left);
    }
    repaints_left = 0;
    hidden_panel = NULL;
}

/* Polling can easily miss a SHOW/HIDE pair that happens inside one tick.
 * Treat every panel event as activity and cancel the delayed repaint
 * immediately, so it can never paint the Messages page over a panel that
 * is in the middle of being switched in. */
static void CALLBACK on_panel_event(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                    LONG id_object, LONG id_child,
                                    DWORD thread, DWORD time)
{
    (void)hook;
    (void)id_object;
    (void)id_child;
    (void)thread;
    (void)time;
    if (!hwnd || !is_panel_class(hwnd)) {
        return;
    }
    last_panel_event = GetTickCount();
    if (event == EVENT_OBJECT_SHOW) {
        cancel_repaints("panel shown");
    } else if (repaints_left) {
        /* A hide during an active switch is not a settled Messages page.
         * Restart the quiet window instead of cancelling outright: the
         * final hide still needs the parent repaint. */
        next_repaint = last_panel_event + REPAINT_QUIET_MS;
    }
}

/* Wine delivers out-of-context WinEvent hooks through the thread message
 * queue, so the polling loop has to keep pumping messages. */
static void wait_and_pump(DWORD timeout)
{
    MSG msg;
    DWORD result;

    result = MsgWaitForMultipleObjectsEx(0, NULL, timeout, QS_ALLINPUT,
                                         MWMO_INPUTAVAILABLE);
    if (result != WAIT_OBJECT_0) {
        return;
    }
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
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
    DWORD started;

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
    started = GetTickCount();
    ok = RedrawWindow(main_wnd, &area, NULL,
                      RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE |
                          RDW_ALLCHILDREN);
    logmsg("repaint %ld,%ld-%ld,%ld ok=%d took=%lums", area.left, area.top,
           area.right, area.bottom, ok, GetTickCount() - started);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show)
{
    HANDLE mutex;
    HWINEVENTHOOK show_hook;
    HWINEVENTHOOK hide_hook;
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

    show_hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL,
                                on_panel_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    hide_hook = SetWinEventHook(EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE, NULL,
                                on_panel_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!show_hook || !hide_hook) {
        logmsg("panel event hooks unavailable show=%p hide=%p", show_hook,
               hide_hook);
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
                wait_and_pump(POLL_MS);
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
        /* A panel can be opened again before all delayed repaints have run
         * (for example Messages -> Emails -> Messages -> Document within
         * two seconds).  Repainting the main window after the next panel is
         * already visible makes the panel flicker between its real content
         * and the Messages page underneath.  Once any panel is visible,
         * the stale-parent problem no longer applies, so drop the queue. */
        if ((!mail_visible && mail_now) || (!doc_visible && doc_now)) {
            cancel_repaints("panel shown");
        }
        if ((mail_visible && !mail_now) || (doc_visible && !doc_now)) {
            logmsg("panel hidden mail=%d->%d doc=%d->%d", mail_visible,
                   mail_now, doc_visible, doc_now);
            /* WeCom keeps painting for a while after the switch, so one
             * early repaint can be overwritten.  Spread a few shots out
             * and let the last one win. */
            hidden_panel = (mail_visible && !mail_now) ? mail_wnd : doc_wnd;
            repaints_left = REPAINT_SHOTS;
            next_repaint = GetTickCount() + REPAINT_QUIET_MS;
        }
        mail_visible = mail_now;
        doc_visible = doc_now;

        if (repaints_left && (LONG)(GetTickCount() - next_repaint) >= 0) {
            /* The poll above can miss a hide/show pair that happens inside
             * one 250 ms tick.  Check the live state again right before the
             * synchronous repaint and never paint over a visible panel. */
            if ((mail_wnd && IsWindowVisible(mail_wnd)) ||
                (doc_wnd && IsWindowVisible(doc_wnd)) ||
                (last_panel_event &&
                 GetTickCount() - last_panel_event < REPAINT_QUIET_MS)) {
                cancel_repaints("panel activity");
            } else if (IsWindow(main_wnd) && !IsIconic(main_wnd)) {
                repaint_panel_area(hidden_panel);
                repaints_left--;
                next_repaint = GetTickCount() + REPAINT_STEP_MS;
                continue;
            } else {
                cancel_repaints("main window unavailable");
            }
        }

        wait_and_pump(POLL_MS);
    }
}
