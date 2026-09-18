// omanoise-engine — procedural soundscape generator for the Omanoise
// Omarchy shell plugin (sound engine v5).
//
// Streams float32 stereo to PipeWire and takes text commands on stdin. Every
// parameter change is echoed back on stdout as a single `state {json}` line so
// the UI mirrors the engine.
//
// Commands (one per line):
//   play | pause | toggle | quit | state
//   mode focus|relax|environment      (aliases: sleep -> relax, ocean/env -> environment)
//   volume|intensity|brightness|tonal <0..1>
//   env ocean|rain|fire|wind|stream|birds <0..1>
//   binaural|adaptive|pulse|modulation 0|1
//   envonly|sway 0|1                  (Environment mode: mute the tonal bed / levels sway)
//   swayrate <0..1>                   (sway period, 8 min .. 1 min)
//
// Offline:
//   --render <mode> <seconds> <out.f32> [--tail <seconds>] [--env name=v,name=v]
//                                       [--set key=v,key=v]   (any 0..1 param or 0|1 flag)
//   --render-bank <dir>
//   --stats                      (event counts + the roughness metric, stderr)
//   --play                       (start playing immediately)
//
// Build: see build.sh (gcc -O2 ... -lpipewire-0.3 -lfluidsynth -lpthread -lm)

#include "common.h"
#include "control.h"
#include "bank.h"
#include "brain.h"
#include "dsp.h"
#include "env.h"
#include "layers.h"
#include "padsynth.h"
#include "sf2.h"

#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#define STATE_VERSION 5

// -------------------------------------------------- bank + SoundFont worker

static bank_t g_bank;
static sf2_t *g_sf2 = NULL;
static recbank_t g_recs;
static pthread_t g_bank_thread;
static int g_bank_started = 0;

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// ------------------------------------------------- recorded environment set
//
// `<pluginDir>/sounds/<name>.wav`, the plugin directory being the parent of the
// directory this binary lives in. `OMANOISE_SOUNDS` overrides it (the
// missing-file acceptance run points it at an empty directory).

static void sounds_dir(char *out, size_t n) {
  const char *e = getenv("OMANOISE_SOUNDS");
  if (e && *e) { snprintf(out, n, "%s", e); return; }
  char exe[480];
  ssize_t k = readlink("/proc/self/exe", exe, sizeof exe - 1);
  if (k > 0) {
    exe[k] = 0;
    char *s = strrchr(exe, '/');                       // .../bin/omanoise-engine
    if (s) { *s = 0; s = strrchr(exe, '/'); if (s) *s = 0; }
    snprintf(out, n, "%s/sounds", exe);
    return;
  }
  snprintf(out, n, "sounds");
}

// Loads every recording that exists, measures it, derives its calibration gain
// and publishes the whole set with one atomic pointer swap. Worker thread only.
static void sounds_load(void) {
  char dir[512];
  sounds_dir(dir, sizeof dir);
  size_t bytes = 0;
  int loaded = 0;
  for (int i = 0; i < ENV_N; i++) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.wav", dir, ENV_FILE[i]);
    wav_t *w = &g_recs.w[i];
    int rc = wav_load(path, w);
    if (rc == 0) {
      wav_measure(w);
      // Level 1.0 alone has to land on the mode's -23 LUFS target *before* the
      // auto-trim, so the gain is the distance from the loudness the servo
      // will see to that target, plus what the rest of the chain adds.
      w->cal_db = REC_TARGET_LUFS - w->servo_lufs + REC_OFFSET_DB;
      bytes += (size_t)w->frames * (size_t)w->ch * sizeof(int16_t);
      loaded++;
      fprintf(stderr, "sound %-6s %7.1f s  file %.1f LUFS (short-term mean %.1f)  "
                      "cal %+5.1f dB  %5.1f MB\n",
              ENV_NAME[i], (double)w->seconds, (double)w->lufs, (double)w->servo_lufs,
              (double)w->cal_db, (double)w->frames * w->ch * 2.0 / 1048576.0);
    } else if (rc == -1 && i > ENV_RAIN) {
      // ocean and rain are synthesized, so a missing file there is normal.
      fprintf(stderr, "omanoise: %s missing — the %s slot is unavailable\n", path, ENV_NAME[i]);
    }
  }
  for (int i = 0; i < ENV_N; i++)
    atomic_store(&ctl.sounds[i], (i <= ENV_RAIN || g_recs.w[i].d) ? 1 : 0);
  engine_set_sounds(&g_recs);
  fprintf(stderr, "sounds %d recording(s), %.1f MB\n", loaded, bytes / 1048576.0);
}

static void sounds_free(void) {
  for (int i = 0; i < ENV_N; i++) wav_free(&g_recs.w[i]);
}

// Everything expensive happens here: the PADsynth tables (four 2^18-point
// inverse transforms) and the SoundFont load, which is also where FluidSynth
// pages sample data in. The realtime thread only ever sees finished objects.
static void *bank_worker(void *arg) {
  uint64_t seed = (uint64_t)(uintptr_t)arg;
  double t0 = now_ms();
  if (bank_render(&g_bank, seed) == 0) {
    engine_set_bank(&g_bank);
    fprintf(stderr, "bank ready %.0f ms (%.1f MB)\n", g_bank.ms, g_bank.bytes / 1048576.0);
  } else {
    fprintf(stderr, "bank render failed\n");
  }
  double sf_ms = 0.0;
  g_sf2 = sf2_create(sf2_path(), &sf_ms);
  if (g_sf2) {
    engine_set_sf2(g_sf2);
    fprintf(stderr, "soundfont ready %.0f ms (%s)\n", sf_ms, sf2_path());
  } else {
    fprintf(stderr, "soundfont missing or unreadable: %s (pads and nature only)\n", sf2_path());
  }
  sounds_load();
  fprintf(stderr, "sounds ready %.0f ms total\n", now_ms() - t0);
  fflush(stderr);
  return NULL;
}

