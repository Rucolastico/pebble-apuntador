#include <pebble.h>

// Debe coincidir con ACCIONES en src/pkjs/index.js
enum { ACC_NEXT, ACC_PREV, ACC_BLACK, ACC_START, ACC_START_HERE, ACC_ESC };
// Debe coincidir con PERFILES en src/pkjs/index.js
enum { PERFIL_ESTANDAR, PERFIL_ONLYOFFICE };
enum { ESTADO_FALLO, ESTADO_OK, ESTADO_SIN_TOKEN };

#define P_INICIO 1      // time_t del inicio de la sesión (0 = sin empezar)
#define P_ULTIMA 2      // time_t de la última acción
#define P_VIBE1 3
#define P_VIBE2 4
#define P_PERFIL 5
#define SESION_CADUCA (3 * 60 * 60)   // tras 3 h sin acciones, sesión nueva
#define ESPERA_TELEFONO 12000
#define ESPERA_RESULTADO 8000
#define LONG_MS 600

#define GRIS PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define APAGADO PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)
#define AVISO PBL_IF_COLOR_ELSE(GColorSunsetOrange, GColorWhite)
#define BIEN PBL_IF_COLOR_ELSE(GColorMalachite, GColorWhite)
#define ACENTO PBL_IF_COLOR_ELSE(GColorTiffanyBlue, GColorWhite)

// ---------------------------------------------------------------- textos

typedef struct {
  // Nombres de acción; [perfil] donde el perfil cambia la tecla.
  const char *siguiente, *anterior, *negro[2], *iniciar, *desde_aqui[2], *esc;
  // Ayudas junto a los botones: "nombre\nlargo: …".
  const char *ayuda_arriba[2], *ayuda_select[2], *ayuda_abajo;
  const char *conectando, *sin_conexion, *falta_token, *sin_telefono;
  const char *sin_respuesta, *ocupado, *error, *crono_cero;
} Textos;

static const Textos ES = {
  "Siguiente", "Anterior", { "Pantalla negra", "Tab" }, "Iniciar", { "Desde aquí", "Primera diapo" }, "Esc",
  { "Ant.\nlargo: aquí", "Ant.\nlargo: 1.ª" }, { "Negro\nlargo: salir", "Tab\nlargo: salir" }, "Sig.\nlargo: inicio",
  "Conectando…", "Sin conexión", "Falta el token", "Sin teléfono",
  "sin respuesta", "ocupado", "error", "Crono a cero",
};

static const Textos EN = {
  "Next", "Previous", { "Black screen", "Tab" }, "Start", { "From here", "First slide" }, "Esc",
  { "Prev\nhold: here", "Prev\nhold: 1st" }, { "Black\nhold: exit", "Tab\nhold: exit" }, "Next\nhold: start",
  "Connecting…", "No connection", "Token missing", "No phone",
  "no response", "busy", "error", "Timer reset",
};

static const Textos *T = &EN;

// ---------------------------------------------------------------- estado

static Window *s_ventana;
static TextLayer *s_estado, *s_crono, *s_accion, *s_resultado;
static TextLayer *s_ayuda[3];
static char s_texto_ayuda[3][24];
static bool s_pequena;

static char s_host[32];
static char s_texto_estado[40];
static char s_texto_crono[16];

static time_t s_inicio, s_ultima;
static int s_vibe1 = 40, s_vibe2 = 50;
static int s_perfil = PERFIL_ESTANDAR;
static int s_transcurrido_previo = -1;
static bool s_conexion_anunciada;
static AppTimer *s_timer_telefono, *s_timer_resultado;

static const char *nombre_accion(int accion) {
  switch (accion) {
    case ACC_NEXT: return T->siguiente;
    case ACC_PREV: return T->anterior;
    case ACC_BLACK: return T->negro[s_perfil];
    case ACC_START: return T->iniciar;
    case ACC_START_HERE: return T->desde_aqui[s_perfil];
    default: return T->esc;
  }
}

// ---------------------------------------------------------------- vibraciones

