/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Launcher modelled on the PortMaster "Moonlight New" launcher: a bottom menu
 * with Settings / Play / Connect, horizontal carousels for settings and apps,
 * a number pad for the PC address, an info panel for pairing and errors,
 * a clock, animated pixel-art themes and UI sounds. Everything is drawn with
 * SDL2 primitives and a built-in 8x8 font, so no extra libraries or assets.
 */

#ifdef HAVE_SDL

#include "ui.h"
#include "platform.h"
#include "config.h"

#include "../third_party/font8x8/font8x8_basic.h"

#include <client.h>
#include <discover.h>
#include <errors.h>

#include <SDL.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MAX_APPS 64
#define MAX_APP_NAME 128
#define MAX_LIST 32
#define MAX_TEXT_LINES 16

#define REPEAT_DELAY 400
#define REPEAT_INTERVAL 120
#define AXIS_PRESS 20000
#define AXIS_RELEASE 12000
#define FADE_TIME 0.35f
#define SPLASH_TIME 2.6f

enum menu { MENU_MAIN, MENU_SETTINGS, MENU_PLAY, MENU_CONNECT, MENU_COUNT };
enum main_item { MAIN_SETTINGS, MAIN_PLAY, MAIN_CONNECT, MAIN_COUNT };
enum connect_item { CONNECT_PAIR, CONNECT_ADDRESS, CONNECT_RELOAD, CONNECT_COUNT };
enum setting { SET_RESOLUTION, SET_BITRATE, SET_FPS, SET_CODEC, SET_REMOTE, SET_SFX, SET_THEME, SET_COUNT };
enum theme { THEME_MOONLIGHT, THEME_SUNSHINE, THEME_KEPLER, THEME_VOID, THEME_COUNT };
enum action { ACT_NONE, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_A, ACT_B, ACT_X, ACT_Y, ACT_START, ACT_SELECT, ACT_QUIT };
enum sound { SND_SIDE, SND_UP, SND_DOWN, SND_ENTER, SND_BACK, SND_KEY, SND_ERROR };
enum pc_state { PC_UNKNOWN, PC_OFFLINE, PC_NOT_PAIRED, PC_PAIRED };
enum result { RESULT_NONE, RESULT_PLAY, RESULT_QUIT };

static const char* main_names[MAIN_COUNT] = {"Settings", "Play", "Connect"};
static const char* connect_names[CONNECT_COUNT] = {"Pair", "IP Address", "Reload Apps"};
static const char* setting_names[SET_COUNT] = {"Resolution", "Bitrate", "Framerate", "Codec", "Remote", "Sound FX", "Theme"};
static const char* menu_titles[MENU_COUNT] = {"", "Settings", "Play", "Connect"};

// Indexed by enum codecs
static const char* codec_names[] = {"Auto", "H.264", "HEVC", "AV1"};
static const char* codec_keys[] = {"auto", "h264", "h265", "av1"};
static const char* remote_names[] = {"Auto", "Yes", "No"};
static const char* remote_keys[] = {"auto", "yes", "no"};
static const int remote_values[] = {STREAM_CFG_AUTO, STREAM_CFG_REMOTE, STREAM_CFG_LOCAL};
static const char* sfx_names[] = {"On", "Off"};
static const char* sfx_keys[] = {"on", "off"};
static const char* theme_names[THEME_COUNT] = {"Moonlight", "Sunshine", "Kepler-62f", "???"};
static const char* theme_keys[THEME_COUNT] = {"moonlight", "sunshine", "kepler", "void"};

static const char* numpad_keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", ":", "Del", "Clear", "Done"};
#define NUMPAD_DEL 12
#define NUMPAD_CLEAR 13
#define NUMPAD_DONE 14

typedef struct {
  char name[MAX_APP_NAME];
  int id;
} UI_APP;

// Index 0 of each list is the "automatic" choice (native resolution, base bitrate)
typedef struct { int width, height; } RESOLUTION;
static RESOLUTION resolutions[MAX_LIST];
static int resolution_count;
static int bitrates[MAX_LIST];
static int bitrate_count;
static int framerates[MAX_LIST];
static int framerate_count;

static const RESOLUTION default_resolutions[] = {
  {480, 320}, {640, 480}, {854, 480}, {960, 544}, {720, 720}, {960, 720},
  {1280, 720}, {1280, 800}, {1600, 900}, {1920, 1080}, {1920, 1152}, {2560, 1440}, {3840, 2160},
};
static const int default_bitrates[] = {500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 6000, 7000, 8000, 9000, 10000, 12000, 15000, 20000, 30000, 40000, 50000};
static const int default_framerates[] = {20, 24, 30, 40, 45, 50, 60, 75, 90, 100, 120, 144};

// Background network job (connect / pair / list apps). Only one runs at a time
// and only the worker touches libgamestream while it runs.
typedef struct {
  bool pair;
  bool quiet;
  char address[MAX_ADDRESS_SIZE];
  unsigned short port;
  char pin[5];
  const char* key_dir;
  int debug_level;
  bool unsupported;

  enum pc_state state;
  SERVER_DATA server;
  UI_APP apps[MAX_APPS];
  int app_count;
  bool apps_loaded;
  char message[640];
} JOB;

static JOB job;
static SDL_Thread* worker;
static SDL_atomic_t job_done;
static bool reconnect_pending;

static SDL_Window* window;
static SDL_Renderer* renderer;
static SDL_Texture* font;
static SDL_AudioDeviceID audio;
static int screen_w, screen_h;
static int native_w, native_h;
static int s_small, s_normal, s_large;

static const struct _CONFIGURATION* base_config;
static char conf_path[4200];

// Launcher state; kept across ui_run() calls so a returning launcher looks the same
static char pc_address[MAX_ADDRESS_SIZE];
static unsigned short pc_port;
static enum pc_state pc_state;
static SERVER_DATA server;
static bool server_valid;
static UI_APP apps[MAX_APPS];
static int app_count;
static char last_app[MAX_APP_NAME];
static char session_app[MAX_APP_NAME];
static char session_address[MAX_ADDRESS_SIZE];
static int setting_index[SET_COUNT];
static bool settings_loaded;

static enum menu menu;
static int selection[MENU_COUNT];
static float glow[MAIN_COUNT];
static bool numpad_open;
static int numpad_selection;
static char numpad_text[MAX_ADDRESS_SIZE];

static char panel_title[64];
static char panel_body[640];

static float now;
static float splash_start;
static bool splash_active;
static float fade_start;
static bool fading_out;
static enum result result;

static enum action held_action;
static Uint32 next_repeat;
static int axis_x, axis_y;

static float shooting_star_time, shooting_star_next;
static float shooting_x, shooting_y;

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

typedef struct { Uint8 r, g, b, a; } COLOR;

static const COLOR WHITE = {255, 255, 255, 255};
static const COLOR GREY = {128, 128, 128, 255};
static const COLOR DIM = {170, 170, 185, 255};

static COLOR rgba(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
  COLOR c = {r, g, b, a};
  return c;
}