static void bank_start(uint64_t seed) {
  if (g_bank_started) return;
  g_bank_started = 1;
  if (pthread_create(&g_bank_thread, NULL, bank_worker, (void *)(uintptr_t)seed) != 0) {
    bank_worker((void *)(uintptr_t)seed);      // fall back to synchronous
    g_bank_thread = 0;
  }
}

static void denormals_off(void) {
#if defined(__x86_64__) || defined(__i386__)
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

// ----------------------------------------------------------- persistence

static char state_path[512];
static const char *g_daypart = "day";

static void state_json(char *buf, size_t n) {
  // The two six-key objects are built first so the format string below stays
  // readable as the bank grows.
  char envs[256], snds[256];
  size_t ep = 0, sp = 0;
  ep += (size_t)snprintf(envs + ep, sizeof envs - ep, "{");
  sp += (size_t)snprintf(snds + sp, sizeof snds - sp, "{");
  for (int i = 0; i < ENV_N; i++) {
    ep += (size_t)snprintf(envs + ep, sizeof envs - ep, "%s\"%s\":%.3f",
                           i ? "," : "", ENV_NAME[i], (double)atomic_load(&ctl.env[i]));
    sp += (size_t)snprintf(snds + sp, sizeof snds - sp, "%s\"%s\":%s",
                           i ? "," : "", ENV_NAME[i],
                           atomic_load(&ctl.sounds[i]) ? "true" : "false");
  }
  snprintf(envs + ep, sizeof envs - ep, "}");
  snprintf(snds + sp, sizeof snds - sp, "}");

  snprintf(buf, n,
    "{\"version\":%d,\"playing\":%s,\"mode\":\"%s\",\"volume\":%.3f,\"intensity\":%.3f,"
    "\"brightness\":%.3f,\"tonal\":%.3f,\"binaural\":%s,\"adaptive\":%s,\"pulse\":%s,"
    "\"modulation\":%s,\"envonly\":%s,\"sway\":%s,\"swayrate\":%.3f,"
    "\"env\":%s,\"sounds\":%s,"
    "\"daypart\":\"%s\",\"bank\":%s,\"sf2\":%s,\"lufs\":%.1f}",
    STATE_VERSION,
    atomic_load(&ctl.playing) ? "true" : "false",
    mode_name(atomic_load(&ctl.mode)),
    (double)atomic_load(&ctl.volume), (double)atomic_load(&ctl.intensity),
    (double)atomic_load(&ctl.brightness), (double)atomic_load(&ctl.tonal),
    atomic_load(&ctl.binaural) ? "true" : "false",
    atomic_load(&ctl.adaptive) ? "true" : "false",
    atomic_load(&ctl.pulse) ? "true" : "false",
    atomic_load(&ctl.modulation) ? "true" : "false",
    atomic_load(&ctl.envonly) ? "true" : "false",
    atomic_load(&ctl.sway) ? "true" : "false",
    (double)atomic_load(&ctl.swayrate),
    envs, snds, g_daypart,
    atomic_load(&ctl.bank_ready) ? "true" : "false",
    atomic_load(&ctl.sf2_ready) ? "true" : "false",
    (double)atomic_load(&ctl.lufs));
}

static void emit_state(void) {
  char buf[1024];
  state_json(buf, sizeof buf);
  printf("state %s\n", buf);
  fflush(stdout);
}

static void save_state(void) {
  if (!state_path[0]) return;
  char tmp[600];
  snprintf(tmp, sizeof tmp, "%s.tmp", state_path);
  FILE *f = fopen(tmp, "w");
  if (!f) return;
  char buf[1024];
  state_json(buf, sizeof buf);
  fputs(buf, f); fputc('\n', f);
  fclose(f);
  rename(tmp, state_path);
}

static bool json_get_num(const char *s, const char *key, float *out) {
  char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", key);
  const char *p = strstr(s, pat); if (!p) return false;
  *out = strtof(p + strlen(pat), NULL); return true;
}
static bool json_get_bool(const char *s, const char *key, int *out) {
  char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", key);
  const char *p = strstr(s, pat); if (!p) return false;
  *out = strncmp(p + strlen(pat), "true", 4) == 0; return true;
}
static bool json_get_str(const char *s, const char *key, char *out, size_t n) {
  char pat[64]; snprintf(pat, sizeof pat, "\"%s\":\"", key);
  const char *p = strstr(s, pat); if (!p) return false;
  p += strlen(pat);
  const char *e = strchr(p, '"'); if (!e) return false;
  size_t len = (size_t)(e - p); if (len >= n) len = n - 1;
  memcpy(out, p, len); out[len] = 0; return true;
}

static int g_state_migrated = 0;

static void load_state(void) {
  const char *xdg = getenv("XDG_STATE_HOME");
  const char *home = getenv("HOME");
  if (xdg && *xdg) snprintf(state_path, sizeof state_path, "%s/omanoise.json", xdg);
  else if (home) snprintf(state_path, sizeof state_path, "%s/.local/state/omanoise.json", home);
  else return;
  char dir[512]; snprintf(dir, sizeof dir, "%s", state_path);
  char *slash = strrchr(dir, '/'); if (slash) { *slash = 0; mkdir(dir, 0755); }

  FILE *f = fopen(state_path, "r");
  if (!f) return;
  char buf[2048]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
  float v; int b; char str[32];
  if (json_get_num(buf, "volume", &v)) atomic_store(&ctl.volume, clampf(v, 0, 1));
  if (json_get_num(buf, "intensity", &v)) atomic_store(&ctl.intensity, clampf(v, 0, 1));
  if (json_get_num(buf, "brightness", &v)) atomic_store(&ctl.brightness, clampf(v, 0, 1));
  if (json_get_num(buf, "tonal", &v)) atomic_store(&ctl.tonal, clampf(v, 0, 1));
  if (json_get_bool(buf, "binaural", &b)) atomic_store(&ctl.binaural, b);
  if (json_get_bool(buf, "adaptive", &b)) atomic_store(&ctl.adaptive, b);
  if (json_get_bool(buf, "pulse", &b)) atomic_store(&ctl.pulse, b);
  if (json_get_bool(buf, "modulation", &b)) atomic_store(&ctl.modulation, b);
  if (json_get_bool(buf, "envonly", &b)) atomic_store(&ctl.envonly, b);
  if (json_get_bool(buf, "sway", &b)) atomic_store(&ctl.sway, b);
  if (json_get_num(buf, "swayrate", &v)) atomic_store(&ctl.swayrate, clampf(v, 0, 1));
  // `sleep` and `ocean` are accepted for ever and resolve to relax and
  // environment (brain.c:MODE_ALIAS), which is the v3 -> v4 mode migration.
  if (json_get_str(buf, "mode", str, sizeof str)) { int m = mode_from_name(str); if (m >= 0) atomic_store(&ctl.mode, m); }

  // The environment levels live in a sub-object, so they are parsed from the
  // text after `"env":{` and cannot collide with any top-level key. Keys a v4
  // file does not have (stream, birds) keep the default of 0, which is the
  // v4 -> v5 migration.
  const char *ep = strstr(buf, "\"env\":{");
  if (ep) for (int i = 0; i < ENV_N; i++) {
    const char *q = strchr(ep, '}');
    char sub[512];
    size_t len = q ? (size_t)(q - ep) + 1 : strlen(ep);
    if (len >= sizeof sub) len = sizeof sub - 1;
    memcpy(sub, ep, len); sub[len] = 0;
    if (json_get_num(sub, ENV_NAME[i], &v)) atomic_store(&ctl.env[i], clampf(v, 0, 1));
  }

  // Migrations. A file with no version key was written by v2, where the 16 Hz
  // modulation and the pulse were on by default; both sit in bands v3 forbids,
  // so they are forced off once. v3 -> v4 needs no value changes beyond the
  // mode aliases above and the env defaults; v4 -> v5 adds the two new env
  // keys at 0. Either way the file is rewritten so the version key advances.
  float ver = 0.0f;
  if (!json_get_num(buf, "version", &ver)) ver = 2.0f;
  if (ver < 3.0f) {
    atomic_store(&ctl.modulation, 0);
    atomic_store(&ctl.pulse, 0);
  }
  if (ver < (float)STATE_VERSION) g_state_migrated = 1;
}

// -------------------------------------------------- time-of-day adaptation

static void update_daypart(void) {
  if (!atomic_load(&ctl.adaptive)) {
    g_daypart = "off";
    atomic_store(&ctl.adapt_bright, 0.0f);
    atomic_store(&ctl.adapt_root, 0.0f);
    return;
  }
  time_t now = time(NULL);
  struct tm tm; localtime_r(&now, &tm);
  float h = tm.tm_hour + tm.tm_min / 60.0f;
  float bright, root;
  if (h < 6.0f || h >= 22.0f) { g_daypart = "night";   bright = -0.35f; root = -5.0f; }
  else if (h < 11.0f)         { g_daypart = "morning"; bright =  0.15f; root =  0.0f; }
  else if (h < 17.0f)         { g_daypart = "day";     bright =  0.0f;  root =  0.0f; }
  else                        { g_daypart = "evening"; bright = -0.18f; root = -2.0f; }
  atomic_store(&ctl.adapt_bright, bright);
  atomic_store(&ctl.adapt_root, root);
}

// ------------------------------------------------------------ pipewire

struct app {
  struct pw_main_loop *loop;
  struct pw_stream *stream;
  struct spa_source *stdin_src;
  struct spa_source *tick_src;
  char linebuf[1024];
  size_t linelen;
  bool active;
  int tick_count;
};

static struct app app;

static void on_process(void *userdata) {
  static __thread int ftz_done = 0;
  if (!ftz_done) { denormals_off(); ftz_done = 1; }
  struct app *a = userdata;
  struct pw_buffer *b = pw_stream_dequeue_buffer(a->stream);
  if (!b) return;
  struct spa_buffer *buf = b->buffer;
  float *dst = buf->datas[0].data;
  if (!dst) { pw_stream_queue_buffer(a->stream, b); return; }
  uint32_t stride = sizeof(float) * CH;
  uint32_t n = buf->datas[0].maxsize / stride;
  if (b->requested) n = SPA_MIN((uint32_t)b->requested, n);
  engine_render(dst, n);
  buf->datas[0].chunk->offset = 0;
  buf->datas[0].chunk->stride = stride;
  buf->datas[0].chunk->size = n * stride;
  pw_stream_queue_buffer(a->stream, b);
}

// Set when a working stream is lost (the PipeWire daemon restarted or the
// client was disconnected). A pw_stream cannot reconnect, so the engine
// exits; Service.qml restarts it and resumes playback.
static bool g_server_lost = false;

static void on_state_changed(void *userdata, enum pw_stream_state old,
                             enum pw_stream_state state, const char *error) {
  struct app *a = userdata;
  if (state == PW_STREAM_STATE_ERROR) {
    fprintf(stderr, "omanoise: stream error: %s\n", error ? error : "?");
    fflush(stderr);
  }
  // A daemon restart drops the stream from PAUSED/STREAMING straight back to
  // UNCONNECTED (or ERROR) without any call of ours. Only an established
  // stream counts: an initial connect failure must not become a restart loop
  // while PipeWire is genuinely absent.
  if (old >= PW_STREAM_STATE_PAUSED &&
      (state == PW_STREAM_STATE_UNCONNECTED || state == PW_STREAM_STATE_ERROR)) {
    fprintf(stderr, "omanoise: lost the PipeWire server (%s), exiting to be restarted\n",
            error ? error : "connection closed");
    fflush(stderr);
    g_server_lost = true;
    pw_main_loop_quit(a->loop);
  }
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .process = on_process,
  .state_changed = on_state_changed,
};

static void set_active(bool on) {
  if (app.active == on) return;
  app.active = on;
  pw_stream_set_active(app.stream, on);
}

static void set_playing(bool on) {
  if (on) {
    set_active(true);
    atomic_store(&ctl.idle, 0);
  }
  atomic_store(&ctl.playing, on ? 1 : 0);
}

static bool parse01(const char *arg, float *out) {
  if (!arg) return false;
  char *end; float v = strtof(arg, &end);
  if (end == arg) return false;
  *out = clampf(v, 0.0f, 1.0f); return true;
}
static bool parse_bool(const char *arg, int *out) {
  if (!arg) return false;
  if (!strcmp(arg, "1") || !strcmp(arg, "on") || !strcmp(arg, "true")) { *out = 1; return true; }
  if (!strcmp(arg, "0") || !strcmp(arg, "off") || !strcmp(arg, "false")) { *out = 0; return true; }
  return false;
}

static void handle_command(char *line) {
  char *cmd = strtok(line, " \t\r\n");
  if (!cmd) return;
  char *arg = strtok(NULL, " \t\r\n");
  char *arg2 = strtok(NULL, " \t\r\n");
  float v; int b;
  bool changed = true;

  if (!strcmp(cmd, "play")) set_playing(true);
  else if (!strcmp(cmd, "pause") || !strcmp(cmd, "stop")) set_playing(false);
  else if (!strcmp(cmd, "toggle")) set_playing(!atomic_load(&ctl.playing));
  else if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) { pw_main_loop_quit(app.loop); return; }
  else if (!strcmp(cmd, "state")) changed = false;
  else if (!strcmp(cmd, "mode")) {
    int m = arg ? mode_from_name(arg) : -1;
    if (m < 0) { printf("error unknown mode\n"); fflush(stdout); return; }
    atomic_store(&ctl.mode, m);
  }
  else if (!strcmp(cmd, "volume") && parse01(arg, &v)) atomic_store(&ctl.volume, v);
  else if (!strcmp(cmd, "intensity") && parse01(arg, &v)) atomic_store(&ctl.intensity, v);
  else if (!strcmp(cmd, "brightness") && parse01(arg, &v)) atomic_store(&ctl.brightness, v);
  else if (!strcmp(cmd, "tonal") && parse01(arg, &v)) atomic_store(&ctl.tonal, v);
  else if (!strcmp(cmd, "swayrate") && parse01(arg, &v)) atomic_store(&ctl.swayrate, v);
  else if (!strcmp(cmd, "env")) {
    int e = arg ? env_from_name(arg) : -1;
    if (e < 0 || !parse01(arg2, &v)) {
      printf("error usage: env <ocean|rain|fire|wind|stream|birds> <0..1>\n");
      fflush(stdout);
      return;
    }
    atomic_store(&ctl.env[e], v);
  }
  else if (!strcmp(cmd, "binaural") && parse_bool(arg, &b)) atomic_store(&ctl.binaural, b);
  else if (!strcmp(cmd, "pulse") && parse_bool(arg, &b)) atomic_store(&ctl.pulse, b);
  else if (!strcmp(cmd, "modulation") && parse_bool(arg, &b)) atomic_store(&ctl.modulation, b);
  else if (!strcmp(cmd, "envonly") && parse_bool(arg, &b)) atomic_store(&ctl.envonly, b);
  else if (!strcmp(cmd, "sway") && parse_bool(arg, &b)) atomic_store(&ctl.sway, b);
  else if (!strcmp(cmd, "adaptive") && parse_bool(arg, &b)) { atomic_store(&ctl.adaptive, b); update_daypart(); }
  else { printf("error unknown command %s\n", cmd); fflush(stdout); return; }

  if (changed) save_state();
  emit_state();
}