static void vibrar(const uint32_t *seg, uint32_t n) {
  VibePattern p = { .durations = seg, .num_segments = n };
  vibes_enqueue_custom_pattern(p);
}

static void anunciar_conexion(bool ok) {
  if (s_conexion_anunciada) return;
  s_conexion_anunciada = true;
  if (s_timer_telefono) { app_timer_cancel(s_timer_telefono); s_timer_telefono = NULL; }
  if (ok) {
    vibes_short_pulse();
  } else {
    static const uint32_t dos_largas[] = { 500, 250, 500 };
    vibrar(dos_largas, ARRAY_LENGTH(dos_largas));
  }
}

// ---------------------------------------------------------------- cronómetro

static void pintar_crono(void) {
  int t = s_inicio ? (int)(time(NULL) - s_inicio) : 0;
  if (t < 0) t = 0;
#ifdef CAPTURAS
  t = 12 * 60 + 34;   // el reloj del emulador se ajusta al arrancar: valor fijo
#endif
  if (t >= 3600) {
    snprintf(s_texto_crono, sizeof(s_texto_crono), "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    text_layer_set_font(s_crono, fonts_get_system_font(
        s_pequena ? FONT_KEY_LECO_20_BOLD_NUMBERS : FONT_KEY_LECO_32_BOLD_NUMBERS));
  } else {
    snprintf(s_texto_crono, sizeof(s_texto_crono), "%02d:%02d", t / 60, t % 60);
    text_layer_set_font(s_crono, fonts_get_system_font(
        s_pequena ? FONT_KEY_LECO_32_BOLD_NUMBERS : FONT_KEY_LECO_42_NUMBERS));
  }
  text_layer_set_text(s_crono, s_texto_crono);
  text_layer_set_text_color(s_crono, s_inicio ? GColorWhite : APAGADO);

  if (!s_inicio) return;
  // Avisa solo al cruzar el umbral, no al reabrir la app con la sesión ya pasada de él.
  if (s_transcurrido_previo >= 0) {
    if (s_vibe1 > 0 && s_transcurrido_previo < s_vibe1 * 60 && t >= s_vibe1 * 60) {
      static const uint32_t dos[] = { 300, 200, 300 };
      vibrar(dos, ARRAY_LENGTH(dos));
    }
    if (s_vibe2 > 0 && s_transcurrido_previo < s_vibe2 * 60 && t >= s_vibe2 * 60) {
      static const uint32_t tres[] = { 600, 200, 600, 200, 600 };
      vibrar(tres, ARRAY_LENGTH(tres));
    }
  }
  s_transcurrido_previo = t;
}

static void tick(struct tm *tm, TimeUnits cambios) {
  pintar_crono();
}

static void marcar_actividad(void) {
  time_t ahora = time(NULL);
  if (!s_inicio) {
    s_inicio = ahora;
    s_transcurrido_previo = 0;
    persist_write_int(P_INICIO, (int32_t)s_inicio);
  }
  // Escribir en flash a cada pulsación desgasta: basta con hacerlo cada minuto.
  if (ahora - s_ultima >= 60) {
    s_ultima = ahora;
    persist_write_int(P_ULTIMA, (int32_t)s_ultima);
  }
  pintar_crono();
}

// ---------------------------------------------------------------- estado y resultado

static void pintar_estado(int estado) {
  switch (estado) {
    case ESTADO_OK:
      snprintf(s_texto_estado, sizeof(s_texto_estado), "%s", s_host);
      text_layer_set_text_color(s_estado, ACENTO);
      break;
    case ESTADO_SIN_TOKEN:
      snprintf(s_texto_estado, sizeof(s_texto_estado), "%s", T->falta_token);
      text_layer_set_text_color(s_estado, AVISO);
      break;
    default:
      snprintf(s_texto_estado, sizeof(s_texto_estado), "%s", T->sin_conexion);
      text_layer_set_text_color(s_estado, AVISO);
  }
  text_layer_set_text(s_estado, s_texto_estado);
}

static void mostrar_resultado(const char *texto, GColor color) {
  text_layer_set_text(s_resultado, texto);
  text_layer_set_text_color(s_resultado, color);
}