static COLOR mix(COLOR a, COLOR b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

static COLOR with_alpha(COLOR c, float alpha) {
  c.a = c.a * (alpha < 0 ? 0 : alpha > 1 ? 1 : alpha);
  return c;
}

static void set_color(COLOR c) {
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
}

static void fill_rect(int x, int y, int w, int h, COLOR c) {
  SDL_Rect rect = {x, y, w, h};
  set_color(c);
  SDL_RenderFillRect(renderer, &rect);
}

static void fill_circle(float cx, float cy, float radius, COLOR c) {
  SDL_Rect spans[512];
  int count = 0;
  int r = (int) radius;
  for (int dy = -r; dy <= r && count < (int) ARRAY_SIZE(spans); dy++) {
    int half = (int) sqrtf(radius * radius - dy * dy);
    spans[count++] = (SDL_Rect) {(int) cx - half, (int) cy + dy, half * 2 + 1, 1};
  }
  set_color(c);
  SDL_RenderFillRects(renderer, spans, count);
}

static void fill_round_rect(int x, int y, int w, int h, int radius, COLOR c) {
  SDL_Rect spans[1024];
  int count = 0;
  if (radius * 2 > h)
    radius = h / 2;
  for (int row = 0; row < h && count < (int) ARRAY_SIZE(spans); row++) {
    int inset = 0;
    int edge = row < radius ? radius - row : row >= h - radius ? row - (h - radius - 1) : 0;
    if (edge > 0)
      inset = radius - (int) sqrtf((float) (radius * radius - (edge - 0.5f) * (edge - 0.5f)));
    spans[count++] = (SDL_Rect) {x + inset, y + row, w - inset * 2, 1};
  }
  set_color(c);
  SDL_RenderFillRects(renderer, spans, count);
}

static void vertical_gradient(int x, int y, int w, int h, COLOR top, COLOR bottom) {
  for (int row = 0; row < h; row++)
    fill_rect(x, y + row, w, 1, mix(top, bottom, h > 1 ? (float) row / (h - 1) : 0));
}

static SDL_Texture* create_font_texture(void) {
  SDL_Surface* surface = SDL_CreateRGBSurface(0, 128, 64, 32, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
  if (surface == NULL)
    return NULL;

  Uint32* pixels = surface->pixels;
  int pitch = surface->pitch / 4;
  for (int glyph = 0; glyph < 128; glyph++) {
    int gx = (glyph % 16) * 8, gy = (glyph / 16) * 8;
    for (int row = 0; row < 8; row++) {
      unsigned char bits = (unsigned char) font8x8_basic[glyph][row];
      for (int col = 0; col < 8; col++)
        pixels[(gy + row) * pitch + gx + col] = (bits >> col) & 1 ? 0xFFFFFFFF : 0;
    }
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (texture != NULL)
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  return texture;
}

// UTF-8 aware: one glyph per code point, '?' for anything outside ASCII
static int next_glyph(const char** text) {
  unsigned char c = (unsigned char) **text;
  if (c == 0)
    return -1;
  (*text)++;
  if (c < 0x80)
    return c;
  while ((**text & 0xC0) == 0x80)
    (*text)++;
  return '?';
}

static int glyph_count(const char* text) {
  int count = 0;
  while (next_glyph(&text) >= 0)
    count++;
  return count;
}

static int text_width(const char* text, int scale) {
  return glyph_count(text) * 8 * scale;
}

static void draw_glyphs(const char* text, int x, int y, int scale, COLOR c) {
  SDL_SetTextureColorMod(font, c.r, c.g, c.b);
  SDL_SetTextureAlphaMod(font, c.a);
  int glyph;
  while ((glyph = next_glyph(&text)) >= 0) {
    SDL_Rect src = {(glyph % 16) * 8, (glyph / 16) * 8, 8, 8};
    SDL_Rect dst = {x, y, 8 * scale, 8 * scale};
    SDL_RenderCopy(renderer, font, &src, &dst);
    x += 8 * scale;
  }
}

static void draw_text(const char* text, int x, int y, int scale, COLOR c) {
  int shadow = scale > 1 ? scale / 2 : 1;
  draw_glyphs(text, x + shadow, y + shadow, scale, rgba(0, 0, 0, c.a * 0.6f));
  draw_glyphs(text, x, y, scale, c);
}

// Shrinks the scale and then truncates (into clipped) so text fits max_width
static const char* fit_text(const char* text, int* scale, int max_width, char clipped[256]) {
  while (*scale > 1 && text_width(text, *scale) > max_width)
    (*scale)--;

  int max_glyphs = max_width / (8 * *scale);
  if (glyph_count(text) > max_glyphs && max_glyphs > 2) {
    const char* p = text;
    for (int i = 0; i < max_glyphs - 2 && next_glyph(&p) >= 0; i++);
    snprintf(clipped, 256, "%.*s..", (int) (p - text), text);
    return clipped;
  }
  return text;
}

static void draw_text_fit(const char* text, int x, int y, int scale, COLOR c, int max_width) {
  char clipped[256];
  text = fit_text(text, &scale, max_width, clipped);
  draw_text(text, x, y, scale, c);
}

static void draw_text_centered(const char* text, int cx, int y, int scale, COLOR c, int max_width) {
  char clipped[256];
  text = fit_text(text, &scale, max_width, clipped);
  draw_text(text, cx - text_width(text, scale) / 2, y, scale, c);
}

// Greedy word wrap; returns the number of lines
static int wrap_text(const char* text, int max_glyphs, char lines[][128], int max_lines) {
  int count = 0;
  if (max_glyphs > 127)
    max_glyphs = 127;
  if (max_glyphs < 4)
    max_glyphs = 4;

  while (*text && count < max_lines) {
    const char* end = strchr(text, '\n');
    size_t paragraph = end ? (size_t) (end - text) : strlen(text);
    const char* p = text;
    do {
      size_t len = (size_t) (text + paragraph - p);
      if ((int) len > max_glyphs) {
        len = max_glyphs;
        while (len > 0 && p[len] != ' ')
          len--;
        if (len == 0)
          len = max_glyphs;
      }
      snprintf(lines[count++], 128, "%.*s", (int) len, p);
      p += len;
      while (*p == ' ')
        p++;
    } while (p < text + paragraph && count < max_lines);
    text += paragraph;
    if (*text == '\n')
      text++;
  }
  return count;
}

// ---------------------------------------------------------------------------
// Sound effects (synthesized, no assets)
// ---------------------------------------------------------------------------

static void play_sound(enum sound sound) {
  if (audio == 0 || setting_index[SET_SFX] != 0)
    return;

  static const struct { float freq[2]; float duration; } sounds[] = {
    [SND_SIDE] = {{740, 0}, 0.035f},
    [SND_UP] = {{880, 0}, 0.045f},
    [SND_DOWN] = {{587, 0}, 0.045f},
    [SND_ENTER] = {{660, 988}, 0.05f},
    [SND_BACK] = {{784, 523}, 0.05f},
    [SND_KEY] = {{1175, 0}, 0.025f},
    [SND_ERROR] = {{196, 147}, 0.08f},
  };

  static Sint16 buffer[22050 / 4];
  int rate = 22050, count = 0;
  for (int part = 0; part < 2 && sounds[sound].freq[part] > 0; part++) {
    int samples = sounds[sound].duration * rate;
    for (int i = 0; i < samples && count < (int) ARRAY_SIZE(buffer); i++) {
      float envelope = 1.0f - (float) i / samples;
      float wave = sinf(2 * (float) M_PI * sounds[sound].freq[part] * i / rate);
      buffer[count++] = (Sint16) (wave * envelope * envelope * 5000);
    }
  }
  SDL_ClearQueuedAudio(audio);
  SDL_QueueAudio(audio, buffer, count * sizeof(Sint16));
}

// ---------------------------------------------------------------------------
// Settings lists and launcher.conf
// ---------------------------------------------------------------------------

static int int_list_find_or_add(int* list, int* count, int value) {
  for (int i = 1; i < *count; i++) {
    if (list[i] == value)
      return i;
  }
  if (*count >= MAX_LIST)
    return 0;

  int pos = 1;
  while (pos < *count && list[pos] < value)
    pos++;
  memmove(&list[pos + 1], &list[pos], (*count - pos) * sizeof(int));
  list[pos] = value;
  (*count)++;
  return pos;
}

static int resolution_find_or_add(int width, int height) {
  for (int i = 1; i < resolution_count; i++) {
    if (resolutions[i].width == width && resolutions[i].height == height)
      return i;
  }
  if (resolution_count >= MAX_LIST || width <= 0 || height <= 0)
    return 0;

  int pos = 1;
  while (pos < resolution_count && resolutions[pos].width * resolutions[pos].height <= width * height)
    pos++;
  memmove(&resolutions[pos + 1], &resolutions[pos], (resolution_count - pos) * sizeof(RESOLUTION));
  resolutions[pos] = (RESOLUTION) {width, height};
  resolution_count++;
  return pos;
}

static int key_index(const char* value, const char** keys, int count) {
  for (int i = 0; i < count; i++) {
    if (strcasecmp(value, keys[i]) == 0)
      return i;
  }
  return -1;
}

static void settings_init(const struct _CONFIGURATION* base) {
  resolution_count = 1;
  for (size_t i = 0; i < ARRAY_SIZE(default_resolutions); i++)
    resolutions[resolution_count++] = default_resolutions[i];
  bitrate_count = 1;
  for (size_t i = 0; i < ARRAY_SIZE(default_bitrates); i++)
    bitrates[bitrate_count++] = default_bitrates[i];
  framerate_count = 1;
  for (size_t i = 0; i < ARRAY_SIZE(default_framerates); i++)
    framerates[framerate_count++] = default_framerates[i];

  // Defaults come from the command line / moonlight.conf
  setting_index[SET_RESOLUTION] = resolution_find_or_add(base->stream.width, base->stream.height);
  setting_index[SET_BITRATE] = 0;
  setting_index[SET_FPS] = int_list_find_or_add(framerates, &framerate_count, base->stream.fps);
  setting_index[SET_CODEC] = base->codec < (int) ARRAY_SIZE(codec_names) ? (int) base->codec : 0;
  setting_index[SET_REMOTE] = 0;
  for (size_t i = 0; i < ARRAY_SIZE(remote_values); i++) {
    if (remote_values[i] == base->stream.streamingRemotely)
      setting_index[SET_REMOTE] = i;
  }
  setting_index[SET_SFX] = 0;
  setting_index[SET_THEME] = THEME_MOONLIGHT;

  pc_address[0] = 0;
  if (base->address != NULL)
    snprintf(pc_address, sizeof(pc_address), "%s", base->address);
  pc_port = base->port;
}

static void launcher_load(void) {
  FILE* file = fopen(conf_path, "r");
  if (file == NULL)
    return;

  char line[512];
  while (fgets(line, sizeof(line), file) != NULL) {
    char key[64], value[256];
    if (line[0] == '#' || sscanf(line, " %63[^= \t] = %255[^\n]", key, value) != 2)
      continue;

    size_t len = strlen(value);
    while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t' || value[len - 1] == '\r'))
      value[--len] = 0;

    int index, width, height;
    if (strcmp(key, "address") == 0) {
      if (len < sizeof(pc_address))
        strcpy(pc_address, value);
    } else if (strcmp(key, "port") == 0) {
      pc_port = atoi(value);
    } else if (strcmp(key, "app") == 0) {
      snprintf(last_app, sizeof(last_app), "%.*s", (int) sizeof(last_app) - 1, value);
    } else if (strcmp(key, "resolution") == 0) {
      if (strcasecmp(value, "native") == 0)
        setting_index[SET_RESOLUTION] = 0;
      else if (sscanf(value, "%dx%d", &width, &height) == 2)
        setting_index[SET_RESOLUTION] = resolution_find_or_add(width, height);
    } else if (strcmp(key, "bitrate") == 0) {
      setting_index[SET_BITRATE] = strcasecmp(value, "auto") == 0 ? 0 : int_list_find_or_add(bitrates, &bitrate_count, atoi(value));
    } else if (strcmp(key, "fps") == 0) {
      setting_index[SET_FPS] = int_list_find_or_add(framerates, &framerate_count, atoi(value));
    } else if (strcmp(key, "codec") == 0) {
      if ((index = key_index(value, codec_keys, ARRAY_SIZE(codec_keys))) >= 0)
        setting_index[SET_CODEC] = index;
    } else if (strcmp(key, "remote") == 0) {
      if ((index = key_index(value, remote_keys, ARRAY_SIZE(remote_keys))) >= 0)
        setting_index[SET_REMOTE] = index;
    } else if (strcmp(key, "sfx") == 0) {
      if ((index = key_index(value, sfx_keys, ARRAY_SIZE(sfx_keys))) >= 0)
        setting_index[SET_SFX] = index;
    } else if (strcmp(key, "theme") == 0) {
      if ((index = key_index(value, theme_keys, THEME_COUNT)) >= 0)
        setting_index[SET_THEME] = index;
    }
  }
  fclose(file);
}

static void make_dirs(const char* path) {
  char dir[4096];
  snprintf(dir, sizeof(dir), "%s", path);
  for (char* p = dir + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      mkdir(dir, 0755);
      *p = '/';
    }
  }
  mkdir(dir, 0755);
}

static void launcher_save(void) {
  make_dirs(base_config->key_dir);

  char tmp_path[sizeof(conf_path) + 4];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", conf_path);
  FILE* file = fopen(tmp_path, "w");
  if (file == NULL) {
    fprintf(stderr, "Can't save %s: %s\n", conf_path, strerror(errno));
    return;
  }

  fprintf(file, "# Written by the Moonlight launcher (moonlight ui)\n");
  if (pc_address[0])
    fprintf(file, "address = %s\n", pc_address);
  if (pc_port != 47989)
    fprintf(file, "port = %u\n", pc_port);
  if (last_app[0])
    fprintf(file, "app = %s\n", last_app);
  RESOLUTION res = resolutions[setting_index[SET_RESOLUTION]];
  if (setting_index[SET_RESOLUTION] == 0)
    fprintf(file, "resolution = native\n");
  else
    fprintf(file, "resolution = %dx%d\n", res.width, res.height);
  if (setting_index[SET_BITRATE] == 0)
    fprintf(file, "bitrate = auto\n");
  else
    fprintf(file, "bitrate = %d\n", bitrates[setting_index[SET_BITRATE]]);
  fprintf(file, "fps = %d\n", framerates[setting_index[SET_FPS]]);
  fprintf(file, "codec = %s\n", codec_keys[setting_index[SET_CODEC]]);
  fprintf(file, "remote = %s\n", remote_keys[setting_index[SET_REMOTE]]);
  fprintf(file, "sfx = %s\n", sfx_keys[setting_index[SET_SFX]]);
  fprintf(file, "theme = %s\n", theme_keys[setting_index[SET_THEME]]);

  if (fclose(file) != 0 || rename(tmp_path, conf_path) != 0)
    fprintf(stderr, "Can't save %s: %s\n", conf_path, strerror(errno));
}

static int setting_count(enum setting setting) {
  switch (setting) {
  case SET_RESOLUTION: return resolution_count;
  case SET_BITRATE: return bitrate_count;
  case SET_FPS: return framerate_count;
  case SET_CODEC: return ARRAY_SIZE(codec_names);
  case SET_REMOTE: return ARRAY_SIZE(remote_names);
  case SET_SFX: return ARRAY_SIZE(sfx_names);
  case SET_THEME: return THEME_COUNT;
  default: return 1;
  }
}

static void setting_value(enum setting setting, char* buffer, size_t size) {
  int index = setting_index[setting];
  switch (setting) {
  case SET_RESOLUTION:
    if (index == 0)
      snprintf(buffer, size, "Native");
    else
      snprintf(buffer, size, "%dx%d", resolutions[index].width, resolutions[index].height);
    break;
  case SET_BITRATE:
    if (index == 0)
      snprintf(buffer, size, "Auto");
    else
      snprintf(buffer, size, "%d kbps", bitrates[index]);
    break;
  case SET_FPS: snprintf(buffer, size, "%d FPS", framerates[index]); break;
  case SET_CODEC: snprintf(buffer, size, "%s", codec_names[index]); break;
  case SET_REMOTE: snprintf(buffer, size, "%s", remote_names[index]); break;
  case SET_SFX: snprintf(buffer, size, "%s", sfx_names[index]); break;
  case SET_THEME: snprintf(buffer, size, "%s", theme_names[index]); break;
  default: buffer[0] = 0;
  }
}

static void apply_settings(struct _CONFIGURATION* session) {
  int res = setting_index[SET_RESOLUTION];
  session->stream.width = res == 0 ? native_w : resolutions[res].width;
  session->stream.height = res == 0 ? native_h : resolutions[res].height;
  session->stream.fps = framerates[setting_index[SET_FPS]];
  // "Auto" keeps the bitrate from moonlight.conf / command line, or the default for the resolution
  session->stream.bitrate = setting_index[SET_BITRATE] == 0 ? base_config->stream.bitrate : bitrates[setting_index[SET_BITRATE]];
  session->codec = setting_index[SET_CODEC];
  session->stream.streamingRemotely = remote_values[setting_index[SET_REMOTE]];
}

// ---------------------------------------------------------------------------
// Network jobs
// ---------------------------------------------------------------------------

static int job_main(void* data) {
  JOB* j = data;
  const char* error;

  if (j->address[0] == 0) {
    gs_discover_server(j->address, &j->port);
    if (j->address[0] == 0) {
      j->state = PC_UNKNOWN;
      snprintf(j->message, sizeof(j->message), "No PC found on the network.\nEnter the PC's IP address in Connect > IP Address.");
      goto done;
    }
  }

  gs_error = NULL;
  int ret = gs_init(&j->server, j->address, j->port, j->key_dir, j->debug_level, j->unsupported);
  if (ret != GS_OK) {
    j->state = PC_OFFLINE;
    error = ret == GS_OUT_OF_MEMORY ? "Not enough memory" : gs_error ? gs_error : "Unknown error";
    snprintf(j->message, sizeof(j->message), "Can't connect to %s:\n%s\n\nCheck the IP address and that Sunshine is running on the PC.", j->address, error);
    goto done;
  }

  bool paired_now = false;
  if (j->pair && !j->server.paired) {
    if (gs_pair(&j->server, j->pin) != GS_OK) {
      j->state = PC_NOT_PAIRED;
      snprintf(j->message, sizeof(j->message), "Pairing failed:\n%s", gs_error ? gs_error : "Unknown error");
      goto done;
    }
    paired_now = true;
  }

  if (!j->server.paired) {
    j->state = PC_NOT_PAIRED;
    snprintf(j->message, sizeof(j->message), "Not paired with %s yet.\nSelect Connect > Pair.", j->address);
    goto done;
  }

  j->state = PC_PAIRED;
  PAPP_LIST list = NULL;
  if (gs_applist(&j->server, &list) != GS_OK) {
    snprintf(j->message, sizeof(j->message), "Can't load the apps from %s:\n%s", j->address, gs_error ? gs_error : "Unknown error");
    goto done;
  }

  // libgamestream returns the apps in reverse server order
  int total = 0;
  for (PAPP_LIST app = list; app != NULL; app = app->next) {
    if (app->name != NULL)
      total++;
  }
  j->app_count = total < MAX_APPS ? total : MAX_APPS;
  j->apps_loaded = true;
  int index = total;
  while (list != NULL) {
    PAPP_LIST next = list->next;
    if (list->name != NULL) {
      index--;
      if (index < MAX_APPS) {
        snprintf(j->apps[index].name, MAX_APP_NAME, "%s", list->name);
        j->apps[index].id = list->id;
      }
      free(list->name);
    }
    free(list);
    list = next;
  }

  int len = 0;
  if (paired_now)
    len = snprintf(j->message, sizeof(j->message), "Paired with %s.\n\n", j->address);
  else if (j->pair)
    len = snprintf(j->message, sizeof(j->message), "Already paired with %s.\n\n", j->address);
  if (j->app_count == 0) {
    snprintf(j->message + len, sizeof(j->message) - len, "No apps found. Add apps in Sunshine's web UI.");
  } else {
    len += snprintf(j->message + len, sizeof(j->message) - len, "Apps on %s:", j->address);
    for (int i = 0; i < j->app_count && len < (int) sizeof(j->message); i++)
      len += snprintf(j->message + len, sizeof(j->message) - len, "\n%d. %s", i + 1, j->apps[i].name);
  }

done:
  SDL_AtomicSet(&job_done, 1);
  return 0;
}

static bool job_start(bool pair, bool quiet) {
  if (worker != NULL)
    return false;

  memset(&job, 0, sizeof(job));
  job.pair = pair;
  job.quiet = quiet;
  snprintf(job.address, sizeof(job.address), "%s", pc_address);
  job.port = pc_port;
  job.key_dir = base_config->key_dir;
  job.debug_level = base_config->debug_level;
  job.unsupported = base_config->unsupported;
  for (int i = 0; i < 4; i++)
    job.pin[i] = '0' + rand() % 10;

  SDL_AtomicSet(&job_done, 0);
  worker = SDL_CreateThread(job_main, "ui-network", &job);
  if (worker == NULL) {
    snprintf(panel_body, sizeof(panel_body), "Can't start the network job: %s", SDL_GetError());
    return false;
  }
  return true;
}

static void job_finish(void) {
  SDL_WaitThread(worker, NULL);
  worker = NULL;

  // The address changed while this job ran: its results are for the old one
  if (reconnect_pending) {
    reconnect_pending = false;
    job_start(false, false);
    return;
  }

  bool discovered = pc_address[0] == 0 && job.address[0] != 0;
  snprintf(pc_address, sizeof(pc_address), "%s", job.address);
  pc_port = job.port;
  pc_state = job.state;
  server_valid = job.state == PC_PAIRED;
  if (job.state == PC_PAIRED || job.state == PC_NOT_PAIRED) {
    server = job.server;
    server.serverInfo.address = pc_address;
  }
  // Keep the previous list when only the app list request failed
  if (job.apps_loaded || job.state != PC_PAIRED) {
    app_count = job.app_count;
    memcpy(apps, job.apps, sizeof(apps));
  }
  if (job.state == PC_PAIRED) {
    selection[MENU_PLAY] = 0;
    for (int i = 0; i < app_count; i++) {
      if (strcmp(apps[i].name, last_app) == 0)
        selection[MENU_PLAY] = i;
    }
  }
  if (discovered)
    launcher_save();

  bool failed = job.state != PC_PAIRED || job.app_count == 0;
  if (!job.quiet || failed) {
    panel_title[0] = 0;
    snprintf(panel_body, sizeof(panel_body), "%s", job.message);
    play_sound(failed ? SND_ERROR : SND_ENTER);
  }
}

static void connect_address(const char* address) {
  snprintf(pc_address, sizeof(pc_address), "%s", address);
  pc_port = base_config->port;
  pc_state = PC_UNKNOWN;
  server_valid = false;
  app_count = 0;
  launcher_save();
  if (!job_start(false, false))
    reconnect_pending = true;
}

// ---------------------------------------------------------------------------
// Themes (drawn on a low resolution canvas for a pixel-art look)
// ---------------------------------------------------------------------------

#define STAR_COUNT 140

typedef struct { float x, y, z, phase, speed; } STAR;
static STAR stars[STAR_COUNT];

static float random_float(void) {
  return (float) rand() / RAND_MAX;
}

static void stars_init(void) {
  for (int i = 0; i < STAR_COUNT; i++)
    stars[i] = (STAR) {random_float(), random_float(), random_float(), random_float() * 6.28f, 0.5f + random_float() * 2.5f};
}

static void draw_stars(int cw, int ch, float max_y, float min_bright, float max_bright) {
  for (int i = 0; i < STAR_COUNT; i++) {
    float y = stars[i].y * max_y * ch;
    float twinkle = 0.5f + 0.5f * sinf(now * stars[i].speed + stars[i].phase);
    Uint8 v = min_bright + (max_bright - min_bright) * twinkle;
    set_color(rgba(v, v, v + (255 - v) / 3, 255));
    SDL_RenderDrawPoint(renderer, (int) (stars[i].x * cw), (int) y);
  }
}

static void draw_hills(int cw, int ch, float base, float amp1, float freq1, float amp2, float freq2, float offset, COLOR c) {
  SDL_Rect columns[1024];
  int count = cw < (int) ARRAY_SIZE(columns) ? cw : (int) ARRAY_SIZE(columns);
  for (int x = 0; x < count; x++) {
    int top = (int) (base * ch + amp1 * ch * sinf(x * freq1 + offset) + amp2 * ch * sinf(x * freq2 + offset * 2.3f));
    columns[x] = (SDL_Rect) {x, top, 1, ch - top};
  }
  set_color(c);
  SDL_RenderFillRects(renderer, columns, count);
}

static void draw_mountains(int cw, int ch, float base, float amp1, float freq1, float amp2, float freq2, COLOR c) {
  SDL_Rect columns[1024];
  int count = cw < (int) ARRAY_SIZE(columns) ? cw : (int) ARRAY_SIZE(columns);
  for (int x = 0; x < count; x++) {
    int top = (int) (base * ch - amp1 * ch * fabsf(sinf(x * freq1)) - amp2 * ch * fabsf(sinf(x * freq2 + 1.3f)));
    columns[x] = (SDL_Rect) {x, top, 1, ch - top};
  }
  set_color(c);
  SDL_RenderFillRects(renderer, columns, count);
}

static void draw_shooting_star(int cw, int ch) {
  if (now >= shooting_star_next) {
    shooting_star_time = now;
    shooting_star_next = now + 3 + random_float() * 5;
    shooting_x = cw * (0.15f + random_float() * 0.6f);
    shooting_y = ch * (0.05f + random_float() * 0.2f);
  }
  float t = (now - shooting_star_time) / 0.7f;
  if (t > 1)
    return;

  float head_x = shooting_x + t * cw * 0.25f, head_y = shooting_y + t * ch * 0.12f;
  for (int i = 0; i < 14; i++) {
    float alpha = (1.0f - i / 14.0f) * (1.0f - t);
    set_color(rgba(255, 255, 230, 255 * alpha));
    SDL_RenderDrawPoint(renderer, (int) (head_x - i * 2), (int) (head_y - i));
  }
}

static void draw_ellipse_half(float cx, float cy, float a, float b, bool front, COLOR c) {
  set_color(c);
  for (int i = 0; i < 360; i++) {
    float angle = i * (float) M_PI / 180;
    float y = sinf(angle);
    if ((y >= 0) != front)
      continue;
    SDL_RenderDrawPoint(renderer, (int) (cx + cosf(angle) * a), (int) (cy + y * b));
    SDL_RenderDrawPoint(renderer, (int) (cx + cosf(angle) * a), (int) (cy + y * b + 1));
  }
}

static void draw_theme(int theme, int cw, int ch) {
  switch (theme) {
  case THEME_MOONLIGHT: {
    vertical_gradient(0, 0, cw, ch, rgba(8, 10, 34, 255), rgba(70, 44, 98, 255));
    draw_stars(cw, ch, 0.7f, 60, 255);
    draw_shooting_star(cw, ch);
    float mx = cw * 0.76f, my = ch * 0.22f, mr = ch * 0.1f;
    fill_circle(mx, my, mr * 1.6f, rgba(190, 190, 255, 16));
    fill_circle(mx, my, mr * 1.3f, rgba(200, 200, 255, 28));
    fill_circle(mx, my, mr, rgba(238, 234, 208, 255));
    fill_circle(mx - mr * 0.3f, my - mr * 0.2f, mr * 0.2f, rgba(212, 206, 182, 255));
    fill_circle(mx + mr * 0.3f, my + mr * 0.2f, mr * 0.26f, rgba(212, 206, 182, 255));
    fill_circle(mx - mr * 0.1f, my + mr * 0.5f, mr * 0.13f, rgba(212, 206, 182, 255));
    fill_circle(mx + mr * 0.4f, my - mr * 0.42f, mr * 0.11f, rgba(212, 206, 182, 255));
    draw_hills(cw, ch, 0.7f, 0.04f, 0.031f, 0.02f, 0.077f, 1.0f, rgba(40, 30, 74, 255));
    draw_hills(cw, ch, 0.8f, 0.03f, 0.023f, 0.015f, 0.061f, 4.0f, rgba(18, 14, 40, 255));
    break;
  }
  case THEME_SUNSHINE: {
    vertical_gradient(0, 0, cw, ch, rgba(84, 160, 236, 255), rgba(252, 214, 160, 255));
    float sx = cw * 0.22f, sy = ch * 0.25f, sr = ch * 0.085f;
    fill_circle(sx, sy, sr * 1.7f, rgba(255, 244, 190, 40));
    set_color(rgba(255, 236, 140, 255));
    for (int i = 0; i < 12; i++) {
      float angle = now * 0.25f + i * (float) M_PI / 6;
      SDL_RenderDrawLine(renderer, (int) (sx + cosf(angle) * sr * 1.25f), (int) (sy + sinf(angle) * sr * 1.25f),
                         (int) (sx + cosf(angle) * sr * 1.6f), (int) (sy + sinf(angle) * sr * 1.6f));
    }
    fill_circle(sx, sy, sr, rgba(255, 224, 100, 255));
    for (int i = 0; i < 6; i++) {
      float speed = 2 + (i % 3) * 1.5f;
      float span = cw + ch * 0.4f;
      float x = fmodf(stars[i].x * span + now * speed, span) - ch * 0.2f;
      float y = ch * (0.08f + stars[i].y * 0.35f);
      float r = ch * (0.025f + stars[i].z * 0.02f);
      COLOR cloud = rgba(255, 255, 255, 230);
      fill_circle(x, y, r, cloud);
      fill_circle(x + r * 1.1f, y - r * 0.4f, r * 1.2f, cloud);
      fill_circle(x + r * 2.3f, y, r, cloud);
      fill_rect((int) x, (int) y, (int) (r * 2.3f), (int) r + 1, cloud);
    }
    draw_hills(cw, ch, 0.72f, 0.04f, 0.027f, 0.02f, 0.069f, 2.0f, rgba(122, 192, 92, 255));
    draw_hills(cw, ch, 0.82f, 0.03f, 0.021f, 0.015f, 0.057f, 5.0f, rgba(76, 148, 68, 255));
    break;
  }
  case THEME_KEPLER: {
    vertical_gradient(0, 0, cw, ch, rgba(14, 38, 58, 255), rgba(124, 78, 134, 255));
    draw_stars(cw, ch, 0.6f, 40, 150);
    fill_circle(cw * 0.22f, ch * 0.2f, ch * 0.035f, rgba(172, 204, 222, 255));
    float px = cw * 0.7f, py = ch * 0.3f, pr = ch * 0.13f;
    COLOR ring = rgba(240, 212, 170, 255);
    draw_ellipse_half(px, py, pr * 1.9f, pr * 0.45f, false, ring);
    fill_circle(px, py, pr, rgba(228, 150, 96, 255));
    for (int band = -2; band <= 2; band++) {
      float y = py + band * pr * 0.35f;
      float half = sqrtf(fmaxf(0, pr * pr - (y - py) * (y - py)));
      fill_rect((int) (px - half), (int) y, (int) (half * 2), (int) (pr * 0.12f) + 1, rgba(196, 118, 78, 255));
    }
    draw_ellipse_half(px, py, pr * 1.9f, pr * 0.45f, true, ring);
    draw_mountains(cw, ch, 0.8f, 0.09f, 0.045f, 0.05f, 0.13f, rgba(66, 38, 94, 255));
    draw_mountains(cw, ch, 0.9f, 0.06f, 0.06f, 0.03f, 0.17f, rgba(34, 18, 52, 255));
    break;
  }
  default: {
    fill_rect(0, 0, cw, ch, rgba(0, 0, 0, 255));
    float cx = cw / 2.0f, cy = ch * 0.4f;
    for (int i = 0; i < STAR_COUNT; i++) {
      float z = fmodf(stars[i].z - now * 0.12f * (0.5f + stars[i].speed / 3), 1.0f);
      if (z < 0)
        z += 1.0f;
      z = 0.05f + z * 0.95f;
      float x = cx + (stars[i].x - 0.5f) * cw * 0.5f / z;
      float y = cy + (stars[i].y - 0.5f) * ch * 0.5f / z;
      Uint8 v = 255 * (1.0f - z);
      set_color(rgba(v, v, v, 255));
      SDL_RenderDrawPoint(renderer, (int) x, (int) y);
      if (z < 0.3f)
        SDL_RenderDrawPoint(renderer, (int) x + 1, (int) y);
    }
    break;
  }
  }
}

static int canvas_scale(void) {
  int scale = screen_h / 180;
  return scale < 1 ? 1 : scale;
}

static void draw_background(void) {
  int k = canvas_scale();
  SDL_RenderSetScale(renderer, k, k);
  draw_theme(setting_index[SET_THEME], (screen_w + k - 1) / k, (screen_h + k - 1) / k);
  SDL_RenderSetScale(renderer, 1, 1);
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

static void draw_splash(float t) {
  fill_rect(0, 0, screen_w, screen_h, rgba(0, 0, 0, 255));

  int k = canvas_scale();
  int cw = (screen_w + k - 1) / k, ch = (screen_h + k - 1) / k;
  SDL_RenderSetScale(renderer, k, k);
  float appear = t / 0.8f;
  draw_stars(cw, ch, 1.0f, 0, 120 * fminf(1, appear));
  fill_circle(cw / 2.0f, ch * 0.32f, ch * 0.11f * 1.5f, rgba(190, 190, 255, 20 * fminf(1, appear)));
  fill_circle(cw / 2.0f, ch * 0.32f, ch * 0.11f, rgba(238, 234, 208, 255 * fminf(1, appear)));
  fill_circle(cw / 2.0f + ch * 0.045f, ch * 0.32f - ch * 0.03f, ch * 0.095f, rgba(0, 0, 0, 255 * fminf(1, appear)));
  SDL_RenderSetScale(renderer, 1, 1);

  const char* title = "MOONLIGHT";
  int scale = s_large + 2;
  int letters = (int) ((t - 0.4f) * 12);
  int width = text_width(title, scale);
  int x = (screen_w - width) / 2, y = screen_h / 2 + scale * 4;
  for (int i = 0; i < 9 && i < letters; i++) {
    char letter[2] = {title[i], 0};
    draw_text(letter, x + i * 8 * scale, y, scale, WHITE);
  }
  float sub = (t - 1.3f) / 0.5f;
  if (sub > 0)
    draw_text_centered("EMBEDDED", screen_w / 2, y + scale * 12, s_normal, with_alpha(DIM, sub), screen_w);

  float out = (t - (SPLASH_TIME - 0.4f)) / 0.4f;
  if (out > 0)
    fill_rect(0, 0, screen_w, screen_h, rgba(0, 0, 0, 255 * fminf(1, out)));
}

static void draw_status(void) {
  int pad = s_small * 4;
  char status[96];
  const char* address = pc_address[0] ? pc_address : "auto";
  static const char* dots[] = {"", ".", "..", "..."};
  if (worker != NULL)
    snprintf(status, sizeof(status), "%s %s%s", job.pair ? "Pairing with" : pc_address[0] ? "Connecting to" : "Searching for PC", pc_address, dots[(int) (now * 3) % 4]);
  else if (pc_state == PC_PAIRED)
    snprintf(status, sizeof(status), "%s - paired", address);
  else if (pc_state == PC_NOT_PAIRED)
    snprintf(status, sizeof(status), "%s - not paired", address);
  else if (pc_state == PC_OFFLINE)
    snprintf(status, sizeof(status), "%s - offline", address);
  else
    snprintf(status, sizeof(status), pc_address[0] ? "PC %s" : "No PC selected", address);

  char clock[8];
  time_t t = time(NULL);
  struct tm local;
  strftime(clock, sizeof(clock), "%H:%M", localtime_r(&t, &local));
  int clock_width = text_width(clock, s_normal);
  draw_text_centered(clock, screen_w / 2, pad, s_normal, WHITE, screen_w);
  // Left of the clock
  draw_text_fit(status, pad, pad, s_small, DIM, screen_w / 2 - clock_width / 2 - pad * 2);
}

static int row_y(void) {
  return screen_h * 0.74f;
}

// Largest scale (up to scale) at which every visible label fits max_width
static int common_scale(const char** labels, int first, int last, int scale, int max_width) {
  for (int i = first; i <= last; i++) {
    while (scale > 1 && text_width(labels[i], scale) > max_width)
      scale--;
  }
  return scale;
}

static void draw_carousel(const char** names, int count, int selected, const char** values) {
  int slot = screen_w / 3;
  int max_width = slot - s_normal * 4;
  int y = row_y();
  int first = selected > 0 ? selected - 1 : 0, last = selected < count - 1 ? selected + 1 : count - 1;
  int scale = common_scale(names, first, last, s_normal, max_width);
  int value_scale = s_normal;
  for (int i = first; values != NULL && i <= last; i++) {
    if (values[i] != NULL)
      value_scale = common_scale(&values[i], 0, 0, value_scale, max_width);
  }
  for (int i = -1; i <= 1; i++) {
    int index = selected + i;
    if (index < 0 || index >= count)
      continue;
    int cx = slot * (i + 1) + slot / 2;
    draw_text_centered(names[index], cx, y, scale, i == 0 ? WHITE : GREY, max_width);
    if (values != NULL && values[index] != NULL)
      draw_text_centered(values[index], cx, y + 8 * s_normal + s_normal * 6, value_scale, i == 0 ? DIM : GREY, max_width);
  }

  // Arrows hint that more entries follow
  int arrow_y = y + s_normal;
  if (selected > 1)
    draw_text("<", s_small * 2, arrow_y, s_small, GREY);
  if (selected < count - 2)
    draw_text(">", screen_w - s_small * 10, arrow_y, s_small, GREY);
}

static void draw_menu(void) {
  // Darken the bottom of the scene so the menu stays readable on every theme:
  // ramp up to the menu row, then stay dark
  int band = screen_h * 0.56f, full = row_y() - 8 * s_small * 2;
  for (int y = band; y < screen_h; y += 2)
    fill_rect(0, y, screen_w, 2, rgba(0, 0, 0, 170 * fminf(1, (float) (y - band) / (full - band))));

  int y = row_y();
  if (menu == MENU_MAIN) {
    int slot = screen_w / MAIN_COUNT;
    int scale = common_scale(main_names, 0, MAIN_COUNT - 1, s_large, slot - s_large * 2);
    for (int i = 0; i < MAIN_COUNT; i++) {
      Uint8 v = 255 * glow[i];
      draw_text_centered(main_names[i], slot * i + slot / 2, y, scale, rgba(v, v, v, 255), slot - s_large * 2);
    }
    return;
  }

  draw_text_centered(menu_titles[menu], screen_w / 2, y - 8 * s_small - s_small * 5, s_small, GREY, screen_w);
  int line_y = y + 8 * s_normal + s_normal * 3;
  fill_rect(0, line_y, screen_w, s_normal > 1 ? s_normal / 2 + 1 : 1, rgba(255, 255, 255, 220));

  if (menu == MENU_SETTINGS) {
    char values[SET_COUNT][32];
    const char* value_ptrs[SET_COUNT];
    for (int i = 0; i < SET_COUNT; i++) {
      setting_value(i, values[i], sizeof(values[i]));
      value_ptrs[i] = values[i];
    }
    draw_carousel(setting_names, SET_COUNT, selection[MENU_SETTINGS], value_ptrs);
  } else if (menu == MENU_CONNECT) {
    const char* values[CONNECT_COUNT] = {NULL, pc_address[0] ? pc_address : "Auto", NULL};
    draw_carousel(connect_names, CONNECT_COUNT, selection[MENU_CONNECT], values);
  } else if (menu == MENU_PLAY) {
    if (app_count == 0) {
      const char* text = worker != NULL ? "Loading apps..." : pc_state == PC_NOT_PAIRED ? "Not paired - see Connect > Pair" : "No apps - press Y to reload";
      draw_text_centered(text, screen_w / 2, y, s_normal, GREY, screen_w - s_normal * 8);
      return;
    }
    const char* names[MAX_APPS];
    const char* values[MAX_APPS];
    for (int i = 0; i < app_count; i++) {
      names[i] = apps[i].name;
      values[i] = server_valid && server.currentGame == apps[i].id ? "Running" : NULL;
    }
    draw_carousel(names, app_count, selection[MENU_PLAY], values);
  }
}

static void draw_panel(void) {
  char title[64], body[sizeof(panel_body)];
  if (worker != NULL && job.pair) {
    snprintf(title, sizeof(title), "PIN %s", job.pin);
    snprintf(body, sizeof(body), "On the PC open Sunshine's web UI (https://localhost:47990) > PIN, pick this device's request and enter %s with a name for it.\n\nWaiting for the PC...", job.pin);
  } else if (worker != NULL && !job.quiet) {
    title[0] = 0;
    snprintf(body, sizeof(body), job.address[0] ? "Connecting to %s..." : "Searching for your PC...", job.address);
  } else {
    snprintf(title, sizeof(title), "%s", panel_title);
    snprintf(body, sizeof(body), "%s", panel_body);
  }
  if (title[0] == 0 && body[0] == 0)
    return;

  int width = screen_w * 0.86f;
  int pad = s_normal * 5;
  char lines[MAX_TEXT_LINES][128];
  int line_height = 8 * s_normal + s_normal * 3;
  int max_lines = (int) ((row_y() - screen_h * 0.12f - pad * 2 - (title[0] ? 8 * s_large * 2 : 0)) / line_height);
  if (max_lines > MAX_TEXT_LINES)
    max_lines = MAX_TEXT_LINES;
  int count = wrap_text(body, (width - pad * 2) / (8 * s_normal), lines, max_lines > 1 ? max_lines : 1);

  int height = pad * 2 + count * line_height + (title[0] ? 8 * s_large * 2 : 0);
  int x = (screen_w - width) / 2, y = screen_h * 0.12f;
  fill_round_rect(x, y, width, height, s_normal * 4, rgba(0, 0, 0, 205));

  int text_y = y + pad;
  if (title[0]) {
    draw_text_centered(title, screen_w / 2, text_y, s_large * 2 - 1, WHITE, width - pad * 2);
    text_y += 8 * s_large * 2;
  }
  for (int i = 0; i < count; i++)
    draw_text_centered(lines[i], screen_w / 2, text_y + i * line_height, s_normal, WHITE, width - pad);
}

static void draw_numpad(void) {
  int gap = s_normal * 2;
  int cell_w = 8 * s_normal * 5 + s_normal * 6;
  int cell_h = 8 * s_normal + s_normal * 6;
  int pad = s_normal * 5;
  int width = cell_w * 3 + gap * 2 + pad * 2;
  int field_h = 8 * s_normal + s_normal * 6;
  int height = pad * 3 + field_h + cell_h * 5 + gap * 4 + 8 * s_small + s_small * 4;
  int x = (screen_w - width) / 2, y = (screen_h - height) / 2;
  fill_round_rect(x, y, width, height, s_normal * 4, rgba(0, 0, 0, 225));

  draw_text_centered("PC IP address", screen_w / 2, y + pad, s_small, DIM, width);
  int field_y = y + pad + 8 * s_small + s_small * 4;
  fill_round_rect(x + pad, field_y, width - pad * 2, field_h, s_normal * 2, rgba(255, 255, 255, 40));
  char field[MAX_ADDRESS_SIZE + 2];
  snprintf(field, sizeof(field), "%s%s", numpad_text, ((int) (now * 2) % 2) ? "_" : " ");
  draw_text_centered(field, screen_w / 2, field_y + s_normal * 3, s_normal, WHITE, width - pad * 2);

  int grid_y = field_y + field_h + pad;
  for (int i = 0; i < (int) ARRAY_SIZE(numpad_keys); i++) {
    int cx = x + pad + (i % 3) * (cell_w + gap);
    int cy = grid_y + (i / 3) * (cell_h + gap);
    bool selected = i == numpad_selection;
    fill_round_rect(cx, cy, cell_w, cell_h, s_normal * 2, selected ? rgba(210, 214, 255, 240) : rgba(255, 255, 255, 30));
    draw_text_centered(numpad_keys[i], cx + cell_w / 2, cy + s_normal * 3, s_normal, selected ? rgba(20, 20, 40, 255) : WHITE, cell_w);
  }
}

static void draw_hints(void) {
  const char* hint;
  if (numpad_open)
    hint = "A Press   Y Delete   Start Done   B Cancel";
  else if (menu == MENU_MAIN)
    hint = "A Select   L1+R1 Quit";
  else if (menu == MENU_SETTINGS)
    hint = "Left/Right Setting   Up/Down Change   B Back";
  else if (menu == MENU_PLAY)
    hint = "A Play   Y Reload   B Back";
  else
    hint = "A Select   B Back";
  draw_text_centered(hint, screen_w / 2, screen_h - 8 * s_small - s_small * 4, s_small, GREY, screen_w);
}

static void render(void) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  if (splash_active) {
    draw_splash(now - splash_start);
    return;
  }

  draw_background();
  draw_menu();
  draw_status();
  draw_panel();
  if (numpad_open)
    draw_numpad();
  draw_hints();

  float fade = (now - fade_start) / FADE_TIME;
  if (fade < 0)
    fade = 0;
  if (fade > 1)
    fade = 1;
  float alpha = fading_out ? fade : 1 - fade;
  if (alpha > 0)
    fill_rect(0, 0, screen_w, screen_h, rgba(0, 0, 0, 255 * alpha));
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static void clear_panel(void) {
  panel_title[0] = panel_body[0] = 0;
}

static void enter_menu(enum menu target) {
  menu = target;
  clear_panel();
  if (target == MENU_PLAY && app_count == 0 && worker == NULL)
    job_start(false, false);
}

static void launch_app(int index) {
  if (worker != NULL || !server_valid) {
    play_sound(SND_ERROR);
    return;
  }
  snprintf(last_app, sizeof(last_app), "%s", apps[index].name);
  launcher_save();
  play_sound(SND_ENTER);
  fading_out = true;
  fade_start = now;
  result = RESULT_PLAY;
}

static void numpad_press(int key) {
  size_t len = strlen(numpad_text);
  if (key == NUMPAD_DONE) {
    numpad_open = false;
    SDL_StopTextInput();
    play_sound(SND_ENTER);
    connect_address(numpad_text);
    return;
  }

  play_sound(SND_KEY);
  if (key == NUMPAD_DEL) {
    if (len > 0)
      numpad_text[len - 1] = 0;
  } else if (key == NUMPAD_CLEAR) {
    numpad_text[0] = 0;
  } else if (len + 1 < sizeof(numpad_text)) {
    numpad_text[len] = numpad_keys[key][0];
    numpad_text[len + 1] = 0;
  }
}

static void numpad_action(enum action action) {
  int count = ARRAY_SIZE(numpad_keys);
  switch (action) {
  case ACT_LEFT: numpad_selection = (numpad_selection + count - 1) % count; play_sound(SND_SIDE); break;
  case ACT_RIGHT: numpad_selection = (numpad_selection + 1) % count; play_sound(SND_SIDE); break;
  case ACT_UP: numpad_selection = (numpad_selection + count - 3) % count; play_sound(SND_UP); break;
  case ACT_DOWN: numpad_selection = (numpad_selection + 3) % count; play_sound(SND_DOWN); break;
  case ACT_A: numpad_press(numpad_selection); break;
  case ACT_Y: case ACT_X: numpad_press(NUMPAD_DEL); break;
  case ACT_START: numpad_press(NUMPAD_DONE); break;
  case ACT_B: numpad_open = false; SDL_StopTextInput(); play_sound(SND_BACK); break;
  default: break;
  }
}

static int wrap_index(int index, int count) {
  return (index % count + count) % count;
}

static void handle_action(enum action action) {
  if (action == ACT_QUIT) {
    fading_out = true;
    fade_start = now;
    result = RESULT_QUIT;
    return;
  }
  if (fading_out)
    return;

  if (splash_active) {
    if (action == ACT_A || action == ACT_B || action == ACT_START) {
      splash_active = false;
      fade_start = now;
    }
    return;
  }

  if (numpad_open) {
    numpad_action(action);
    return;
  }

  if (worker == NULL && action != ACT_NONE)
    clear_panel();

  int* sel = &selection[menu];
  switch (menu) {
  case MENU_MAIN:
    if (action == ACT_LEFT || action == ACT_RIGHT) {
      *sel = wrap_index(*sel + (action == ACT_LEFT ? -1 : 1), MAIN_COUNT);
      play_sound(SND_SIDE);
    } else if (action == ACT_A) {
      play_sound(SND_ENTER);
      enter_menu(*sel == MAIN_SETTINGS ? MENU_SETTINGS : *sel == MAIN_PLAY ? MENU_PLAY : MENU_CONNECT);
    }
    break;
  case MENU_SETTINGS:
    if (action == ACT_LEFT || action == ACT_RIGHT) {
      *sel = wrap_index(*sel + (action == ACT_LEFT ? -1 : 1), SET_COUNT);
      play_sound(SND_SIDE);
    } else if (action == ACT_UP || action == ACT_DOWN || action == ACT_A) {
      int* index = &setting_index[*sel];
      *index = wrap_index(*index + (action == ACT_DOWN ? -1 : 1), setting_count(*sel));
      play_sound(action == ACT_DOWN ? SND_DOWN : SND_UP);
    } else if (action == ACT_B) {
      launcher_save();
      play_sound(SND_BACK);
      enter_menu(MENU_MAIN);
    }
    break;
  case MENU_PLAY:
    if ((action == ACT_LEFT || action == ACT_RIGHT) && app_count > 0) {
      *sel = wrap_index(*sel + (action == ACT_LEFT ? -1 : 1), app_count);
      play_sound(SND_SIDE);
    } else if (action == ACT_A && app_count > 0) {
      launch_app(*sel);
    } else if (action == ACT_Y || action == ACT_X || (action == ACT_A && app_count == 0)) {
      play_sound(job_start(false, false) ? SND_ENTER : SND_ERROR);
    } else if (action == ACT_B) {
      play_sound(SND_BACK);
      enter_menu(MENU_MAIN);
    }
    break;
  case MENU_CONNECT:
    if (action == ACT_LEFT || action == ACT_RIGHT) {
      *sel = wrap_index(*sel + (action == ACT_LEFT ? -1 : 1), CONNECT_COUNT);
      play_sound(SND_SIDE);
    } else if (action == ACT_A) {
      if (*sel == CONNECT_ADDRESS) {
        snprintf(numpad_text, sizeof(numpad_text), "%s", pc_address);
        numpad_selection = 0;
        numpad_open = true;
        SDL_StartTextInput();
        play_sound(SND_ENTER);
      } else {
        play_sound(job_start(*sel == CONNECT_PAIR, false) ? SND_ENTER : SND_ERROR);
      }
    } else if (action == ACT_B) {
      play_sound(SND_BACK);
      enter_menu(MENU_MAIN);
    }
    break;
  default:
    break;
  }
}

static bool is_direction(enum action action) {
  return action == ACT_UP || action == ACT_DOWN || action == ACT_LEFT || action == ACT_RIGHT;
}

static void press(enum action action) {
  handle_action(action);
  if (is_direction(action)) {
    held_action = action;
    next_repeat = SDL_GetTicks() + REPEAT_DELAY;
  }
}

static void release(enum action action) {
  if (held_action == action)
    held_action = ACT_NONE;
}

static enum action controller_action(Uint8 button) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return ACT_UP;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return ACT_DOWN;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return ACT_LEFT;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return ACT_RIGHT;
  case SDL_CONTROLLER_BUTTON_A: return ACT_A;
  case SDL_CONTROLLER_BUTTON_B: return ACT_B;
  case SDL_CONTROLLER_BUTTON_X: return ACT_X;
  case SDL_CONTROLLER_BUTTON_Y: return ACT_Y;
  case SDL_CONTROLLER_BUTTON_START: return ACT_START;
  case SDL_CONTROLLER_BUTTON_BACK: return ACT_SELECT;
  default: return ACT_NONE;
  }
}

static void axis_motion(int* state, int value, enum action negative, enum action positive) {
  int direction = *state;
  if (value < -AXIS_PRESS)
    direction = -1;
  else if (value > AXIS_PRESS)
    direction = 1;
  else if (value > -AXIS_RELEASE && value < AXIS_RELEASE)
    direction = 0;

  if (direction == *state)
    return;
  if (*state != 0)
    release(*state < 0 ? negative : positive);
  *state = direction;
  if (direction != 0)
    press(direction < 0 ? negative : positive);
}

static void handle_event(SDL_Event* event) {
  switch (event->type) {
  case SDL_QUIT:
    handle_action(ACT_QUIT);
    break;
  case SDL_CONTROLLERDEVICEADDED:
    SDL_GameControllerOpen(event->cdevice.which);
    break;
  case SDL_CONTROLLERBUTTONDOWN: {
    SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cbutton.which);
    Uint8 button = event->cbutton.button;
    if ((button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) && controller != NULL &&
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) &&
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
      handle_action(ACT_QUIT);
    else
      press(controller_action(button));
    break;
  }
  case SDL_CONTROLLERBUTTONUP:
    release(controller_action(event->cbutton.button));
    break;
  case SDL_CONTROLLERAXISMOTION:
    if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
      axis_motion(&axis_x, event->caxis.value, ACT_LEFT, ACT_RIGHT);
    else if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
      axis_motion(&axis_y, event->caxis.value, ACT_UP, ACT_DOWN);
    break;
  case SDL_TEXTINPUT:
    if (numpad_open) {
      for (const char* c = event->text.text; *c; c++) {
        size_t len = strlen(numpad_text);
        bool allowed = (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '.' || *c == ':' || *c == '-';
        if (allowed && len + 1 < sizeof(numpad_text)) {
          numpad_text[len] = *c;
          numpad_text[len + 1] = 0;
          play_sound(SND_KEY);
        }
      }
    }
    break;
  case SDL_KEYDOWN: {
    SDL_Keycode key = event->key.keysym.sym;
    enum action action = ACT_NONE;
    switch (key) {
    case SDLK_UP: action = ACT_UP; break;
    case SDLK_DOWN: action = ACT_DOWN; break;
    case SDLK_LEFT: action = ACT_LEFT; break;
    case SDLK_RIGHT: action = ACT_RIGHT; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: action = numpad_open ? ACT_START : ACT_A; break;
    case SDLK_SPACE: action = numpad_open ? ACT_NONE : ACT_A; break;
    case SDLK_BACKSPACE: action = numpad_open ? ACT_Y : ACT_B; break;
    case SDLK_ESCAPE: action = menu == MENU_MAIN && !numpad_open && !splash_active ? ACT_QUIT : ACT_B; break;
    case SDLK_y: case SDLK_F5: action = numpad_open ? ACT_NONE : ACT_Y; break;
    default: break;
    }
    // SDL repeats held keys itself
    if (action != ACT_NONE && (!event->key.repeat || is_direction(action)))
      handle_action(action);
    break;
  }
  default:
    break;
  }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

static bool video_init(const struct _CONFIGURATION* base) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
    return false;
  }

  SDL_DisplayMode mode;
  if (SDL_GetDesktopDisplayMode(0, &mode) == 0) {
    native_w = mode.w;
    native_h = mode.h;
  } else {
    native_w = base->stream.width;
    native_h = base->stream.height;
  }

  int width = base->fullscreen ? native_w : (base->stream.width < 1280 ? base->stream.width : 1280);
  int height = base->fullscreen ? native_h : (base->stream.height < 720 ? base->stream.height : 720);
  Uint32 flags = base->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE;
  window = SDL_CreateWindow("Moonlight", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
  if (window == NULL) {
    fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
    return false;
  }

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL)
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (renderer == NULL) {
    fprintf(stderr, "Could not create renderer: %s\n", SDL_GetError());
    return false;
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  font = create_font_texture();
  if (font == NULL) {
    fprintf(stderr, "Could not create font texture: %s\n", SDL_GetError());
    return false;
  }

  SDL_ShowCursor(SDL_DISABLE);
  SDL_StopTextInput();

  if (base->mapping != NULL)
    SDL_GameControllerAddMappingsFromFile(base->mapping);
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (SDL_IsGameController(i))
      SDL_GameControllerOpen(i);
  }

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
    SDL_AudioSpec want = {0}, have;
    want.freq = 22050;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio != 0)
      SDL_PauseAudioDevice(audio, 0);
  }
  if (audio == 0)
    fprintf(stderr, "Launcher sounds disabled: %s\n", SDL_GetError());

  return true;
}