static void on_stdin(void *userdata, int fd, uint32_t mask) {
  struct app *a = userdata;
  if (mask & (SPA_IO_HUP | SPA_IO_ERR)) { pw_main_loop_quit(a->loop); return; }
  char buf[512];
  ssize_t n = read(fd, buf, sizeof buf);
  if (n <= 0) { if (n == 0 || (errno != EAGAIN && errno != EINTR)) pw_main_loop_quit(a->loop); return; }
  for (ssize_t i = 0; i < n; i++) {
    char c = buf[i];
    if (c == '\n') {
      a->linebuf[a->linelen] = 0;
      handle_command(a->linebuf);
      a->linelen = 0;
    } else if (a->linelen < sizeof a->linebuf - 1) {
      a->linebuf[a->linelen++] = c;
    }
  }
}

static void on_tick(void *userdata, uint64_t expirations) {
  (void)expirations;
  struct app *a = userdata;
  static int bank_announced = 0, sf2_announced = 0;
  if (!bank_announced && atomic_load(&ctl.bank_ready)) { bank_announced = 1; emit_state(); }
  if (!sf2_announced && atomic_load(&ctl.sf2_ready)) { sf2_announced = 1; emit_state(); }
  if (!atomic_load(&ctl.playing) && atomic_load(&ctl.idle) && a->active) set_active(false);
  if (a->active) {
    printf("meter %.3f\n", (double)atomic_load(&ctl.meter));
    fflush(stdout);
  }
  if (++a->tick_count >= 600) {           // every minute
    a->tick_count = 0;
    const char *before = g_daypart;
    update_daypart();
    if (before != g_daypart) emit_state();
  }
}