// En pantallas pequeñas solo cabe la primera línea de cada ayuda.
static void pintar_ayudas(void) {
  if (!s_ayuda[0]) return;
  const char *textos[] = { T->ayuda_arriba[s_perfil], T->ayuda_select[s_perfil], T->ayuda_abajo };
  for (int i = 0; i < 3; i++) {
    snprintf(s_texto_ayuda[i], sizeof(s_texto_ayuda[i]), "%s", textos[i]);
    if (s_pequena) {
      char *salto = strchr(s_texto_ayuda[i], '\n');
      if (salto) *salto = '\0';
    }
    text_layer_set_text(s_ayuda[i], s_texto_ayuda[i]);
  }
}

static void sin_telefono(void *ctx) {
  s_timer_telefono = NULL;
  snprintf(s_texto_estado, sizeof(s_texto_estado), "%s", T->sin_telefono);
  text_layer_set_text(s_estado, s_texto_estado);
  text_layer_set_text_color(s_estado, AVISO);
  anunciar_conexion(false);
}

static void sin_resultado(void *ctx) {
  s_timer_resultado = NULL;
  mostrar_resultado(T->sin_respuesta, AVISO);
}

// ---------------------------------------------------------------- AppMessage

static void recibido(DictionaryIterator *it, void *ctx) {
  Tuple *t;
  if ((t = dict_find(it, MESSAGE_KEY_Host))) {
    snprintf(s_host, sizeof(s_host), "%s", t->value->cstring);
  }
#ifndef CAPTURAS
  if ((t = dict_find(it, MESSAGE_KEY_Status))) {
    int estado = t->value->int32;
    pintar_estado(estado);
    anunciar_conexion(estado == ESTADO_OK);
  }
#endif
  if ((t = dict_find(it, MESSAGE_KEY_Result))) {
    if (s_timer_resultado) { app_timer_cancel(s_timer_resultado); s_timer_resultado = NULL; }
    if (t->value->int32) {
      mostrar_resultado("OK", BIEN);
    } else {
      mostrar_resultado(T->error, AVISO);
    }
  }
  if ((t = dict_find(it, MESSAGE_KEY_Perfil))) {
    s_perfil = t->value->int32 == PERFIL_ONLYOFFICE ? PERFIL_ONLYOFFICE : PERFIL_ESTANDAR;
    persist_write_int(P_PERFIL, s_perfil);
    pintar_ayudas();
  }
  Tuple *v1 = dict_find(it, MESSAGE_KEY_Vibe1);
  Tuple *v2 = dict_find(it, MESSAGE_KEY_Vibe2);
  if (v1) { s_vibe1 = v1->value->int32; persist_write_int(P_VIBE1, s_vibe1); }
  if (v2) { s_vibe2 = v2->value->int32; persist_write_int(P_VIBE2, s_vibe2); }
  if (v1 || v2) {
    // Tras guardar ajustes llega un nuevo Status: vuelve a avisar de la conexión.
    s_conexion_anunciada = false;
    s_transcurrido_previo = s_inicio ? (int)(time(NULL) - s_inicio) : -1;
  }
}

static void enviar_accion(int accion) {
  text_layer_set_text(s_accion, nombre_accion(accion));
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) {
    mostrar_resultado(T->ocupado, AVISO);
    return;
  }
  dict_write_uint8(it, MESSAGE_KEY_Action, accion);
  if (app_message_outbox_send() != APP_MSG_OK) {
    mostrar_resultado(T->error, AVISO);
    return;
  }
  mostrar_resultado("...", GRIS);
  if (s_timer_resultado) app_timer_cancel(s_timer_resultado);
  s_timer_resultado = app_timer_register(ESPERA_RESULTADO, sin_resultado, NULL);
  marcar_actividad();
}

static void fallo_envio(DictionaryIterator *it, AppMessageResult motivo, void *ctx) {
  if (s_timer_resultado) { app_timer_cancel(s_timer_resultado); s_timer_resultado = NULL; }
  mostrar_resultado(T->sin_telefono, AVISO);
}

