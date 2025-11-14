// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#include "xdg-shell-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-protocol.h"
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <gio/gio.h>

////////////////////////////////
//~ rjf: Helpers


////////////////////////////////
//~ rjf: @os_hooks Main Initialization API (Implemented Per-OS)

internal const int cursor_theme_size = 24;

internal OS_Event *
os_lnx_push_event(OS_EventKind kind, OS_LNX_Window *window)
{
  OS_Event *result = os_event_list_push_new(os_lnx_event_arena, &os_lnx_event_list, kind);
  OS_Handle h = {(U64)window};
  result->window = h;
  result->modifiers = os_get_modifiers();
  return result;
}

internal void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
                     uint32_t serial, struct wl_surface *surface,
                     wl_fixed_t sx, wl_fixed_t sy)
{
  OS_LNX_Window *window = wl_surface_get_user_data(surface);
  os_lnx_gfx_state->focused_window = window;
  os_lnx_gfx_state->pointer_serial = serial;
}

internal void
pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
                     uint32_t serial, struct wl_surface *surface)
{
  os_lnx_gfx_state->focused_window = NULL;
}

internal enum xdg_toplevel_resize_edge get_resize_edge(OS_LNX_Window *w) {
  int x = w->mouse_x;
  int y = w->mouse_y;
  int width = w->width * w->scale;
  int height = w->height * w->scale;
  int margin = w->edge_thickness * w->scale;

  B32 top = y < margin;
  B32 bottom = y > (height - margin);
  B32 left = x < margin;
  B32 right = x > (width - margin);

  if (top)
    if (left)
      return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    else if (right)
      return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    else
      return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
  else if (bottom)
    if (left)
      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    else if (right)
      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    else
      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
  else if (left)
    return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
  else if (right)
    return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
  else
    return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
}

internal void set_cursor(struct wl_cursor *wl_cursor) {
  OS_LNX_Window *window = os_lnx_gfx_state->focused_window;

  if (!window)
    return;

  struct wl_cursor_image *image = wl_cursor->images[0];
  struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);

  wl_pointer_set_cursor(os_lnx_gfx_state->pointer,
                        os_lnx_gfx_state->pointer_serial,
                        os_lnx_gfx_state->cursor_surface,
                        image->hotspot_x / window->scale,
                        image->hotspot_y / window->scale);
  wl_surface_attach(os_lnx_gfx_state->cursor_surface, buffer, 0, 0);
  wl_surface_damage_buffer(os_lnx_gfx_state->cursor_surface, 0, 0,
                           image->width, image->height);
  wl_surface_commit(os_lnx_gfx_state->cursor_surface);
}

internal void
pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
                      uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
  OS_LNX_Window *window = os_lnx_gfx_state->focused_window;

  if (!window)
    return;

  double mouse_x = wl_fixed_to_double(sx);
  double mouse_y = wl_fixed_to_double(sy);

  window->mouse_x = mouse_x * window->scale;
  window->mouse_y = mouse_y * window->scale;

  OS_Event *event = os_lnx_push_event(OS_EventKind_MouseMove, window);
  event->pos.x = mouse_x * window->scale;
  event->pos.y = mouse_y * window->scale;

  enum xdg_toplevel_resize_edge resize_edge = get_resize_edge(window);
  if (resize_edge != XDG_TOPLEVEL_RESIZE_EDGE_NONE && !window->is_maximized && !window->is_fullscreen) {
    const char* cursor_name = "left_ptr";

    switch (resize_edge) {
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP:          cursor_name = "top_side"; break;
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:       cursor_name = "bottom_side"; break;
    case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:         cursor_name = "left_side"; break;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:     cursor_name = "top_left_corner"; break;
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:  cursor_name = "bottom_left_corner"; break;
    case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:        cursor_name = "right_side"; break;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:    cursor_name = "top_right_corner"; break;
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT: cursor_name = "bottom_right_corner"; break;
    default: break;
    }

    struct wl_cursor *cursor = wl_cursor_theme_get_cursor(os_lnx_gfx_state->cursor_theme, cursor_name);
    set_cursor(cursor);

    os_lnx_gfx_state->force_border_cursor = 1;
  } else {
    os_lnx_gfx_state->force_border_cursor = 0;
  }
}