static void on_signal(void *userdata, int sig) {
  (void)sig;
  struct app *a = userdata;
  pw_main_loop_quit(a->loop);
}

// ------------------------------------------- roughness / fluctuation metric
//
// PLAN v3 §5.2. The signal envelope (|x| -> one-pole 100 Hz -> decimated to
// 400 Hz) is the thing Zwicker's roughness and fluctuation-strength models are
// built on. FLUCT is the energy in 1-8 Hz (fluctuation strength peaks at 4 Hz)
// and ROUGH the energy in 15-200 Hz (roughness lives in 15-300 Hz), both
// relative to the mean envelope level, so the numbers are level-independent
// and comparable between modes. Ocean is the reference: it is the mode the
// user found calm, so every other mode has to stay within 2 dB of it.

#define ENV_RATE 400
#define ENV_DECIM (RATE / ENV_RATE)

// The plan specifies a single one-pole at 100 Hz before decimating to 400 Hz.
// A first run showed why that is not enough: |x| of the pad partials (130-460
// Hz) puts strong components at 260-920 Hz, a 6 dB/oct filter barely touches
// them, and they alias back into 0-200 Hz — every mode, Ocean included, then
// reported the same bogus 49.1 Hz "modulation line". Four cascaded one-poles
// at the same 100 Hz corner (24 dB/oct, -48 dB at 400 Hz) put the alias floor
// below the real envelope content; the band definitions are unchanged.
#define ENV_POLES 4