// ---------------------------------------------------------------- botones

static void clic_arriba(ClickRecognizerRef r, void *ctx) { enviar_accion(ACC_PREV); }
static void clic_abajo(ClickRecognizerRef r, void *ctx) { enviar_accion(ACC_NEXT); }
static void clic_select(ClickRecognizerRef r, void *ctx) { enviar_accion(ACC_BLACK); }
static void largo_arriba(ClickRecognizerRef r, void *ctx) { enviar_accion(ACC_START_HERE); }
static void largo_abajo(ClickRecognizerRef r, void *ctx) { enviar_accion(ACC_START); }
static void largo_select(ClickRecognizerRef r, void *ctx) { enviar_accion(ACC_ESC); }

// Atrás no admite pulsación larga (el sistema la usa para salir), así que el
// reinicio va con doble clic y el clic sencillo conserva su función de salir.
static void clic_atras(ClickRecognizerRef r, void *ctx) { window_stack_pop(true); }

static void doble_atras(ClickRecognizerRef r, void *ctx) {
  s_inicio = 0;
  s_transcurrido_previo = -1;
  persist_write_int(P_INICIO, 0);
  vibes_short_pulse();
  text_layer_set_text(s_accion, T->crono_cero);
  mostrar_resultado("", GRIS);
  pintar_crono();
}

static void config_clics(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_BACK, clic_atras);
  window_multi_click_subscribe(BUTTON_ID_BACK, 2, 2, 0, true, doble_atras);
  window_single_click_subscribe(BUTTON_ID_UP, clic_arriba);
  window_single_click_subscribe(BUTTON_ID_DOWN, clic_abajo);
  window_single_click_subscribe(BUTTON_ID_SELECT, clic_select);
  window_long_click_subscribe(BUTTON_ID_UP, LONG_MS, largo_arriba, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, LONG_MS, largo_abajo, NULL);
  window_long_click_subscribe(BUTTON_ID_SELECT, LONG_MS, largo_select, NULL);
}

// ---------------------------------------------------------------- ventana

static TextLayer *crear_texto(Layer *raiz, GRect marco, const char *fuente, GTextAlignment alin, GColor color) {
  TextLayer *t = text_layer_create(marco);
  text_layer_set_background_color(t, GColorClear);
  text_layer_set_text_color(t, color);
  text_layer_set_font(t, fonts_get_system_font(fuente));
  text_layer_set_text_alignment(t, alin);
  text_layer_set_overflow_mode(t, GTextOverflowModeTrailingEllipsis);
  layer_add_child(raiz, text_layer_get_layer(t));
  return t;
}

static void cargar_ventana(Window *v) {
  Layer *raiz = window_get_root_layer(v);
  GRect b = layer_get_bounds(raiz);
  int w = b.size.w, h = b.size.h;
  s_pequena = w < 180;

#if defined(PBL_ROUND)
  // En pantallas redondas los bordes se recortan: todo centrado y sin ayudas.
  int margen = w / 8;
  GTextAlignment alin = GTextAlignmentCenter;
  int x = margen, ancho = w - 2 * margen, centro_x = 0, ancho_crono = w;
  s_ayuda[0] = s_ayuda[1] = s_ayuda[2] = NULL;
  int y_estado = h / 8, y_accion = h * 5 / 8;
#else
  int ancho_ayuda = s_pequena ? 44 : w / 3;
  GTextAlignment alin = GTextAlignmentLeft;
  int x = 4, ancho = w - ancho_ayuda - 8, centro_x = 0, ancho_crono = w - ancho_ayuda;
  int y_estado = 2, y_accion = h - 62;

  // Ayudas junto a cada botón: arriba, centro y abajo del borde derecho.
  int alto_ayuda = s_pequena ? 18 : 36;
  int ys[] = { h / 6 - alto_ayuda / 2, h / 2 - alto_ayuda / 2, h * 5 / 6 - alto_ayuda / 2 };
  for (int i = 0; i < 3; i++) {
    s_ayuda[i] = crear_texto(raiz, GRect(w - ancho_ayuda - 2, ys[i], ancho_ayuda, alto_ayuda),
                             FONT_KEY_GOTHIC_14, GTextAlignmentRight, GRIS);
  }
  pintar_ayudas();
#endif

  s_estado = crear_texto(raiz, GRect(x, y_estado, ancho, 22), FONT_KEY_GOTHIC_18_BOLD, alin, GRIS);
  snprintf(s_texto_estado, sizeof(s_texto_estado), "%s", T->conectando);
  text_layer_set_text(s_estado, s_texto_estado);

  int alto_crono = s_pequena ? 40 : 50;
  s_crono = crear_texto(raiz, GRect(centro_x, h / 2 - alto_crono / 2 - 6, ancho_crono, alto_crono),
                        FONT_KEY_LECO_42_NUMBERS, GTextAlignmentCenter, GColorWhite);

  const char *fuente_accion = s_pequena ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_24_BOLD;
  const char *fuente_resultado = s_pequena ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_24;
  int alto_linea = s_pequena ? 22 : 28;
  if (s_pequena) y_accion = h - 2 * alto_linea - 2;
  s_accion = crear_texto(raiz, GRect(x, y_accion, ancho, alto_linea), fuente_accion, alin, GColorWhite);
  s_resultado = crear_texto(raiz, GRect(x, y_accion + alto_linea - 2, ancho, alto_linea),
                            fuente_resultado, alin, GRIS);

  pintar_crono();
}