internal void
pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
                      uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
  OS_LNX_Window *w = os_lnx_gfx_state->focused_window;

  uint32_t delta_time = time - w->last_click_time;

  if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
    if (delta_time < 200 && w->last_click_x == w->mouse_x && w->last_click_y == w->mouse_y) {
      if (w->is_maximized)
        xdg_toplevel_unset_maximized(w->xdg_toplevel);
      else
        xdg_toplevel_set_maximized(w->xdg_toplevel);
    } else {
      enum xdg_toplevel_resize_edge resize_edge = get_resize_edge(w);

      if (resize_edge != XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
        xdg_toplevel_resize(w->xdg_toplevel, os_lnx_gfx_state->seat, serial,
                            resize_edge);
      } else if (w->mouse_y < w->title_bar_thickness) {
        B32 is_over_title_bar_client_area = 0;

        // skip client area (buttons and stuff in the title bar)
        for (OS_LNX_TitleBarClientArea *area = w->first_title_bar_client_area;
             area != NULL; area = area->next) {
          Rng2F32 rect = area->rect;
          if (rect.x0 <= w->mouse_x && w->mouse_x < rect.x1 &&
              rect.y0 <= w->mouse_y && w->mouse_y < rect.y1) {
            is_over_title_bar_client_area = 1;
            break;
          }
        }

        if (!is_over_title_bar_client_area)
          xdg_toplevel_move(w->xdg_toplevel, os_lnx_gfx_state->seat, serial);
      }
    }
  } else if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED && w->mouse_y < w->title_bar_thickness) {
    xdg_toplevel_show_window_menu(w->xdg_toplevel, os_lnx_gfx_state->seat, serial, w->mouse_x / w->scale, w->mouse_y / w->scale);
  }

  if (button == BTN_LEFT) {
    w->last_click_time = time;
    w->last_click_x = w->mouse_x;
    w->last_click_y = w->mouse_y;
  }

  OS_Key key = OS_Key_Null;
  if(button == BTN_LEFT || button == 0x110)  key = OS_Key_LeftMouseButton; // BTN_LEFT constant if included
  else if(button == BTN_MIDDLE || button == 0x112) key = OS_Key_MiddleMouseButton;
  else if(button == BTN_RIGHT || button == 0x111)  key = OS_Key_RightMouseButton;

  if(key != OS_Key_Null) {
    OS_Event *event = os_lnx_push_event((state == WL_POINTER_BUTTON_STATE_PRESSED) ? OS_EventKind_Press : OS_EventKind_Release, w);

    event->key = key;
    event->pos = v2f32(w->mouse_x, w->mouse_y);
  }
}

internal void
pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
                    uint32_t time, uint32_t axis, wl_fixed_t value) {
  OS_LNX_Window *w = os_lnx_gfx_state->focused_window;
  OS_Event *event = os_lnx_push_event(OS_EventKind_Scroll, w);
  event->window.u64[0] = (U64)w;

  double v = wl_fixed_to_double(value) / 10;

  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
    event->delta = v2f32(0, v);
  }
  event->pos = v2f32(w->mouse_x, w->mouse_y);
}

internal void pointer_handle_frame(void *data, struct wl_pointer *pointer) {}

internal void pointer_handle_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {}

internal void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis_source) {}

internal void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {}

internal void pointer_handle_axis_value120(void *data,
 struct wl_pointer *wl_pointer,
 uint32_t axis,
 int32_t value120) {}

internal void pointer_handle_axis_relative_direction(void *data,
                                struct wl_pointer *wl_pointer,
                                uint32_t axis,
                                uint32_t direction) {}

internal const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
    .axis_value120 = pointer_handle_axis_value120,
    .axis_relative_direction = pointer_handle_axis_relative_direction
};

internal void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                                   uint32_t format, int fd, uint32_t size)
{
  char* map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

  os_lnx_gfx_state->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  os_lnx_gfx_state->xkb_keymap = xkb_keymap_new_from_string(os_lnx_gfx_state->xkb_ctx,
                                                 map_str,
                                                 XKB_KEYMAP_FORMAT_TEXT_V1,
                                                 XKB_KEYMAP_COMPILE_NO_FLAGS);
  os_lnx_gfx_state->xkb_state = xkb_state_new(os_lnx_gfx_state->xkb_keymap);

  munmap(map_str, size);
  close(fd);
}

internal void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
                                  uint32_t serial, struct wl_surface *surface,
                                  struct wl_array *keys)
{
}

internal void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                                  uint32_t serial, struct wl_surface *surface)
{
}