typedef struct {
  float *buf;
  int    cap, n;
  float  z[ENV_POLES], k;
  int    phase;
} envcap_t;

static int envcap_init(envcap_t *e, double seconds) {
  e->cap = (int)(seconds * ENV_RATE) + 64;
  e->buf = calloc((size_t)e->cap, sizeof(float));
  e->n = 0; e->phase = 0;
  for (int i = 0; i < ENV_POLES; i++) e->z[i] = 0.0f;
  e->k = 1.0f - expf(-TWOPI_F * 100.0f / (float)RATE);
  return e->buf ? 0 : -1;
}

static inline void envcap_push(envcap_t *e, float l, float r) {
  float a = fabsf(0.5f * (l + r));
  for (int i = 0; i < ENV_POLES; i++) { e->z[i] += e->k * (a - e->z[i]); a = e->z[i]; }
  if (++e->phase >= ENV_DECIM) {
    e->phase = 0;
    if (e->n < e->cap) e->buf[e->n++] = a;
  }
}

// Band-limited variance of the mean-removed envelope, in dB relative to the
// square of the mean (the envelope's "DC power").
static void envcap_stats(const envcap_t *e, double skip_s,
                         double *fluct_db, double *rough_db, double *peak_hz,
                         double *peak_rel_db) {
  *fluct_db = *rough_db = -99.0;
  *peak_hz = 0.0; *peak_rel_db = -99.0;
  int off = (int)(skip_s * ENV_RATE);
  int m = e->n - off;
  if (m < ENV_RATE * 4) return;
  int N = 1; while (N < m) N <<= 1;
  float *re = calloc((size_t)N, sizeof(float));
  float *im = calloc((size_t)N, sizeof(float));
  if (!re || !im) { free(re); free(im); return; }

  double mean = 0.0;
  for (int i = 0; i < m; i++) mean += e->buf[off + i];
  mean /= (double)m;
  double w2 = 0.0;
  for (int i = 0; i < m; i++) {
    double w = 0.5 - 0.5 * cos(2.0 * PI_F * (double)i / (double)(m - 1));
    re[i] = (float)((e->buf[off + i] - mean) * w);
    w2 += w * w;
  }
  fft_radix2(re, im, N, 0);

  // Parseval: sum_k |X_k|^2 = N * sum_i (x_i w_i)^2, so dividing by N*w2 turns
  // a bin sum straight into the variance that band carries.
  const double scale = 1.0 / ((double)N * w2);
  const double df = (double)ENV_RATE / (double)N;
  double fl = 0.0, ro = 0.0, pk = 0.0;
  int pk_k = 0;
  for (int k = 1; k < N / 2; k++) {
    double f = (double)k * df;
    if (f > 300.0) break;
    double p = 2.0 * ((double)re[k] * re[k] + (double)im[k] * im[k]) * scale;
    if (f >= 1.0 && f <= 8.0) fl += p;
    if (f >= 15.0 && f <= 200.0) ro += p;
    if (f >= 0.5 && p > pk) { pk = p; pk_k = k; }
  }
  // OMANOISE_ENVPROF=1 prints the whole envelope spectrum in 1/3-decade bands,
  // which is how the fluctuation sources were tracked down during tuning.
  if (getenv("OMANOISE_ENVPROF")) {
    fprintf(stderr, "envprof");
    for (double f1 = 0.125; f1 < 300.0; f1 *= 2.0) {
      double f2 = f1 * 2.0, acc = 0.0;
      int k1 = (int)(f1 / df), k2 = (int)(f2 / df);
      if (k1 < 1) k1 = 1;
      if (k2 > N / 2) k2 = N / 2;
      for (int k = k1; k < k2; k++)
        acc += 2.0 * ((double)re[k] * re[k] + (double)im[k] * im[k]) * scale;
      fprintf(stderr, " %.3g:%.1f", f1, 10.0 * log10((acc > 1e-20 ? acc : 1e-20) / (mean * mean + 1e-20)));
    }
    fprintf(stderr, "\n");
  }
  free(re); free(im);
  double ref = mean * mean;
  if (ref < 1e-20) ref = 1e-20;
  *fluct_db = 10.0 * log10((fl > 1e-20 ? fl : 1e-20) / ref);
  *rough_db = 10.0 * log10((ro > 1e-20 ? ro : 1e-20) / ref);
  *peak_hz = (double)pk_k * df;
  *peak_rel_db = 10.0 * log10((pk > 1e-20 ? pk : 1e-20) / ref);
}

