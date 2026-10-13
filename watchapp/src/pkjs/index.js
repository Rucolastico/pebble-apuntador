var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var firma = require('./firma');

// Debe coincidir con los enum de src/c/watchapp.c
var ACCIONES = ['next', 'prev', 'black', 'start', 'start_here', 'esc'];
var PERFILES = ['estandar', 'onlyoffice'];
var ESTADO_FALLO = 0, ESTADO_OK = 1, ESTADO_SIN_TOKEN = 2;
var TIEMPO_PING = 2000, TIEMPO_TECLA = 2500;

var servidor = null;   // "ip:puerto" que respondió al último /ping
var sondeo = null;     // callbacks esperando al sondeo en curso

function ajustes() {
  var s = {};
  try { s = JSON.parse(localStorage.getItem('clay-settings')) || {}; } catch (e) {}
  var puerto = parseInt(s.Port, 10) || 8765;
  var lista = [s.Tailscale || '']
    .concat(String(s.Locales || '').split(/[\s,;]+/));
  var dirs = [];
  lista.forEach(function(d) {
    d = String(d || '').trim();
    if (!d) { return; }
    if (!/:\d+$/.test(d)) { d += ':' + puerto; }
    if (dirs.indexOf(d) < 0) { dirs.push(d); }
  });
  var perfil = PERFILES.indexOf(s.Perfil) >= 0 ? s.Perfil : PERFILES[0];
  return { token: String(s.Token || '').trim(), dirs: dirs, perfil: perfil };
}

// XHR con timeout propio: no todos los entornos de PebbleKit JS respetan xhr.timeout.
// Firma cada petición con el token (HMAC): el token nunca viaja por la red.
function peticion(metodo, dir, ruta, token, ms, cb) {
  var xhr = new XMLHttpRequest();
  var hecho = false;
  function fin(ok) {
    if (hecho) { return; }
    hecho = true;
    clearTimeout(reloj);
    cb(ok);
  }
  var reloj = setTimeout(function() { try { xhr.abort(); } catch (e) {} fin(false); }, ms);
  xhr.onload = function() { fin(xhr.status === 200); };
  xhr.onerror = function() { fin(false); };
  try {
    xhr.open(metodo, 'http://' + dir + ruta, true);
    var cab = firma.cabeceras(metodo, ruta, token);
    for (var k in cab) { xhr.setRequestHeader(k, cab[k]); }
    xhr.send();
  } catch (e) { fin(false); }
}

// Prueba todas las direcciones a la vez pero respeta el orden: gana la primera
// de la lista que responda, sin esperar a las de detrás.
function sondear(cb) {
  if (sondeo) { sondeo.push(cb); return; }
  sondeo = [cb];
  var a = ajustes();
  function terminar(dir, estado) {
    servidor = dir;
    var cbs = sondeo;
    sondeo = null;
    console.log('Sondeo: ' + (dir || 'ninguna') + ' (estado ' + estado + ')');
    cbs.forEach(function(f) { f(dir, estado); });
  }
  if (!a.token) { terminar(null, ESTADO_SIN_TOKEN); return; }
  if (!a.dirs.length) { terminar(null, ESTADO_FALLO); return; }
  var res = a.dirs.map(function() { return null; });
  var acabado = false;
  function revisar() {
    if (acabado) { return; }
    for (var i = 0; i < res.length; i++) {
      if (res[i] === null) { return; }       // una anterior aún puede ganar
      if (res[i] === true) { acabado = true; terminar(a.dirs[i], ESTADO_OK); return; }
    }
    acabado = true;
    terminar(null, ESTADO_FALLO);
  }
  a.dirs.forEach(function(dir, i) {
    peticion('GET', dir, '/ping', a.token, TIEMPO_PING, function(ok) {
      res[i] = ok;
      revisar();
    });
  });
}

function enviar(msg) {
  Pebble.sendAppMessage(msg, null, function(e) {
    console.log('Fallo al enviar al reloj: ' + JSON.stringify(e));
  });
}

function informarEstado() {
  sondear(function(dir, estado) {
    enviar({ Status: estado, Host: dir ? dir.replace(/:\d+$/, '') : '' });
  });
}

function pulsar(accion, reintento) {
  var a = ajustes();
  if (!servidor) {
    if (reintento) { enviar({ Result: 0, Status: ESTADO_FALLO, Host: '' }); return; }
    sondear(function(dir, estado) {
      if (dir) { pulsar(accion, true); }
      else { enviar({ Result: 0, Status: estado, Host: '' }); }
    });
    return;
  }
  peticion('POST', servidor, '/key/' + a.perfil + '/' + accion, a.token, TIEMPO_TECLA, function(ok) {
    if (ok) { enviar({ Result: 1 }); return; }
    // La red pudo cambiar (p. ej. Tailscale caído): vuelve a sondear y reintenta una vez.
    servidor = null;
    if (reintento) { enviar({ Result: 0, Status: ESTADO_FALLO, Host: '' }); }
    else { pulsar(accion, true); }
  });
}

Pebble.addEventListener('ready', function() {
  informarEstado();
});

Pebble.addEventListener('appmessage', function(e) {
  var p = e.payload;
  if (p.Action !== undefined) {
    var accion = ACCIONES[p.Action];
    if (accion) { pulsar(accion, false); }
  }
});

// La página de ajustes sale en el idioma del reloj (o del teléfono si no se sabe).
function idiomaEspanol() {
  var idioma = '';
  try { idioma = Pebble.getActiveWatchInfo().language || ''; } catch (e) {}
  if (!idioma && typeof navigator !== 'undefined') { idioma = navigator.language || ''; }
  return /^es/i.test(idioma);
}

var clay = null;
function obtenerClay() {
  if (!clay) { clay = new Clay(clayConfig(idiomaEspanol()), null, { autoHandleEvents: false }); }
  return clay;
}

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(obtenerClay().generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) { return; }
  var s = obtenerClay().getSettings(e.response, false);   // también lo guarda en localStorage
  function num(v, porDefecto) {
    var n = parseInt(v && v.value !== undefined ? v.value : v, 10);
    return isNaN(n) ? porDefecto : n;
  }
  // Al reloj solo le interesan los avisos; el token se queda en el teléfono.
  enviar({ Vibe1: num(s.Vibe1, 40), Vibe2: num(s.Vibe2, 50), Perfil: Math.max(0, PERFILES.indexOf(ajustes().perfil)) });
  servidor = null;
  informarEstado();
});