internal void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                                uint32_t serial, uint32_t time,
                                uint32_t key, uint32_t state)
{

  OS_EventKind event_kind = state == WL_KEYBOARD_KEY_STATE_PRESSED ? OS_EventKind_Press : OS_EventKind_Release;
  OS_Event *event = os_lnx_push_event(event_kind, os_lnx_gfx_state->focused_window);
  OS_Key os_key = OS_Key_Null;

  switch(key) {
  case KEY_A: os_key = OS_Key_A; break;
  case KEY_B: os_key = OS_Key_B; break;
  case KEY_C: os_key = OS_Key_C; break;
  case KEY_D: os_key = OS_Key_D; break;
  case KEY_E: os_key = OS_Key_E; break;
  case KEY_F: os_key = OS_Key_F; break;
  case KEY_G: os_key = OS_Key_G; break;
  case KEY_H: os_key = OS_Key_H; break;
  case KEY_I: os_key = OS_Key_I; break;
  case KEY_J: os_key = OS_Key_J; break;
  case KEY_K: os_key = OS_Key_K; break;
  case KEY_L: os_key = OS_Key_L; break;
  case KEY_M: os_key = OS_Key_M; break;
  case KEY_N: os_key = OS_Key_N; break;
  case KEY_O: os_key = OS_Key_O; break;
  case KEY_P: os_key = OS_Key_P; break;
  case KEY_Q: os_key = OS_Key_Q; break;
  case KEY_R: os_key = OS_Key_R; break;
  case KEY_S: os_key = OS_Key_S; break;
  case KEY_T: os_key = OS_Key_T; break;
  case KEY_U: os_key = OS_Key_U; break;
  case KEY_V: os_key = OS_Key_V; break;
  case KEY_W: os_key = OS_Key_W; break;
  case KEY_X: os_key = OS_Key_X; break;
  case KEY_Y: os_key = OS_Key_Y; break;
  case KEY_Z: os_key = OS_Key_Z; break;
  case KEY_0: os_key = OS_Key_0; break;
  case KEY_1: os_key = OS_Key_1; break;
  case KEY_2: os_key = OS_Key_2; break;
  case KEY_3: os_key = OS_Key_3; break;
  case KEY_4: os_key = OS_Key_4; break;
  case KEY_5: os_key = OS_Key_5; break;
  case KEY_6: os_key = OS_Key_6; break;
  case KEY_7: os_key = OS_Key_7; break;
  case KEY_8: os_key = OS_Key_8; break;
  case KEY_9: os_key = OS_Key_9; break;
  case KEY_FN_F1: os_key = OS_Key_F1; break;
  case KEY_FN_F2: os_key = OS_Key_F2; break;
  case KEY_FN_F3: os_key = OS_Key_F3; break;
  case KEY_FN_F4: os_key = OS_Key_F4; break;
  case KEY_FN_F5: os_key = OS_Key_F5; break;
  case KEY_FN_F6: os_key = OS_Key_F6; break;
  case KEY_FN_F7: os_key = OS_Key_F7; break;
  case KEY_FN_F8: os_key = OS_Key_F8; break;
  case KEY_FN_F9: os_key = OS_Key_F9; break;
  case KEY_FN_F10: os_key = OS_Key_F10; break;
  case KEY_FN_F11: os_key = OS_Key_F11; break;
  case KEY_FN_F12: os_key = OS_Key_F12; break;
  case KEY_UP: os_key = OS_Key_Up; break;
  case KEY_DOWN: os_key = OS_Key_Down; break;
  case KEY_LEFT: os_key = OS_Key_Left; break;
  case KEY_RIGHT: os_key = OS_Key_Right; break;
  case KEY_PAGEUP: os_key = OS_Key_PageUp; break;
  case KEY_END: os_key = OS_Key_End; break;
  case KEY_HOME: os_key = OS_Key_Home; break;
  case KEY_PAGEDOWN: os_key = OS_Key_PageDown; break;
  case KEY_SPACE: os_key = OS_Key_Space; break;
  case KEY_ENTER: os_key = OS_Key_Return; break;
  case KEY_BACKSPACE: os_key = OS_Key_Backspace; break;
  case KEY_TAB: os_key = OS_Key_Tab; break;
  case KEY_ESC: os_key = OS_Key_Esc; break;
  default: break;
  }

  event->key = os_key;

  if (os_lnx_gfx_state->xkb_state && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(os_lnx_gfx_state->xkb_state, key + 8);

    char buf[64] = {0};
    int n = xkb_keysym_to_utf8(keysym, buf, sizeof(buf));

    for (int i = 0; i < n; ) {
      UnicodeDecode decode = utf8_decode((U8*)buf + i, n - i);
      if (decode.codepoint != 0 && (decode.codepoint >= 32 || decode.codepoint == '\t')) {
        OS_Event *text_event = os_lnx_push_event(OS_EventKind_Text, os_lnx_gfx_state->focused_window);
        text_event->character = decode.codepoint;
      }

      if (decode.inc == 0)
        break;

      i += decode.inc;
    }
  }
}

internal void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
                                      uint32_t serial, uint32_t mods_depressed,
                                      uint32_t mods_latched, uint32_t mods_locked,
                                      uint32_t group)
{
  xkb_state_update_mask(os_lnx_gfx_state->xkb_state,
                        mods_depressed, mods_latched,
                        mods_locked, 0, 0, group);
}

internal void keyboard_handle_repeat_info(void *data,
                        struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {}

internal const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};



