/*
 * no-shaped-systray.so —— 去掉 wine 托盘图标窗口的 bounding shape
 *
 * wine 的 x11 driver 在画分层窗口（托盘图标就是）时，会按图标的 alpha 通道给
 * 窗口设一个 ShapeBounding 区域（dlls/winex11.drv/bitblt.c 里 surface shape 那
 * 段）。没有合成器的桌面上，这个 shape 顺便实现了"透明的地方点击穿透"。
 *
 * 开了合成器以后就不需要 shape 了：窗口本身就是 32 位 ARGB，透明处 alpha 为 0。
 * 但 Xfce panel 是把托盘 socket 单独合成出来的，它不按 alpha 处理这个 shape，
 * 而是把 shape 圈住的区域整块填黑，于是托盘图标变成黑底方块，边上还多一圈
 * 边框。把 bounding shape 去掉，面板就老老实实按 alpha 画图标了。
 *
 * 做法是在 wine 进程里拦一道 XShapeCombine*：只在有合成器、且目标是那种几十
 * 像素的 32 位托盘窗口时，丢掉 ShapeBounding 的设置。ShapeInput 一律不动，
 * 透明处的点击穿透行为保持原样。
 *
 * 用法：LD_PRELOAD=/usr/lib/wecom-win-bin/no-shaped-systray.so wine ...
 * 启动器里已经设好，不需要手工带。
 */
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

/* 托盘图标就二十来像素，留点余量认高 DPI 下的 32/48 */
#define TRAY_MAX_SIZE 64

static void (*real_shape_mask)(Display *, Window, int, int, int, Pixmap, int);
static void (*real_shape_rects)(Display *, Window, int, int, int, XRectangle *, int, int, int);

__attribute__((constructor))
static void load_real_symbols(void)
{
	real_shape_mask = dlsym(RTLD_NEXT, "XShapeCombineMask");
	real_shape_rects = dlsym(RTLD_NEXT, "XShapeCombineRectangles");
}

static void debug_log(const char *what, Window win)
{
	if (!getenv("WECOM_NOSHAPE_DEBUG")) {
		return;
	}
	fprintf(stderr, "no-shaped-systray: %s window %#lx\n", what, win);
}

static Bool compositor_active(Display *dpy)
{
	static Display *cached_dpy;
	static Atom cm_atom;
	static Bool cached;
	static time_t checked_at;

	if (dpy != cached_dpy) {
		cached_dpy = dpy;
		cm_atom = XInternAtom(dpy, "_NET_WM_CM_S0", False);
		cached = XGetSelectionOwner(dpy, cm_atom) != None;
		checked_at = time(NULL);
		return cached;
	}
	/* 合成器基本不会中途开关，几秒问一次就够，别给每次重绘都加一次往返 */
	if (time(NULL) - checked_at >= 5) {
		cached = XGetSelectionOwner(dpy, cm_atom) != None;
		checked_at = time(NULL);
	}
	return cached;
}

static Bool tray_icon_window(Display *dpy, Window win)
{
	XWindowAttributes attrs;

	/* depth 32 才有 alpha；尺寸卡小，普通窗口、菜单、tooltip 都不在这个范围 */
	if (!XGetWindowAttributes(dpy, win, &attrs)) {
		return False;
	}
	if (attrs.depth != 32 || attrs.width > TRAY_MAX_SIZE || attrs.height > TRAY_MAX_SIZE) {
		return False;
	}

	/* XEMBED 的托盘图标带 _XEMBED_INFO，并且会被面板 reparent 进 socket */
	{
		Atom atom = XInternAtom(dpy, "_XEMBED_INFO", False);
		Atom type;
		int format;
		unsigned long count, remaining;
		unsigned char *data = NULL;

		if (XGetWindowProperty(dpy, win, atom, 0, 1, False, XA_INTEGER,
				       &type, &format, &count, &remaining, &data) == Success) {
			if (data) {
				XFree(data);
			}
			if (type != None) {
				return True;
			}
		}
	}

	{
		Window root, parent, *children = NULL;
		unsigned int nchildren = 0;
		Bool embedded = XQueryTree(dpy, win, &root, &parent, &children, &nchildren) &&
				parent != root;

		if (children) {
			XFree(children);
		}
		return embedded;
	}
}

static Bool drop_bounding_shape(Display *dpy, Window win, int dest)
{
	if (dest != ShapeBounding) {
		return False;
	}
	return compositor_active(dpy) && tray_icon_window(dpy, win);
}

void XShapeCombineMask(Display *dpy, Window win, int dest, int x, int y,
		       Pixmap src, int op)
{
	if (!real_shape_mask) {
		load_real_symbols();
	}
	/* src == None 是"清掉 shape"，这种要放过去，否则会留下旧的剪裁 */
	if (src != None && drop_bounding_shape(dpy, win, dest)) {
		debug_log("drop bounding mask of", win);
		return;
	}
	real_shape_mask(dpy, win, dest, x, y, src, op);
}

void XShapeCombineRectangles(Display *dpy, Window win, int dest, int x, int y,
			     XRectangle *rects, int n_rects, int op, int ordering)
{
	int i;

	if (!real_shape_rects) {
		load_real_symbols();
	}
	/* 空区域（宽高为 0）是 wine 用来把窗口藏起来的，别碰 */
	for (i = 0; i < n_rects; i++) {
		if (rects[i].width == 0 || rects[i].height == 0) {
			goto forward;
		}
	}
	if (drop_bounding_shape(dpy, win, dest)) {
		debug_log("drop bounding rects of", win);
		return;
	}

forward:
	real_shape_rects(dpy, win, dest, x, y, rects, n_rects, op, ordering);
}