// --------------------------------------------------------- offline render

// `--env ocean=1,rain=0.5` overrides the persisted environment levels for one
// render; anything not named keeps the level the state file gave it.
static void apply_env_override(const char *spec) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    char *eq = strchr(tok, '=');
    if (!eq) continue;
    *eq = 0;
    int e = env_from_name(tok);
    if (e >= 0) atomic_store(&ctl.env[e], clampf(strtof(eq + 1, NULL), 0.0f, 1.0f));
  }
}

// --set key=v,...: any 0..1 parameter or 0|1 flag, by its command name.
static void apply_set_override(const char *spec) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    char *eq = strchr(tok, '=');
    if (!eq) continue;
    *eq = 0;
    float v = clampf(strtof(eq + 1, NULL), 0.0f, 1.0f);
    int b = v >= 0.5f;
    if (!strcmp(tok, "volume")) atomic_store(&ctl.volume, v);
    else if (!strcmp(tok, "intensity")) atomic_store(&ctl.intensity, v);
    else if (!strcmp(tok, "brightness")) atomic_store(&ctl.brightness, v);
    else if (!strcmp(tok, "tonal")) atomic_store(&ctl.tonal, v);
    else if (!strcmp(tok, "swayrate")) atomic_store(&ctl.swayrate, v);
    else if (!strcmp(tok, "binaural")) atomic_store(&ctl.binaural, b);
    else if (!strcmp(tok, "adaptive")) atomic_store(&ctl.adaptive, b);
    else if (!strcmp(tok, "pulse")) atomic_store(&ctl.pulse, b);
    else if (!strcmp(tok, "modulation")) atomic_store(&ctl.modulation, b);
    else if (!strcmp(tok, "envonly")) atomic_store(&ctl.envonly, b);
    else if (!strcmp(tok, "sway")) atomic_store(&ctl.sway, b);
    else fprintf(stderr, "--set: unknown key %s\n", tok);
  }
}

static int render_file(const char *mode, const char *secs, const char *path,
                       float tail_s, int stats, int layers, const char *env_spec,
                       const char *set_spec) {
  int m = mode_from_name(mode);
  if (m < 0) { fprintf(stderr, "unknown mode\n"); return 1; }
  atomic_store(&ctl.mode, m);
  if (env_spec) apply_env_override(env_spec);
  if (set_spec) apply_set_override(set_spec);
  update_daypart();
  if (engine_init((uint64_t)time(NULL) * 0x2545F4914F6CDD1Dull ^ (uint64_t)getpid())) {
    fprintf(stderr, "engine init failed\n");
    return 1;
  }
  engine_set_layers(layers);
  denormals_off();
  double t0 = now_ms();
  if (bank_render(&g_bank, 0x0DDBA11ull) != 0) { fprintf(stderr, "bank render failed\n"); return 1; }
  engine_set_bank(&g_bank);
  double sf_ms = 0.0;
  g_sf2 = sf2_create(sf2_path(), &sf_ms);
  if (g_sf2) engine_set_sf2(g_sf2);
  sounds_load();
  fprintf(stderr, "sounds ready %.0f ms (bank %.0f ms %.1f MB, sf2 %.0f ms %s)\n",
          now_ms() - t0, g_bank.ms, g_bank.bytes / 1048576.0, sf_ms,
          g_sf2 ? "ok" : "MISSING");

  atomic_store(&ctl.playing, 1);
  FILE *f = fopen(path, "wb");
  if (!f) { perror("open"); return 1; }

  double total_s = atof(secs);
  uint32_t total = (uint32_t)(total_s * RATE);
  uint32_t tail = (uint32_t)(tail_s * RATE);
  float buf[1024 * CH];
  double peak = 0.0, dc = 0.0;
  uint64_t count = 0;
  double max_step = 0.0;
  float prevL = 0.0f, prevR = 0.0f;
  uint64_t done = 0;
  envcap_t env = { 0 };
  int have_env = stats && envcap_init(&env, total_s + tail_s + 1.0) == 0;

  for (int phase = 0; phase < 2; phase++) {
    if (phase == 1) {
      if (tail == 0) break;
      atomic_store(&ctl.playing, 0);
      total = tail;
    }
    while (total > 0) {
      uint32_t n = total > 1024 ? 1024 : total;
      engine_render(buf, n);
      fwrite(buf, sizeof(float), (size_t)n * CH, f);
      for (uint32_t i = 0; i < n; i++) {
        float l = buf[i * CH], r = buf[i * CH + 1];
        if (!isfinite(l) || !isfinite(r)) {
          fprintf(stderr, "NON-FINITE SAMPLE at frame %llu\n", (unsigned long long)(done + i));
          fclose(f);
          return 2;
        }
        double al = fabs(l), ar = fabs(r);
        if (al > peak) peak = al;
        if (ar > peak) peak = ar;
        dc += l + r;
        if (done + i > 5 * RATE) {
          double sl = fabs(l - prevL), sr = fabs(r - prevR);
          if (sl > max_step) max_step = sl;
          if (sr > max_step) max_step = sr;
        }
        prevL = l; prevR = r;
        if (have_env && phase == 0) envcap_push(&env, l, r);
        count += 2;
      }
      done += n;
      total -= n;
    }
  }
  fclose(f);
  if (stats) {
    float secs_f = (float)done / (float)RATE;
    char levs[128];
    size_t lp = 0;
    for (int i = 0; i < ENV_N; i++)
      lp += (size_t)snprintf(levs + lp, sizeof levs - lp, "%s%.2f", i ? "/" : "",
                             (double)atomic_load(&ctl.env[i]));
    fprintf(stderr, "stats mode=%s seconds=%.1f melody_events=%d rate=%.2f/min "
                    "peak=%.4f dc=%.6f max_step=%.4f lufs_st=%.1f trim=%.2f sf2=%s "
                    "env=%s\n",
            mode, secs_f, atomic_load(&ctl.melody_events),
            atomic_load(&ctl.melody_events) * 60.0 / (secs_f > 0 ? secs_f : 1),
            peak, count ? dc / (double)count : 0.0, max_step,
            (double)atomic_load(&ctl.lufs), (double)engine_trim_db(),
            g_sf2 ? "true" : "false", levs);
    // The segment log of every recorded slot that actually played: proof that
    // the scheduler is picking random positions and lengths, not looping. The
    // realtime thread only fills a fixed array; the printing happens here.
    for (int i = 0; i < ENV_N; i++) {
      int n = engine_seg_log(i, -1, NULL, NULL);
      if (n <= 0 || atomic_load(&ctl.env[i]) <= 0.0f) continue;
      fprintf(stderr, "segments %s n=%d", ENV_NAME[i], n);
      for (int k = 0; k < n; k++) {
        float st = 0.0f, ln = 0.0f;
        engine_seg_log(i, k, &st, &ln);
        fprintf(stderr, " %.2f+%.2f", (double)st, (double)ln);
      }
      fprintf(stderr, "\n");
    }
    if (have_env) {
      double fl, ro, ph, prd;
      envcap_stats(&env, 10.0, &fl, &ro, &ph, &prd);
      fprintf(stderr, "rough mode=%s FLUCT=%.2f dB ROUGH=%.2f dB peak_mod=%.2f Hz "
                      "peak_rel=%.2f dB\n", mode, fl, ro, ph, prd);
    }
    fflush(stderr);
  }
  free(env.buf);
  engine_free();
  sf2_destroy(g_sf2); g_sf2 = NULL;
  bank_free(&g_bank);
  sounds_free();
  return 0;
}