internal void
seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
  if(caps & WL_SEAT_CAPABILITY_POINTER)
  {
    if(!os_lnx_gfx_state->pointer)
    {
      os_lnx_gfx_state->pointer = wl_seat_get_pointer(os_lnx_gfx_state->seat);
      wl_pointer_add_listener(os_lnx_gfx_state->pointer, &pointer_listener, NULL);
    }
  }
  else if(os_lnx_gfx_state->pointer)
  {
    wl_pointer_destroy(os_lnx_gfx_state->pointer);
    os_lnx_gfx_state->pointer = NULL;
  }

  if (caps & WL_SEAT_CAPABILITY_KEYBOARD)
  {
    if (!os_lnx_gfx_state->keyboard)
    {
      os_lnx_gfx_state->keyboard = wl_seat_get_keyboard(seat);
      wl_keyboard_add_listener(os_lnx_gfx_state->keyboard, &keyboard_listener, NULL);
    }
  }
  else if (os_lnx_gfx_state->keyboard)
  {
    wl_keyboard_destroy(os_lnx_gfx_state->keyboard);
    os_lnx_gfx_state->keyboard = NULL;
  }
}

internal void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {}

internal const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

internal void fractional_preferred_scale(void *data, struct wp_fractional_scale_v1 *scale, uint32_t newscale) {
  OS_LNX_Window *w = (OS_LNX_Window *)data;

  float s = (float)newscale / 120.0f;

  if (w->scale != s) {
    if (os_lnx_gfx_state->cursor_theme)
      wl_cursor_theme_destroy(os_lnx_gfx_state->cursor_theme);

    os_lnx_gfx_state->cursor_theme = wl_cursor_theme_load("default", cursor_theme_size * s, os_lnx_gfx_state->shm);

    struct wl_cursor* cursor = wl_cursor_theme_get_cursor(os_lnx_gfx_state->cursor_theme, "left_ptr");

    wp_viewport_set_destination(os_lnx_gfx_state->cursor_viewport, cursor_theme_size, cursor_theme_size);

    set_cursor(cursor);
  }

  w->scale = s;
}

internal const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_preferred_scale,
};

internal void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface, uint32_t version)
{
  if(strcmp(interface, wl_compositor_interface.name) == 0) {
    os_lnx_gfx_state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
  }
  else if (strcmp(interface, wl_shm_interface.name) == 0) {
    os_lnx_gfx_state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  }
  else if(strcmp(interface, xdg_wm_base_interface.name) == 0) {
    os_lnx_gfx_state->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
  }
  else if(strcmp(interface, wl_seat_interface.name) == 0) {
    os_lnx_gfx_state->seat = wl_registry_bind(registry, name, &wl_seat_interface, 8);
    wl_seat_add_listener(os_lnx_gfx_state->seat, &seat_listener, NULL);
  } else if(strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
    os_lnx_gfx_state->fractional_scale_manager =
        wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
  } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
    os_lnx_gfx_state->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
  }
}

internal void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

internal struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

internal void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height, struct wl_array *states)
{
  OS_LNX_Window *w = (OS_LNX_Window *)data;

  w->is_maximized = 0;
  w->is_fullscreen = 0;

  uint32_t *state;
  wl_array_for_each(state, states) {
    switch (*state) {
    case XDG_TOPLEVEL_STATE_MAXIMIZED:
      w->is_maximized = 1;
      break;
    case XDG_TOPLEVEL_STATE_FULLSCREEN:
      w->is_fullscreen = 1;
      break;
    default:
      break;
    }
  }

  if (width > 0)
    w->width = width;

  if (height > 0)
    w->height = height;

  int fb_width = w->width * w->scale;
  int fb_height = w->height * w->scale;

  //wp_viewport_set_source(w->viewport, 0, 0, fb_width, fb_height);
  wp_viewport_set_destination(w->viewport, w->width, w->height);

  wl_egl_window_resize(w->egl_window, fb_width, fb_height, 0, 0);
  glViewport(0, 0, fb_width, fb_height);
  wl_surface_commit(w->surface);
}

internal void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
  OS_LNX_Window *w = (OS_LNX_Window *)data;

  OS_Event *e = os_lnx_push_event(OS_EventKind_WindowClose, w);
  e->window.u64[0] = (U64)w;
}

internal const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure,
    toplevel_close
};

internal void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    xdg_surface_ack_configure(surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure
};

internal void wm_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                    uint32_t serial) {
  xdg_wm_base_pong(xdg_wm_base, serial);
}

internal const struct xdg_wm_base_listener wm_base_listener = {
    wm_ping
};

