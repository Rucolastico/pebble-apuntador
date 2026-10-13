// Script de KWin: devuelve por D-Bus la ventana activa y el tamaño del escritorio.
// apuntador-servidor.py lo carga y escucha la llamada con dbus-monitor.
var w = workspace.activeWindow;
var v = workspace.virtualScreenGeometry;
var info = { escritorio: [v.x, v.y, v.width, v.height] };
if (w) {
  var g = w.frameGeometry;
  info.clase = w.resourceClass;
  info.completa = w.fullScreen;
  info.geo = [g.x, g.y, g.width, g.height];
}
callDBus("org.apuntador.Ventana", "/", "org.apuntador.Ventana", "Info", JSON.stringify(info));
