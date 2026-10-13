// Página de ajustes de Clay en español o inglés.
module.exports = function(es) {
  function t(textoEs, textoEn) { return es ? textoEs : textoEn; }
  // Atributos de campos donde el autocorrector estorba (sin Object.assign: el motor puede ser ES5).
  function sinTeclado(extra) {
    var a = { "autocapitalize": "off", "autocorrect": "off", "spellcheck": "false" };
    for (var k in extra) { a[k] = extra[k]; }
    return a;
  }
  return [
    { "type": "heading", "defaultValue": "Apuntador" },
    {
      "type": "text",
      "defaultValue": t(
        "Necesita el servidor en tu computadora: <a href=\"https://github.com/Rucolastico/pebble-apuntador\">github.com/Rucolastico/pebble-apuntador</a>. " +
        "El teléfono prueba las direcciones en orden y usa la primera que responda.",
        "Requires the server running on your computer: <a href=\"https://github.com/Rucolastico/pebble-apuntador\">github.com/Rucolastico/pebble-apuntador</a>. " +
        "The phone tries the addresses in order and uses the first one that answers.")
    },
    {
      "type": "section",
      "items": [
        { "type": "heading", "defaultValue": t("Servidor", "Server") },
        {
          "type": "input", "messageKey": "Token", "label": "Token", "defaultValue": "",
          "description": t("Contenido de ~/.config/apuntador/token en la computadora.",
                           "Contents of ~/.config/apuntador/token on the computer."),
          "attributes": sinTeclado({ "type": "password" })
        },
        {
          "type": "input", "messageKey": "Tailscale", "label": t("Dirección de Tailscale (opcional)", "Tailscale address (optional)"),
          "defaultValue": "",
          "description": t("Se prueba primero. Sirve en cualquier red.", "Tried first. Works on any network."),
          "attributes": sinTeclado({ "placeholder": "100.x.y.z" })
        },
        {
          "type": "input", "messageKey": "Locales", "label": t("Direcciones locales", "Local addresses"),
          "defaultValue": "",
          "description": t("Separadas por comas. Se admite IP:puerto.", "Comma separated. IP:port is accepted."),
          "attributes": sinTeclado({ "placeholder": "192.168.1.20, 10.0.0.5" })
        },
        {
          "type": "input", "messageKey": "Port", "label": t("Puerto", "Port"), "defaultValue": "8765",
          "attributes": { "type": "number", "min": 1, "max": 65535 }
        }
      ]
    },
    {
      "type": "section",
      "items": [
        { "type": "heading", "defaultValue": t("Programa", "Program") },
        {
          "type": "select", "messageKey": "Perfil", "label": t("Atajos de teclado", "Keyboard shortcuts"),
          "defaultValue": "estandar",
          "options": [
            { "label": "PowerPoint / LibreOffice", "value": "estandar" },
            { "label": "OnlyOffice", "value": "onlyoffice" }
          ],
          "description": t(
            "PowerPoint/LibreOffice: Select = pantalla negra (B), arriba largo = desde la actual (Mayús+F5). " +
            "OnlyOffice no tiene esos atajos: Select = Tab y arriba largo = primera diapositiva (Inicio).",
            "PowerPoint/LibreOffice: Select = black screen (B), hold Up = from current slide (Shift+F5). " +
            "OnlyOffice lacks those shortcuts: Select = Tab and hold Up = first slide (Home).")
        }
      ]
    },
    {
      "type": "section",
      "items": [
        { "type": "heading", "defaultValue": t("Avisos del cronómetro", "Timer alerts") },
        {
          "type": "slider", "messageKey": "Vibe1", "label": t("Primer aviso (min)", "First alert (min)"),
          "defaultValue": 40, "min": 0, "max": 180, "step": 5,
          "description": t("0 lo desactiva.", "0 turns it off.")
        },
        {
          "type": "slider", "messageKey": "Vibe2", "label": t("Segundo aviso (min)", "Second alert (min)"),
          "defaultValue": 50, "min": 0, "max": 180, "step": 5
        }
      ]
    },
    { "type": "submit", "defaultValue": t("Guardar", "Save") }
  ];
};