internal void
os_gfx_init(void)
{
  //- rjf: initialize basics
  Arena *arena = arena_alloc();
  os_lnx_gfx_state = push_array(arena, OS_LNX_GfxState, 1);
  os_lnx_gfx_state->arena = arena;

  os_lnx_gfx_state->display = wl_display_connect(NULL);
  os_lnx_gfx_state->registry = wl_display_get_registry(os_lnx_gfx_state->display);
  wl_registry_add_listener(os_lnx_gfx_state->registry, &registry_listener, os_lnx_gfx_state);
  wl_display_roundtrip(os_lnx_gfx_state->display);

  xdg_wm_base_add_listener(os_lnx_gfx_state->wm_base, &wm_base_listener, NULL);

  os_lnx_gfx_state->cursor_surface = wl_compositor_create_surface(os_lnx_gfx_state->compositor);
  os_lnx_gfx_state->cursor_viewport = wp_viewporter_get_viewport(os_lnx_gfx_state->viewporter, os_lnx_gfx_state->cursor_surface);
  os_lnx_gfx_state->cursor_theme = wl_cursor_theme_load(NULL, 24, os_lnx_gfx_state->shm);

  //- rjf: fill out gfx info
  os_lnx_gfx_state->gfx_info.double_click_time = 0.5f;
  os_lnx_gfx_state->gfx_info.caret_blink_time = 0.5f;
  os_lnx_gfx_state->gfx_info.default_refresh_rate = 60.f;
}

////////////////////////////////
//~ rjf: @os_hooks Graphics System Info (Implemented Per-OS)

internal OS_GfxInfo *
os_get_gfx_info(void)
{
  return &os_lnx_gfx_state->gfx_info;
}

////////////////////////////////
//~ rjf: @os_hooks Clipboards (Implemented Per-OS)

internal void
os_set_clipboard_text(String8 string)
{
  
}

internal String8
os_get_clipboard_text(Arena *arena)
{
  String8 result = {0};
  return result;
}

////////////////////////////////
//~ rjf: @os_hooks Windows (Implemented Per-OS)

internal OS_Handle
os_window_open(Rng2F32 rect, OS_WindowFlags flags, String8 title)
{
  Vec2F32 resolution = dim_2f32(rect);
  
  //- rjf: allocate window
  OS_LNX_Window *w = os_lnx_gfx_state->free_window;
  if(w)
  {
    SLLStackPop(os_lnx_gfx_state->free_window);
  }
  else
  {
    w = push_array_no_zero(os_lnx_gfx_state->arena, OS_LNX_Window, 1);
  }
  MemoryZeroStruct(w);
  DLLPushBack(os_lnx_gfx_state->first_window, os_lnx_gfx_state->last_window, w);

  w->title_bar_arena = arena_alloc();

  w->width = 1280;
  w->height = 720;
  w->scale = 1.0f;

  w->surface = wl_compositor_create_surface(os_lnx_gfx_state->compositor);
  wl_surface_set_user_data(w->surface, w);
  w->viewport = wp_viewporter_get_viewport(os_lnx_gfx_state->viewporter, w->surface);

  if (os_lnx_gfx_state->fractional_scale_manager) {
    w->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
        os_lnx_gfx_state->fractional_scale_manager,
        w->surface);
    wp_fractional_scale_v1_add_listener(w->fractional_scale,
                                        &fractional_scale_listener,
                                        w);
  }

  w->xdg_surface = xdg_wm_base_get_xdg_surface(os_lnx_gfx_state->wm_base, w->surface);
  xdg_surface_add_listener(w->xdg_surface, &xdg_surface_listener, NULL);
  w->xdg_toplevel = xdg_surface_get_toplevel(w->xdg_surface);

  //- rjf: attach name
  Temp scratch = scratch_begin(0, 0);
  String8 title_copy = push_str8_copy(scratch.arena, title);
  xdg_toplevel_set_title(w->xdg_toplevel, (const char*)title_copy.str);
  scratch_end(scratch);

  xdg_toplevel_add_listener(w->xdg_toplevel, &toplevel_listener, w);

  w->egl_window = wl_egl_window_create(w->surface, w->width, w->height);

  wl_surface_commit(w->surface);
  wl_display_roundtrip(os_lnx_gfx_state->display);

  //- rjf: convert to handle & return
  OS_Handle handle = {(U64)w};
  return handle;
}

internal void
os_window_close(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];

  xdg_toplevel_destroy(w->xdg_toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);

  wl_display_roundtrip(os_lnx_gfx_state->display);
}

internal void
os_window_set_title(OS_Handle handle, String8 title)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  Temp scratch = scratch_begin(0, 0);
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
  String8 title_copy = push_str8_copy(scratch.arena, title);
  xdg_toplevel_set_title(w->xdg_toplevel, (const char*)title_copy.str);
  scratch_end(scratch);
}

internal void
os_window_first_paint(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
  wl_surface_commit(w->surface);
}

internal void
os_window_focus(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
}

