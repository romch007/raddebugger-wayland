// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef OS_GFX_LINUX_H
#define OS_GFX_LINUX_H

////////////////////////////////
//~ rjf: Includes

#undef global
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>
#define global static

////////////////////////////////
//~ rjf: Window State

typedef struct OS_LNX_TitleBarClientArea OS_LNX_TitleBarClientArea;
struct OS_LNX_TitleBarClientArea
{
  OS_LNX_TitleBarClientArea *next;
  Rng2F32 rect;
};

typedef struct OS_LNX_Window OS_LNX_Window;
struct OS_LNX_Window
{
  OS_LNX_Window *next;
  OS_LNX_Window *prev;
  struct wl_surface *surface;
  struct wp_viewport *viewport;
  struct wp_fractional_scale_v1 *fractional_scale;
  float scale;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  struct wl_egl_window *egl_window;

  int width;
  int height;

  double mouse_x;
  double mouse_y;

  uint32_t last_click_time;
  double last_click_x;
  double last_click_y;

  B32 is_maximized;
  B32 is_fullscreen;

  Arena *title_bar_arena;
  F32 title_bar_thickness;
  F32 edge_thickness;
  OS_LNX_TitleBarClientArea *first_title_bar_client_area;
  OS_LNX_TitleBarClientArea *last_title_bar_client_area;
};

////////////////////////////////
//~ rjf: State Bundle

typedef struct OS_LNX_GfxState OS_LNX_GfxState;
struct OS_LNX_GfxState
{
  Arena *arena;

  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct xdg_wm_base *wm_base;
  struct wl_seat *seat;
  struct wl_pointer *pointer;
  uint32_t pointer_serial;
  struct wl_surface *cursor_surface;
  struct wl_cursor_theme *cursor_theme;
  struct wl_keyboard *keyboard;
  struct xkb_context *xkb_ctx;
  struct xkb_keymap *xkb_keymap;
  struct xkb_state *xkb_state;
  struct wl_shm *shm;
  struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
  struct wp_viewporter *viewporter;
  OS_LNX_Window *pointer_focus;
  B32 force_border_cursor;

  OS_LNX_Window *first_window;
  OS_LNX_Window *last_window;
  OS_LNX_Window *free_window;
  OS_Cursor last_set_cursor;
  OS_GfxInfo gfx_info;
};

////////////////////////////////
//~ rjf: Globals

global OS_LNX_GfxState *os_lnx_gfx_state = 0;
global OS_EventList os_lnx_event_list = {0};
global Arena *os_lnx_event_arena = 0;

////////////////////////////////
//~ rjf: Helpers

internal OS_LNX_Window *os_lnx_window_from_wlsurface(struct wl_surface* surface);

#endif // OS_GFX_LINUX_H