// ---------------------------------------------------- instrument auditing
// PLAN v3 §4 phase 1 / acceptance §3: every melody event kind that a mode can
// actually trigger gets one soft note rendered and measured here.

#define AUDIT_SEC 8
#define AUDIT_N   (AUDIT_SEC * RATE)

static void audit_note(sf2_t *s, int ch, int key, int vel, const char *dir) {
  static float note[AUDIT_N];
  static float env[AUDIT_N];
  sf2_audit_note(s, ch, key, vel, note, AUDIT_N);

  // 40 Hz one-pole envelope, as used for the roughness metric.
  float z = 0.0f, k = 1.0f - expf(-TWOPI_F * 40.0f / (float)RATE);
  double pk = 0.0, energy = 0.0;
  for (int i = 0; i < AUDIT_N; i++) {
    z += k * (fabsf(note[i]) - z);
    env[i] = z;
    if (z > pk) pk = z;
    energy += (double)note[i] * note[i];
  }
  double energy_db = 10.0 * log10(energy / AUDIT_N + 1e-30);
  int t10 = -1, t90 = -1, t60 = -1;
  for (int i = 0; i < AUDIT_N; i++) {
    if (t10 < 0 && env[i] >= 0.10 * pk) t10 = i;
    if (env[i] >= 0.90 * pk) { t90 = i; break; }
  }
  for (int i = (t90 < 0 ? 0 : t90); i < AUDIT_N; i++)
    if (env[i] < 0.001 * pk) { t60 = i; break; }

  // Spectral centroid over a 32768-sample Hann window a little after onset.
  const int NW = 32768;
  static float re[32768], im[32768];
  int off = (t90 > 0 ? t90 : 0) + RATE / 20;
  if (off + NW > AUDIT_N) off = AUDIT_N - NW;
  memset(im, 0, sizeof im);
  for (int i = 0; i < NW; i++) {
    float w = 0.5f - 0.5f * cosf(TWOPI_F * (float)i / (float)(NW - 1));
    re[i] = note[off + i] * w;
  }
  fft_radix2(re, im, NW, 0);
  double num = 0.0, den = 0.0;
  for (int kk = 1; kk < NW / 2; kk++) {
    double f = (double)kk * (double)RATE / (double)NW;
    if (f < 30.0 || f > 16000.0) continue;
    double p = (double)re[kk] * re[kk] + (double)im[kk] * im[kk];
    num += p * f; den += p;
  }
  double centroid = den > 0.0 ? num / den : 0.0;

  fprintf(stderr, "audit %-15s prog %3d key %3d vel %3d  peak %.5f  E %6.1f dB  t10 %6.1f ms  "
                  "t90 %6.1f ms  -60dB %6.0f ms  centroid %6.0f Hz\n",
          sf2_channel_name(ch), sf2_channel_program(ch), key, vel, pk, energy_db,
          t10 < 0 ? -1.0 : t10 * 1000.0 / RATE,
          t90 < 0 ? -1.0 : t90 * 1000.0 / RATE,
          t60 < 0 ? -1.0 : t60 * 1000.0 / RATE, centroid);

  if (dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/note_%s_k%d_v%d.f32", dir, sf2_channel_name(ch), key, vel);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(note, sizeof(float), AUDIT_N, f); fclose(f); }
  }
}