internal B32
os_window_is_focused(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return 0;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
  /* Window focused_window = 0;
  int revert_to = 0;
  XGetInputFocus(os_lnx_gfx_state->display, &focused_window, &revert_to);
  B32 result = (w->window == focused_window);
  return result; */
  return 1;
}

internal B32
os_window_is_fullscreen(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return 0;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
  return w->is_fullscreen;
}

internal void
os_window_set_fullscreen(OS_Handle handle, B32 fullscreen)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];

  if (fullscreen)
    xdg_toplevel_set_fullscreen(w->xdg_toplevel, NULL);
  else
    xdg_toplevel_unset_fullscreen(w->xdg_toplevel);
}

internal B32
os_window_is_maximized(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return 0;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
  return w->is_maximized;
}

internal void
os_window_set_maximized(OS_Handle handle, B32 maximized)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];

  if (maximized) {
    xdg_toplevel_set_maximized(w->xdg_toplevel);
  } else {
    xdg_toplevel_unset_maximized(w->xdg_toplevel);
  }

  wl_display_roundtrip(os_lnx_gfx_state->display);
}

internal B32
os_window_is_minimized(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return 0;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
  return 0;
}

internal void
os_window_set_minimized(OS_Handle handle, B32 minimized)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
  if (minimized)
    xdg_toplevel_set_minimized(w->xdg_toplevel);
}

internal void
os_window_bring_to_front(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  // TODO(rjf)
}

internal void
os_window_set_monitor(OS_Handle handle, OS_Handle monitor)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  // TODO(rjf)
}

internal void
os_window_clear_custom_border_data(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *w = (OS_LNX_Window*)handle.u64[0];
  arena_clear(w->title_bar_arena);
  w->first_title_bar_client_area = NULL;
  w->last_title_bar_client_area = NULL;
  w->title_bar_thickness = 0;
  w->edge_thickness = 0;
}

internal void
os_window_push_custom_title_bar(OS_Handle handle, F32 thickness)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *w = (OS_LNX_Window*)handle.u64[0];
  w->title_bar_thickness = thickness;
}

internal void
os_window_push_custom_edges(OS_Handle handle, F32 thickness)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *w = (OS_LNX_Window*)handle.u64[0];
  w->edge_thickness = thickness;
}

internal void
os_window_push_custom_title_bar_client_area(OS_Handle handle, Rng2F32 rect)
{
  if(os_handle_match(handle, os_handle_zero())) {return;}
  OS_LNX_Window *window = (OS_LNX_Window*)handle.u64[0];

  OS_LNX_TitleBarClientArea *area = push_array(window->title_bar_arena, OS_LNX_TitleBarClientArea, 1);
  if (area) {
    area->rect = rect;
    SLLQueuePush(window->first_title_bar_client_area, window->last_title_bar_client_area, area);
  }
}

internal Rng2F32
os_rect_from_window(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return r2f32p(0, 0, 0, 0);}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
  Rng2F32 result = r2f32p(0, 0, (F32)w->width * w->scale, (F32)w->height * w->scale);
  return result;
}

internal Rng2F32
os_client_rect_from_window(OS_Handle handle)
{
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];

  Rng2F32 result = r2f32p(0, 0, (F32)w->width * w->scale, (F32)w->height * w->scale);
  return result;
}

internal F32
os_dpi_from_window(OS_Handle handle)
{
  // TODO(rjf)
  return 96.f;
}

////////////////////////////////
//~ rjf: @os_hooks External Windows (Implemented Per-OS)

internal OS_Handle
os_focused_external_window(void)
{
  OS_Handle result = {0};
  // TODO(rjf)
  return result;
}

internal void
os_focus_external_window(OS_Handle handle)
{
  // TODO(rjf)
}

////////////////////////////////
//~ rjf: @os_hooks Monitors (Implemented Per-OS)

internal OS_HandleArray
os_push_monitors_array(Arena *arena)
{
  OS_HandleArray result = {0};
  // TODO(rjf)
  return result;
}

internal OS_Handle
os_primary_monitor(void)
{
  OS_Handle result = {0};
  // TODO(rjf)
  return result;
}

internal OS_Handle
os_monitor_from_window(OS_Handle window)
{
  OS_Handle result = {0};
  // TODO(rjf)
  return result;
}

internal String8
os_name_from_monitor(Arena *arena, OS_Handle monitor)
{
  // TODO(rjf)
  return str8_zero();
}

internal Vec2F32
os_dim_from_monitor(OS_Handle monitor)
{
  // TODO(rjf)
  return v2f32(0, 0);
}

internal F32
os_dpi_from_monitor(OS_Handle monitor)
{
  // TODO(rjf)
  return 96.f;
}

////////////////////////////////
//~ rjf: @os_hooks Events (Implemented Per-OS)

internal void
os_send_wakeup_event(void)
{
  // TODO(rjf)
}