static void video_cleanup(void) {
  if (audio != 0)
    SDL_CloseAudioDevice(audio);
  audio = 0;
  if (font != NULL)
    SDL_DestroyTexture(font);
  font = NULL;
  if (renderer != NULL)
    SDL_DestroyRenderer(renderer);
  renderer = NULL;
  if (window != NULL)
    SDL_DestroyWindow(window);
  window = NULL;
  // Release the display and controllers completely for the stream
  SDL_Quit();
}

bool ui_run(const struct _CONFIGURATION* base, struct _CONFIGURATION* session, struct _SERVER_DATA* server_out, const char* message, bool splash) {
  base_config = base;
  snprintf(conf_path, sizeof(conf_path), "%s/launcher.conf", base->key_dir);

  if (!settings_loaded) {
    srand(time(NULL) ^ getpid());
    settings_init(base);
    launcher_load();
    stars_init();
    selection[MENU_MAIN] = MAIN_PLAY;
    settings_loaded = true;
  }

  if (!video_init(base)) {
    video_cleanup();
    exit(1);
  }

  menu = MENU_MAIN;
  numpad_open = false;
  for (int i = 0; i < MAIN_COUNT; i++)
    glow[i] = 0.45f;
  clear_panel();
  if (message != NULL)
    snprintf(panel_body, sizeof(panel_body), "%s", message);
  result = RESULT_NONE;
  fading_out = false;
  held_action = ACT_NONE;
  axis_x = axis_y = 0;

  Uint32 start = SDL_GetTicks();
  now = 0;
  splash_active = splash;
  splash_start = 0;
  fade_start = splash ? SPLASH_TIME : 0;
  shooting_star_next = 2;

  // Refresh pairing state and apps (also picks up a game left running)
  if (worker == NULL)
    job_start(false, true);

  while (result == RESULT_NONE || (now - fade_start) < FADE_TIME) {
    Uint32 frame_start = SDL_GetTicks();
    now = (frame_start - start) / 1000.0f;

    SDL_Event event;
    while (SDL_PollEvent(&event))
      handle_event(&event);

    if (held_action != ACT_NONE && SDL_TICKS_PASSED(frame_start, next_repeat)) {
      handle_action(held_action);
      next_repeat = frame_start + REPEAT_INTERVAL;
    }

    if (worker != NULL && SDL_AtomicGet(&job_done))
      job_finish();

    if (splash_active && now - splash_start >= SPLASH_TIME) {
      splash_active = false;
      fade_start = now;
    }

    float dt = 1.0f / 60;
    for (int i = 0; i < MAIN_COUNT; i++) {
      float target = menu == MENU_MAIN && selection[MENU_MAIN] == i ? 1.0f : 0.45f;
      glow[i] += (target - glow[i]) * fminf(1, dt * 8);
    }

    SDL_GetRendererOutputSize(renderer, &screen_w, &screen_h);
    int base_scale = screen_h / 240 > 0 ? screen_h / 240 : 1;
    s_normal = base_scale;
    s_small = base_scale >= 3 ? base_scale - 1 : 1;
    s_large = base_scale + 1;

    render();
    SDL_RenderPresent(renderer);

    // Cap at ~60 fps when the renderer has no vsync
    Uint32 elapsed = SDL_GetTicks() - frame_start;
    if (elapsed < 16)
      SDL_Delay(16 - elapsed);
  }

  if (result == RESULT_QUIT) {
    // Settings are otherwise saved when leaving the Settings menu
    launcher_save();
    // A pairing request can block until the PC answers; don't wait for it
    if (worker != NULL)
      SDL_DetachThread(worker);
    worker = NULL;
    video_cleanup();
    return false;
  }

  apply_settings(session);
  snprintf(session_app, sizeof(session_app), "%s", last_app);
  snprintf(session_address, sizeof(session_address), "%s", pc_address);
  session->app = session_app;
  session->address = session_address;
  session->port = pc_port;
  *server_out = server;
  server_out->serverInfo.address = session_address;

  video_cleanup();
  return true;
}

#endif /* HAVE_SDL */