static void descargar_ventana(Window *v) {
  for (int i = 0; i < 3; i++) {
    if (s_ayuda[i]) text_layer_destroy(s_ayuda[i]);
    s_ayuda[i] = NULL;
  }
  text_layer_destroy(s_estado);
  text_layer_destroy(s_crono);
  text_layer_destroy(s_accion);
  text_layer_destroy(s_resultado);
}

static void iniciar(void) {
  if (strncmp(i18n_get_system_locale(), "es", 2) == 0) T = &ES;
  if (persist_exists(P_VIBE1)) s_vibe1 = persist_read_int(P_VIBE1);
  if (persist_exists(P_VIBE2)) s_vibe2 = persist_read_int(P_VIBE2);
  if (persist_exists(P_PERFIL)) s_perfil = persist_read_int(P_PERFIL) == PERFIL_ONLYOFFICE;
  s_inicio = persist_read_int(P_INICIO);
  s_ultima = persist_read_int(P_ULTIMA);
  time_t ahora = time(NULL);
  if (s_inicio && (ahora - (s_ultima > s_inicio ? s_ultima : s_inicio) > SESION_CADUCA)) {
    s_inicio = 0;   // sesión vieja: empieza de cero en la primera acción
    persist_write_int(P_INICIO, 0);
  }
  if (s_inicio) s_transcurrido_previo = (int)(ahora - s_inicio);

  s_ventana = window_create();
  window_set_background_color(s_ventana, GColorBlack);
  window_set_click_config_provider(s_ventana, config_clics);
  window_set_window_handlers(s_ventana, (WindowHandlers) {
    .load = cargar_ventana,
    .unload = descargar_ventana,
  });
  window_stack_push(s_ventana, true);

  app_message_register_inbox_received(recibido);
  app_message_register_outbox_failed(fallo_envio);
  app_message_open(256, 64);

  tick_timer_service_subscribe(SECOND_UNIT, tick);
#ifdef CAPTURAS
  // Estado de ejemplo para las capturas de la tienda (herramientas/capturas.sh).
  snprintf(s_host, sizeof(s_host), "192.168.1.20");
  pintar_estado(ESTADO_OK);
  s_inicio = time(NULL) - (12 * 60 + 34);
  s_transcurrido_previo = -1;
  text_layer_set_text(s_accion, nombre_accion(ACC_NEXT));
  mostrar_resultado("OK", BIEN);
  pintar_crono();
#else
  s_timer_telefono = app_timer_register(ESPERA_TELEFONO, sin_telefono, NULL);
#endif
}

static void terminar(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_ventana);
}

int main(void) {
  iniciar();
  app_event_loop();
  terminar();
}