internal OS_EventList
os_get_events(Arena *arena, B32 wait)
{
  os_lnx_event_arena = arena;
  MemoryZeroStruct(&os_lnx_event_list);

  if (wait) {
    wl_display_dispatch(os_lnx_gfx_state->display);
  } else {
    wl_display_dispatch_pending(os_lnx_gfx_state->display);
  }

  return os_lnx_event_list;
}

internal OS_Modifiers
os_get_modifiers(void)
{
  int shift = xkb_state_mod_name_is_active(os_lnx_gfx_state->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE);
  int ctrl  = xkb_state_mod_name_is_active(os_lnx_gfx_state->xkb_state, XKB_MOD_NAME_CTRL,  XKB_STATE_MODS_EFFECTIVE);
  int alt   = xkb_state_mod_name_is_active(os_lnx_gfx_state->xkb_state, XKB_MOD_NAME_ALT,   XKB_STATE_MODS_EFFECTIVE);
  // int logo  = xkb_state_mod_name_is_active(os_lnx_gfx_state->xkb_state, XKB_MOD_NAME_LOGO,  XKB_STATE_MODS_EFFECTIVE);

  OS_Modifiers modifiers = 0;

  if(shift) { modifiers |= OS_Modifier_Shift; }
  if(ctrl)  { modifiers |= OS_Modifier_Ctrl; }
  if(alt)   { modifiers |= OS_Modifier_Alt; }

  return modifiers;
}

internal B32
os_key_is_down(OS_Key key)
{
  // TODO(rjf)
  return 0;
}

internal Vec2F32
os_mouse_from_window(OS_Handle handle)
{
  if(os_handle_match(handle, os_handle_zero())) {return v2f32(0, 0);}
  OS_LNX_Window *w = (OS_LNX_Window *)handle.u64[0];
  Vec2F32 result = {0};
  result.x = w->mouse_x;
  result.y = w->mouse_y;
  return result;
}

////////////////////////////////
//~ rjf: @os_hooks Cursors (Implemented Per-OS)

internal struct wl_cursor *get_cursor_for_type(OS_Cursor type) {
  const char *name = "left_ptr";

  switch (type)
  {
  case OS_Cursor_Pointer:           name = "left_ptr"; break;
  case OS_Cursor_IBar:              name = "text"; break;
  case OS_Cursor_LeftRight:         name = "ew-resize"; break;
  case OS_Cursor_UpDown:            name = "ns-resize"; break;
  case OS_Cursor_DownRight:         name = "nwse-resize"; break;
  case OS_Cursor_UpRight:           name = "nesw-resize"; break;
  case OS_Cursor_UpDownLeftRight:   name = "all-scroll"; break;
  case OS_Cursor_HandPoint:         name = "hand2"; break;
  case OS_Cursor_Disabled:          name = "not-allowed"; break;
  default:                          name = "left_ptr"; break;
  }

  return wl_cursor_theme_get_cursor(os_lnx_gfx_state->cursor_theme, name);
}

internal void
os_set_cursor(OS_Cursor cursor)
{
  if (os_lnx_gfx_state->force_border_cursor)
    return;

  os_lnx_gfx_state->last_set_cursor = cursor;

  struct wl_cursor *wl_cursor = get_cursor_for_type(cursor);
  set_cursor(wl_cursor);
}

////////////////////////////////
//~ rjf: @os_hooks Native User-Facing Graphical Messages (Implemented Per-OS)

internal void
os_graphical_message(B32 error, String8 title, String8 message)
{
  if(error)
  {
    fprintf(stderr, "[X] ");
  }
  fprintf(stderr, "%.*s\n", str8_varg(title));
  fprintf(stderr, "%.*s\n\n", str8_varg(message));
}

struct file_chooser_data {
  GMainLoop *loop;
  char *selected_file;
};

internal void
on_file_chooser_response(GDBusConnection *connection,
            const gchar *sender_name,
            const gchar *object_path,
            const gchar *interface_name,
            const gchar *signal_name,
            GVariant *parameters,
            gpointer user_data) {
  struct file_chooser_data *data = user_data;
  guint32 response;
  GVariant *results;

  g_variant_get(parameters, "(u@a{sv})", &response, &results);

  if (response == 0) {
    GVariant *uris_variant = g_variant_lookup_value(results, "uris", G_VARIANT_TYPE_STRING_ARRAY);
    if (uris_variant) {
      gsize n_uris;
      const gchar **uris = g_variant_get_strv(uris_variant, &n_uris);

      if (n_uris > 0) {
        GFile *file = g_file_new_for_uri(uris[0]);
        char *path = g_file_get_path(file);
        data->selected_file = g_strdup(path);
        g_free(path);
        g_object_unref(file);
      }

      g_free(uris);
      g_variant_unref(uris_variant);
    }
  }

  g_variant_unref(results);

  if (g_main_loop_is_running(data->loop))
    g_main_loop_quit(data->loop);
}