static int render_bank_cmd(const char *dir) {
  double t0 = now_ms();
  if (bank_render(&g_bank, 0x0DDBA11ull) != 0) { fprintf(stderr, "bank render failed\n"); return 1; }
  fprintf(stderr, "bank ready %.0f ms (%.1f MB)\n", g_bank.ms, g_bank.bytes / 1048576.0);
  mkdir(dir, 0755);
  int rc = bank_dump(&g_bank, dir);

  double sf_ms = 0.0;
  g_sf2 = sf2_create(sf2_path(), &sf_ms);
  fprintf(stderr, "soundfont %s %.0f ms (%s)\n", g_sf2 ? "ready" : "MISSING", sf_ms, sf2_path());
  if (g_sf2) {
    // The registers each instrument is actually played in, bottom and top,
    // at the softest and loudest velocity its mode can produce.
    static const struct { int ch, lo, hi, vlo, vhi; } AUD[] = {
      { SF2_PIANO,  36, 72, 34, 62 },
      { SF2_HARP,   48, 72, 28, 52 },
      { SF2_MALLET, 48, 78, 27, 50 },
      { SF2_BOX,    72, 84, 12, 23 },
    };
    for (unsigned i = 0; i < sizeof AUD / sizeof AUD[0]; i++) {
      audit_note(g_sf2, AUD[i].ch, AUD[i].lo, AUD[i].vlo, dir);
      audit_note(g_sf2, AUD[i].ch, AUD[i].hi, AUD[i].vhi, dir);
    }
  }
  fprintf(stderr, "sounds ready %.0f ms total\n", now_ms() - t0);
  sf2_destroy(g_sf2); g_sf2 = NULL;
  bank_free(&g_bank);
  return rc ? 1 : 0;
}

// -------------------------------------------------------------------- main

int main(int argc, char *argv[]) {
  uint64_t seed = (uint64_t)time(NULL) * 0x2545F4914F6CDD1Dull ^ (uint64_t)getpid();
  dsp_tables_init();
  load_state();

  if (argc >= 3 && !strcmp(argv[1], "--render-bank")) return render_bank_cmd(argv[2]);

  if (argc >= 5 && !strcmp(argv[1], "--render")) {
    float tail = 0.0f;
    int stats = 0, layers = LAYER_ALL;
    const char *env_spec = NULL;
    const char *set_spec = NULL;
    for (int i = 5; i < argc; i++) {
      if (!strcmp(argv[i], "--tail") && i + 1 < argc) tail = (float)atof(argv[++i]);
      else if (!strcmp(argv[i], "--stats")) stats = 1;
      else if (!strcmp(argv[i], "--layers") && i + 1 < argc) layers = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--env") && i + 1 < argc) env_spec = argv[++i];
      else if (!strcmp(argv[i], "--set") && i + 1 < argc) set_spec = argv[++i];
    }
    return render_file(argv[2], argv[3], argv[4], tail, stats, layers, env_spec, set_spec);
  }

  pw_init(&argc, &argv);
  update_daypart();
  if (engine_init(seed)) { fprintf(stderr, "omanoise: engine init failed\n"); return 1; }
  bank_start(seed ^ 0xB00Cull);
  if (g_state_migrated) save_state();       // write the v3 defaults back once

  app.loop = pw_main_loop_new(NULL);
  if (!app.loop) { fprintf(stderr, "omanoise: cannot create loop\n"); return 1; }
  struct pw_loop *loop = pw_main_loop_get_loop(app.loop);

  pw_loop_add_signal(loop, SIGINT, on_signal, &app);
  pw_loop_add_signal(loop, SIGTERM, on_signal, &app);

  struct pw_properties *props = pw_properties_new(
    PW_KEY_MEDIA_TYPE, "Audio",
    PW_KEY_MEDIA_CATEGORY, "Playback",
    PW_KEY_MEDIA_ROLE, "Music",
    PW_KEY_APP_NAME, "Omanoise",
    PW_KEY_APP_ICON_NAME, "audio-headphones",
    PW_KEY_NODE_NAME, "omanoise",
    PW_KEY_MEDIA_NAME, "Omanoise soundscape",
    PW_KEY_NODE_LATENCY, "2048/48000",
    NULL);

  app.stream = pw_stream_new_simple(loop, "omanoise", props, &stream_events, &app);
  if (!app.stream) { fprintf(stderr, "omanoise: cannot create stream\n"); return 1; }

  uint8_t pod[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(pod, sizeof pod);
  const struct spa_pod *params[1];
  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
    &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32, .channels = CH, .rate = RATE));

  int res = pw_stream_connect(app.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS |
    PW_STREAM_FLAG_INACTIVE, params, 1);
  if (res < 0) { fprintf(stderr, "omanoise: connect failed: %s\n", spa_strerror(res)); return 1; }
  app.active = false;

  app.stdin_src = pw_loop_add_io(loop, STDIN_FILENO, SPA_IO_IN | SPA_IO_HUP | SPA_IO_ERR, false, on_stdin, &app);
  app.tick_src = pw_loop_add_timer(loop, on_tick, &app);
  struct timespec first = { .tv_sec = 0, .tv_nsec = 100 * 1000000 };
  struct timespec every = { .tv_sec = 0, .tv_nsec = 100 * 1000000 };
  pw_loop_update_timer(loop, app.tick_src, &first, &every, false);

  bool autoplay = false;
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--play")) autoplay = true;
  if (autoplay) set_playing(true);

  emit_state();
  pw_main_loop_run(app.loop);

  pw_stream_destroy(app.stream);
  pw_main_loop_destroy(app.loop);
  pw_deinit();
  if (g_bank_started && g_bank_thread) pthread_join(g_bank_thread, NULL);
  engine_free();
  sf2_destroy(g_sf2);
  bank_free(&g_bank);
  sounds_free();
  return g_server_lost ? 2 : 0;
}