struct glib_wayland_source {
  GSource source;
  GPollFD pollfd;
  struct wl_display *display;
};

internal gboolean wayland_source_prepare(GSource *source, gint *timeout_) {
  *timeout_ = -1;
  return FALSE;
}

internal gboolean wayland_source_check(GSource *source) {
  struct glib_wayland_source *ws = (struct glib_wayland_source *)source;
  return ws->pollfd.revents != 0;
}

internal gboolean wayland_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
  struct glib_wayland_source *ws = (struct glib_wayland_source *)source;

  wl_display_dispatch(ws->display);
  wl_display_flush(ws->display);

  return TRUE;
}

internal GSourceFuncs wayland_source_funcs = {
    .prepare = wayland_source_prepare,
    .check = wayland_source_check,
    .dispatch = wayland_source_dispatch,
};

internal String8
os_graphical_pick_file(Arena *arena, String8 initial_path)
{
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
  struct file_chooser_data data = {0};

  gchar *token = g_strdup_printf("filechooser%u", g_random_int());
  gchar *handle = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s",
                                  g_dbus_connection_get_unique_name(bus) + 1,
                                  token);

 for (gchar *p = handle; *p; p++)
    if (*p == '.') *p = '_';

  data.loop = g_main_loop_new(NULL, FALSE);

  // Dirty hack to keep responding to wayland events while the glib event loop is blocking the function.
  // This avoids "The app is not responding" popup when the file chooser is open

  struct glib_wayland_source *ws = (struct glib_wayland_source*)g_source_new(&wayland_source_funcs, sizeof(struct glib_wayland_source));
  ws->display = os_lnx_gfx_state->display;
  ws->pollfd.fd = wl_display_get_fd(os_lnx_gfx_state->display);
  ws->pollfd.events = G_IO_IN;

  g_source_add_poll((GSource*)ws, &ws->pollfd);
  g_source_attach((GSource*)ws, NULL);

  guint subscription_id = g_dbus_connection_signal_subscribe(
      bus,
      "org.freedesktop.portal.Desktop",
      "org.freedesktop.portal.Request",
      "Response",
      handle,
      NULL,
      G_DBUS_SIGNAL_FLAGS_NONE,
      on_file_chooser_response,
      &data,
      NULL
  );

  GVariantBuilder options_builder = G_VARIANT_BUILDER_INIT(G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options_builder, "{sv}", "handle_token", g_variant_new_string(token));
  g_variant_builder_add(&options_builder, "{sv}", "multiple", g_variant_new_boolean(FALSE));

  if (initial_path.size > 0)
    g_variant_builder_add(&options_builder, "{sv}", "current_folder", g_variant_new_bytestring((const char*)initial_path.str));

  GVariant *params = g_variant_new("(ssa{sv})", "", "Choose a file", &options_builder);
  GVariant *result = g_dbus_connection_call_sync(
      bus,
      "org.freedesktop.portal.Desktop",
      "/org/freedesktop/portal/desktop",
      "org.freedesktop.portal.FileChooser",
      "OpenFile",
      params,
      NULL,
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL,
      NULL
  );

  g_variant_unref(result);

  g_main_loop_run(data.loop);

  g_dbus_connection_signal_unsubscribe(bus, subscription_id);
  g_source_destroy((GSource *)ws);
  g_main_loop_unref(data.loop);
  g_object_unref(bus);
  g_free(token);
  g_free(handle);

  if (!data.selected_file)
    return str8_zero();

  size_t len = strlen(data.selected_file);

  U8 *buf = push_array(arena, U8, len);
  memcpy(buf, data.selected_file, len);

  String8 s = {0};
  s.str = buf;
  s.size = len;

  return s;
}

////////////////////////////////
//~ rjf: @os_hooks Shell Operations

internal void
os_show_in_filesystem_ui(String8 path)
{
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);

  char uri[4096] = {0};
  snprintf(uri, sizeof(uri) - 1, "%s%s", "file://", path.str);

  GVariantBuilder uris_builder = G_VARIANT_BUILDER_INIT(G_VARIANT_TYPE_STRING_ARRAY);

  g_variant_builder_add(&uris_builder, "s", uri);

  GVariant *params = g_variant_new("(ass)", &uris_builder, "");

  g_dbus_connection_call(
      bus,
      "org.freedesktop.FileManager1",
      "/org/freedesktop/FileManager1",
      "org.freedesktop.FileManager1",
      "ShowItems",
      params,
      NULL,
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL,
      NULL,
      NULL
  );
}

internal void
os_open_in_browser(String8 url)
{
  GError *error = NULL;

  g_app_info_launch_default_for_uri((const char*)url.str, NULL, &error);

  if (error)
    g_error_free(error);
}
