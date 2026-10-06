#include "vehicle_fields.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool vehicleFieldIsText(int field) {
  if (field >= VEHICLE_FIELD_MODE_LABEL_1 && field <= VEHICLE_FIELD_MODE_LABEL_3) return true;
  return field == VEHICLE_FIELD_OEM || field == VEHICLE_FIELD_NAME || field == VEHICLE_FIELD_MOTOR ||
         field == VEHICLE_FIELD_CONTROLLER || field == VEHICLE_FIELD_BUILD_ID;
}

bool vehicleFieldIsNumeric(int field) {
  if (field >= SPEED_FIELD_BASE && field < SPEED_FIELD_BASE + SPEED_FIELD_COUNT) {
    return true;
  }
  return (field >= VEHICLE_FIELD_BATTERY_S && field <= VEHICLE_FIELD_WHEEL_MM) ||
         (field >= VEHICLE_FIELD_VESC_BAUD && field <= VEHICLE_FIELD_DRIVE_RATIO) ||
         (field >= VEHICLE_FIELD_CELL_MIN_V && field <= VEHICLE_FIELD_CELL_MAX_V);
}

const char *vehicleFieldTitle(int field) {
  if (field == VEHICLE_FIELD_MODE_LABEL_1) return txt("Low gear label", "Matalan nimi", "Name: Niedrig", "Nom bas", "Nombre bajo", "Nome basso");
  if (field == VEHICLE_FIELD_MODE_LABEL_2) return txt("Mid gear label", "Keskitilan nimi", "Name: Mittel", "Nom moyen", "Nombre medio", "Nome medio");
  if (field == VEHICLE_FIELD_MODE_LABEL_3) return txt("High gear label", "Korkean nimi", "Name: Hoch", "Nom haut", "Nombre alto", "Nome alto");
  if (field >= SPEED_FIELD_BASE && field < SPEED_FIELD_BASE + SPEED_FIELD_COUNT) {
    switch (field - SPEED_FIELD_BASE) {
      case SPEED_FIELD_TOP_SPEED:
        return txt("Speed scale", "Nopeusasteikko", "Temposkala", "Échelle vitesse", "Escala velocidad", "Scala velocità");
      case SPEED_FIELD_MODE_1:
        return txt("Mode 1 Limit", "Tilan 1 raja", "Modus-1-Limit", "Limite mode 1", "Límite modo 1", "Limite modo 1");
      case SPEED_FIELD_MODE_2:
        return txt("Mode 2 Limit", "Tilan 2 raja", "Modus-2-Limit", "Limite mode 2", "Límite modo 2", "Limite modo 2");
      case SPEED_FIELD_MODE_3:
        return txt("Mode 3 Limit", "Tilan 3 raja", "Modus-3-Limit", "Limite mode 3", "Límite modo 3", "Limite modo 3");
      case SPEED_FIELD_POWER_CURVE:
        return txt("Power Curve", "Tehokäyrä", "Leistungskurve", "Courbe puissance", "Curva potencia", "Curva potenza");
      case SPEED_FIELD_ACCEL_CURVE:
        return txt("Accel Curve", "Kiihdytyskäyrä", "Beschl.-Kurve", "Courbe accél.", "Curva aceleración", "Curva acceleraz.");
      default:
        return "";
    }
  }
  switch (field) {
    case VEHICLE_FIELD_OEM:
      return "OEM";
    case VEHICLE_FIELD_NAME:
      return txt("Vehicle Name", "Ajoneuvon nimi", "Fahrzeugname", "Nom du véhicule", "Nombre vehículo", "Nome veicolo");
    case VEHICLE_FIELD_MOTOR:
      return txt("Motor", "Moottori", "Motor", "Moteur", "Motor", "Motore");
    case VEHICLE_FIELD_CONTROLLER:
      return txt("Controller", "Ohjain", "Controller", "Contrôleur", "Controlador", "Controller");
    case VEHICLE_FIELD_BUILD_ID:
      return txt("Build ID", "Koontitunnus", "Build-ID", "ID version", "ID compilación", "ID build");
    case VEHICLE_FIELD_BATTERY_S:
      return txt("Battery Series", "Kennomäärä", "Zellzahl", "Cellules batterie", "Celdas batería", "Celle batteria");
    case VEHICLE_FIELD_BATTERY_CHEMISTRY:
      return txt("Chemistry", "Kemia", "Zellchemie", "Chimie", "Química", "Chimica");
    case VEHICLE_FIELD_CELL_MIN_V:
      return txt("Cell Min", "Kennon min", "Zelle min.", "Cellule min", "Celda mín.", "Cella min");
    case VEHICLE_FIELD_CELL_NOMINAL_V:
      return txt("Cell Nominal", "Kennon nimellis", "Zelle nominal", "Cellule nominale", "Celda nominal",
                 "Cella nominale");
    case VEHICLE_FIELD_CELL_MAX_V:
      return txt("Cell Max", "Kennon max", "Zelle max.", "Cellule max", "Celda máx.", "Cella max");
    case VEHICLE_FIELD_BATTERY_AH:
      return txt("Battery Capacity", "Akun kapasiteetti", "Batteriekapazität", "Capacité batterie", "Capacidad batería", "Capacità batteria");
    case VEHICLE_FIELD_BATTERY_MAX_A:
      return txt("Current scale", "Virta-asteikko", "Stromskala", "Échelle courant", "Escala corriente", "Scala corrente");
    case VEHICLE_FIELD_MOTOR_MAX_A:
      return txt("Motor scale", "Moottoriasteikko", "Motorstromskala", "Échelle moteur", "Escala motor", "Scala motore");
    case VEHICLE_FIELD_CONT_KW:
      return txt("Continuous Power", "Jatkuva teho", "Dauerleistung", "Puissance continue", "Potencia continua", "Potenza continua");
    case VEHICLE_FIELD_PEAK_KW:
      return txt("Power scale", "Tehoasteikko", "Leistungsskala", "Échelle puissance", "Escala potencia", "Scala potenza");
    case VEHICLE_FIELD_WHEEL_MM:
      // One slot, two meanings: while the controller reports its own speed this
      // board has no use for a wheel size, only for a correction to what it is
      // told. Showing the setting that is actually in effect beats showing one
      // that silently does nothing.
      return telemetrySpeedFromController()
                 ? txt("Speed Calibration", "Nopeuden kalibrointi", "Tempo-Kalibrierung",
                       "Calibrage vitesse", "Calibración velocidad", "Calibrazione velocità")
                 : txt("Wheel Diameter", "Renkaan koko", "Raddurchmesser", "Diamètre roue", "Diámetro rueda",
                       "Diametro ruota");
    case VEHICLE_FIELD_SPEED_SETUP:
      return txt("SPEEDS", "NOPEUDET", "TEMPO", "VITESSES", "VELOCIDADES", "VELOCITÀ");
    case VEHICLE_FIELD_SETUP:
      return txt("New User Setup", "Käyttöönotto", "Ersteinrichtung", "Config. initiale", "Config. inicial", "Config. iniziale");
    case VEHICLE_FIELD_VESC_BAUD:
      return txt("UART Baud", "UART-nopeus", "UART-Baudrate", "Débit UART", "Baudios UART", "Baud UART");
    case VEHICLE_FIELD_VESC_CAN_ID:
      return txt("CAN Target ID", "CAN-kohde-ID", "CAN-Ziel-ID", "ID cible CAN", "ID CAN destino", "ID CAN destin.");
    case VEHICLE_FIELD_MOTOR_POLE_PAIRS:
      return txt("Motor Pole Pairs", "Moottorin napaparit", "Motor-Polpaare", "Paires de pôles", "Pares de polos", "Coppie polari");
    case VEHICLE_FIELD_DRIVE_RATIO:
      return txt("Drive Ratio", "Välityssuhde", "Übersetzung", "Rapport transmission", "Relación transmisión", "Rapporto trasmiss.");
    default:
      return "";
  }
}

// Short, field-specific guidance for the compact input screen. These strings
// deliberately explain the meaning and unit instead of repeating the title.
const char *vehicleFieldInputHint(int field) {
  if (field >= VEHICLE_FIELD_MODE_LABEL_1 && field <= VEHICLE_FIELD_MODE_LABEL_3)
    return txt("Display label; blank uses default", "Näyttönimi; tyhjä = oletus", "Anzeigename; leer = Standard", "Nom écran ; vide = défaut", "Nombre; vacío = predeterminado", "Nome; vuoto = predefinito");
  if (field >= SPEED_FIELD_BASE && field < SPEED_FIELD_BASE + SPEED_FIELD_COUNT) {
    switch (field - SPEED_FIELD_BASE) {
      case SPEED_FIELD_TOP_SPEED:
        return txt("Gauge range only; saved on display", "Vain mittari; tallennus näyttöön", "Nur Anzeigeskala; lokal gespeichert", "Échelle seulement ; stockage écran", "Solo escala; guardado en pantalla", "Solo scala; salvata sul display");
      case SPEED_FIELD_MODE_1:
        return txt("Speed limit used by riding mode 1", "Ajotilan 1 nopeusraja", "Tempolimit für Fahrmodus 1",
                   "Limite du mode de conduite 1", "Límite del modo de conducción 1",
                   "Limite della modalità di guida 1");
      case SPEED_FIELD_MODE_2:
        return txt("Speed limit used by riding mode 2", "Ajotilan 2 nopeusraja", "Tempolimit für Fahrmodus 2",
                   "Limite du mode de conduite 2", "Límite del modo de conducción 2",
                   "Limite della modalità di guida 2");
      case SPEED_FIELD_MODE_3:
        return txt("Speed limit used by riding mode 3", "Ajotilan 3 nopeusraja", "Tempolimit für Fahrmodus 3",
                   "Limite du mode de conduite 3", "Límite del modo de conducción 3",
                   "Limite della modalità di guida 3");
      case SPEED_FIELD_POWER_CURVE:
        return txt("Power response from 0 to 100%", "Tehovaste 0-100 %", "Leistungsreaktion von 0 bis 100 %",
                   "Réponse de puissance de 0 à 100 %", "Respuesta de potencia de 0 a 100 %",
                   "Risposta di potenza da 0 a 100%");
      case SPEED_FIELD_ACCEL_CURVE:
        return txt("Acceleration response from 0 to 100%", "Kiihtyvyysvaste 0-100 %",
                   "Beschleunigungsreaktion von 0 bis 100 %", "Réponse d'accélération de 0 à 100 %",
                   "Respuesta de aceleración de 0 a 100 %", "Risposta di accelerazione da 0 a 100%");
      default:
        return "";
    }
  }
  switch (field) {
    case VEHICLE_FIELD_OEM:
      return txt("Vehicle manufacturer or brand", "Ajoneuvon valmistaja tai merkki", "Fahrzeughersteller oder Marke",
                 "Constructeur ou marque du véhicule", "Fabricante o marca del vehículo",
                 "Produttore o marca del veicolo");
    case VEHICLE_FIELD_NAME:
      return txt("Name shown for this vehicle", "Ajoneuvolle näytettävä nimi", "Angezeigter Fahrzeugname",
                 "Nom affiché pour ce véhicule", "Nombre mostrado para este vehículo",
                 "Nome visualizzato per il veicolo");
    case VEHICLE_FIELD_MOTOR:
      return txt("Motor make or model", "Moottorin merkki tai malli", "Hersteller oder Modell des Motors",
                 "Marque ou modèle du moteur", "Marca o modelo del motor", "Marca o modello del motore");
    case VEHICLE_FIELD_CONTROLLER:
      return txt("Controller make or model", "Ohjaimen merkki tai malli", "Hersteller oder Modell des Controllers",
                 "Marque ou modèle du contrôleur", "Marca o modelo del controlador",
                 "Marca o modello del controller");
    case VEHICLE_FIELD_BUILD_ID:
      return txt("Build or configuration identifier", "Koonti- tai määritystunnus", "Build- oder Konfigurationskennung",
                 "Identifiant de build ou configuration", "Identificador de compilación o configuración",
                 "Identificativo build o configurazione");
    case VEHICLE_FIELD_BATTERY_S:
      return txt("Series cell count, 4-32 S", "Sarjaan kytketyt kennot, 4-32 S", "Zellen in Reihe, 4-32 S",
                 "Cellules en série, 4-32 S", "Celdas en serie, 4-32 S", "Celle in serie, 4-32 S");
    case VEHICLE_FIELD_CELL_MIN_V:
      return txt("Empty cell voltage (0%), 2.0-4.0", "Tyhjän kennon jännite (0 %), 2.0-4.0",
                 "Spannung leere Zelle (0 %), 2.0-4.0", "Tension cellule vide (0 %), 2.0-4.0",
                 "Voltaje celda vacía (0 %), 2.0-4.0", "Tensione cella vuota (0%), 2.0-4.0");
    case VEHICLE_FIELD_CELL_NOMINAL_V:
      return txt("Nominal volts per cell, used for Wh, 2.5-4.3", "Nimellisjännite per kenno (Wh), 2.5-4.3",
                 "Nennspannung je Zelle für Wh, 2.5-4.3", "Tension nominale par cellule (Wh), 2.5-4.3",
                 "Voltios nominales por celda (Wh), 2.5-4.3", "Volt nominali per cella (Wh), 2.5-4.3");
    case VEHICLE_FIELD_CELL_MAX_V:
      return txt("Full-charge cell voltage, 3.0-4.5", "Täyden latauksen jännite, 3.0-4.5",
                 "Ladeschlussspannung je Zelle, 3.0-4.5", "Tension de charge complète, 3.0-4.5",
                 "Voltaje de carga completa, 3.0-4.5", "Tensione di carica completa, 3.0-4.5");
    case VEHICLE_FIELD_BATTERY_AH:
      return txt("Capacity in Ah; kAh above 9999.9", "Kapasiteetti Ah; yli 9999.9 kAh",
                 "Kapazität in Ah; ab 9999.9 kAh", "Capacité en Ah ; kAh > 9999.9",
                 "Capacidad en Ah; kAh sobre 9999.9", "Capacità in Ah; kAh oltre 9999.9");
    case VEHICLE_FIELD_BATTERY_MAX_A:
      return txt("Gauge range only; saved on display", "Vain mittari; tallennus näyttöön", "Nur Anzeigeskala; lokal gespeichert", "Échelle seulement ; stockage écran", "Solo escala; guardado en pantalla", "Solo scala; salvata sul display");
    case VEHICLE_FIELD_MOTOR_MAX_A:
      return txt("Gauge range only; saved on display", "Vain mittari; tallennus näyttöön", "Nur Anzeigeskala; lokal gespeichert", "Échelle seulement ; stockage écran", "Solo escala; guardado en pantalla", "Solo scala; salvata sul display");
    case VEHICLE_FIELD_CONT_KW:
      return txt("Continuous system power in kW", "Järjestelmän jatkuva teho (kW)", "Dauerleistung des Systems in kW",
                 "Puissance continue du système en kW", "Potencia continua del sistema en kW",
                 "Potenza continua del sistema in kW");
    case VEHICLE_FIELD_PEAK_KW:
      return txt("Gauge range only; saved on display", "Vain mittari; tallennus näyttöön", "Nur Anzeigeskala; lokal gespeichert", "Échelle seulement ; stockage écran", "Solo escala; guardado en pantalla", "Solo scala; salvata sul display");
    case VEHICLE_FIELD_WHEEL_MM:
      return telemetrySpeedFromController()
                 ? txt("Correction %, 100 keeps reported speed", "Korjaus %, 100 säilyttää ilmoitetun nopeuden",
                       "Korrektur %, 100 behält das Tempo", "Correction %, 100 garde la vitesse reçue",
                       "Corrección %, 100 conserva la velocidad", "Correzione %, 100 mantiene la velocità")
                 : txt("Driven wheel diameter in millimetres", "Vetävän pyörän halkaisija millimetreinä",
                       "Durchmesser des Antriebsrads in Millimetern", "Diamètre de la roue motrice en millimètres",
                       "Diámetro de la rueda motriz en milímetros", "Diametro ruota motrice in millimetri");
    case VEHICLE_FIELD_VESC_BAUD:
      return txt("UART speed, 9600-921600 baud", "UART-nopeus, 9600-921600 baudia", "UART-Tempo, 9600-921600 Baud",
                 "Débit UART, 9600-921600 bauds", "Velocidad UART, 9600-921600 baudios",
                 "Velocità UART, 9600-921600 baud");
    case VEHICLE_FIELD_VESC_CAN_ID:
      return txt("CAN controller ID; 0 uses local", "CAN-ohjaimen ID; 0 käyttää paikallista",
                 "CAN-Controller-ID; 0 ist lokal", "ID du contrôleur CAN ; 0 utilise le local",
                 "ID del controlador CAN; 0 usa el local", "ID controller CAN; 0 usa quello locale");
    case VEHICLE_FIELD_MOTOR_POLE_PAIRS:
      return txt("Pole-pair count used to calculate speed", "Nopeuslaskennan napaparien määrä",
                 "Polpaarzahl zur Tempoberechnung", "Paires de pôles pour calculer la vitesse",
                 "Pares de polos para calcular la velocidad", "Coppie polari per calcolare la velocità");
    case VEHICLE_FIELD_DRIVE_RATIO:
      return txt("Motor-to-wheel reduction ratio", "Moottorin ja pyörän välityssuhde", "Untersetzung vom Motor zum Rad",
                 "Rapport de réduction moteur-roue", "Relación de reducción motor-rueda",
                 "Rapporto di riduzione motore-ruota");
    default:
      return "";
  }
}

static uint32_t *speedFieldKmhPtr(int speedField) {
  switch (speedField) {
    case SPEED_FIELD_TOP_SPEED:
      return &topSpeedKmh;
    case SPEED_FIELD_MODE_1:
      return &speedMode1Kmh;
    case SPEED_FIELD_MODE_2:
      return &speedMode2Kmh;
    case SPEED_FIELD_MODE_3:
      return &speedMode3Kmh;
    default:
      return NULL;
  }
}

static void normalizeSpeedSettings() {
  speedModeCount = constrain(speedModeCount, 1, 3);
  speedMode1Kmh = min(speedMode1Kmh, topSpeedKmh);
  speedMode2Kmh = min(speedMode2Kmh, topSpeedKmh);
  speedMode3Kmh = min(speedMode3Kmh, topSpeedKmh);
}

static void formatSpeedSetting(uint32_t kmh, char *buffer, size_t size, bool withUnit) {
  const float shown = displaySpeed((float)kmh);
  if (unitMode == UNITS_MACH) {
    snprintf(buffer, size, withUnit ? "%.3f %s" : "%.3f", shown, speedUnitLabel());
  } else {
    snprintf(buffer, size, withUnit ? "%d %s" : "%d", (int)roundf(shown), speedUnitLabel());
  }
}

static char *vehicleFieldTextPtr(int field) {
  if (field >= VEHICLE_FIELD_MODE_LABEL_1 && field <= VEHICLE_FIELD_MODE_LABEL_3) return rideModeLabels[field - VEHICLE_FIELD_MODE_LABEL_1];
  switch (field) {
    case VEHICLE_FIELD_OEM:
      return oemName;
    case VEHICLE_FIELD_NAME:
      return vehicleName;
    case VEHICLE_FIELD_MOTOR:
      return motorName;
    case VEHICLE_FIELD_CONTROLLER:
      return controllerName;
    case VEHICLE_FIELD_BUILD_ID:
      return vehicleBuildId;
    default:
      return NULL;
  }
}

size_t vehicleFieldTextSize(int field) {
  if (field >= VEHICLE_FIELD_MODE_LABEL_1 && field <= VEHICLE_FIELD_MODE_LABEL_3) return sizeof(rideModeLabels[0]);
  switch (field) {
    case VEHICLE_FIELD_OEM:
      return sizeof(oemName);
    case VEHICLE_FIELD_NAME:
      return sizeof(vehicleName);
    case VEHICLE_FIELD_MOTOR:
      return sizeof(motorName);
    case VEHICLE_FIELD_CONTROLLER:
      return sizeof(controllerName);
    case VEHICLE_FIELD_BUILD_ID:
      return sizeof(vehicleBuildId);
    default:
      return 0;
  }
}

void vehicleFieldValue(int field, char *buffer, size_t size) {
  if (field >= VEHICLE_FIELD_MODE_LABEL_1 && field <= VEHICLE_FIELD_MODE_LABEL_3) {
    snprintf(buffer, size, "%s", rideModeName(field - VEHICLE_FIELD_MODE_LABEL_1 + 1)); return;
  }
  if (field >= SPEED_FIELD_BASE && field < SPEED_FIELD_BASE + SPEED_FIELD_COUNT) {
    const int speedField = field - SPEED_FIELD_BASE;
    if (speedField == SPEED_FIELD_POWER_CURVE) {
      snprintf(buffer, size, "%u%%", speedPowerCurvePercent);
      return;
    }
    if (speedField == SPEED_FIELD_ACCEL_CURVE) {
      snprintf(buffer, size, "%u%%", speedAccelCurvePercent);
      return;
    }
    uint32_t *value = speedFieldKmhPtr(speedField);
    formatSpeedSetting(value ? *value : 0, buffer, size, true);
    return;
  }
  switch (field) {
    case VEHICLE_FIELD_CONTROLLER: {
      // The name stays the rider's to set; the firmware version is appended to
      // what the tile *shows*, never written into the stored name. Detected
      // facts belong beside the user's label, not on top of it.
      const char *name = controllerName[0] ? controllerName : "-";
      uint8_t major = 0;
      uint8_t minor = 0;
      if (telemetryFirmwareVersion(major, minor)) {
        snprintf(buffer, size, "%s - FW %u.%02u", name, major, minor);
      } else {
        snprintf(buffer, size, "%s", name);
      }
      return;
    }
    case VEHICLE_FIELD_OEM:
    case VEHICLE_FIELD_NAME:
    case VEHICLE_FIELD_MOTOR:
    case VEHICLE_FIELD_BUILD_ID: {
      const char *value = vehicleFieldTextPtr(field);
      snprintf(buffer, size, "%s", value && value[0] ? value : "-");
      return;
    }
    case VEHICLE_FIELD_BATTERY_S:
      snprintf(buffer, size, "%uS", batterySeriesCount);
      return;
    case VEHICLE_FIELD_BATTERY_CHEMISTRY:
      snprintf(buffer, size, "%s", batteryChemistryName(batteryChemistry));
      return;
    case VEHICLE_FIELD_CELL_MIN_V:
      snprintf(buffer, size, "%u.%02u V", batteryCellMinMv / 1000, batteryCellMinMv % 1000 / 10);
      return;
    case VEHICLE_FIELD_CELL_NOMINAL_V:
      snprintf(buffer, size, "%u.%02u V", batteryCellNominalMv / 1000, batteryCellNominalMv % 1000 / 10);
      return;
    case VEHICLE_FIELD_CELL_MAX_V:
      snprintf(buffer, size, "%u.%02u V", batteryCellMaxMv / 1000, batteryCellMaxMv % 1000 / 10);
      return;
    case VEHICLE_FIELD_BATTERY_AH:
      formatBatteryCapacityDeciAh(buffer, size, batteryCapacityDeciAh);
      return;
    case VEHICLE_FIELD_BATTERY_MAX_A:
      snprintf(buffer, size, "%u A", batteryMaxAmps);
      return;
    case VEHICLE_FIELD_MOTOR_MAX_A:
      snprintf(buffer, size, "%u A", motorMaxAmps);
      return;
    case VEHICLE_FIELD_CONT_KW:
      snprintf(buffer, size, "%u.%u kW", continuousPowerDeciKw / 10, continuousPowerDeciKw % 10);
      return;
    case VEHICLE_FIELD_PEAK_KW:
      snprintf(buffer, size, "%u.%u kW", peakPowerDeciKw / 10, peakPowerDeciKw % 10);
      return;
    case VEHICLE_FIELD_WHEEL_MM:
      if (telemetrySpeedFromController()) {
        snprintf(buffer, size, "%u.%02ux", speedCalibrationPercent / 100, speedCalibrationPercent % 100);
      } else {
        snprintf(buffer, size, "%u mm", wheelDiameterMm);
      }
      return;
    case VEHICLE_FIELD_SPEED_SETUP:
      snprintf(buffer, size, "%s", txt("Modes & limits", "Tilat ja rajat", "Modi & Grenzen", "Modes & limites",
                                        "Modos y límites", "Modi e limiti"));
      return;
    case VEHICLE_FIELD_SETUP:
      snprintf(buffer, size, "%s", txt("Run setup", "Aloita määritys", "Einrichtung starten", "Lancer la config.",
                                        "Iniciar configuración", "Avvia configurazione"));
      return;
    case VEHICLE_FIELD_VESC_BAUD:
      snprintf(buffer, size, "%lu", (unsigned long)vescUartBaud);
      return;
    case VEHICLE_FIELD_VESC_CAN_ID:
      snprintf(buffer, size, vescCanId == 0 ? "%u (local)" : "%u", vescCanId);
      return;
    case VEHICLE_FIELD_MOTOR_POLE_PAIRS:
      snprintf(buffer, size, "%u", vescMotorPolePairs);
      return;
    case VEHICLE_FIELD_DRIVE_RATIO:
      snprintf(buffer, size, "%u.%02u : 1", vescDriveRatioHundredths / 100, vescDriveRatioHundredths % 100);
      return;
    default:
      buffer[0] = '\0';
      return;
  }
}

void vehicleFieldEditText(int field, char *buffer, size_t size) {
  if (field >= SPEED_FIELD_BASE && field < SPEED_FIELD_BASE + SPEED_FIELD_COUNT) {
    const int speedField = field - SPEED_FIELD_BASE;
    if (speedField == SPEED_FIELD_POWER_CURVE) {
      snprintf(buffer, size, "%u", speedPowerCurvePercent);
      return;
    }
    if (speedField == SPEED_FIELD_ACCEL_CURVE) {
      snprintf(buffer, size, "%u", speedAccelCurvePercent);
      return;
    }
    uint32_t *value = speedFieldKmhPtr(speedField);
    formatSpeedSetting(value ? *value : 0, buffer, size, false);
    return;
  }
  switch (field) {
    case VEHICLE_FIELD_BATTERY_S:
      snprintf(buffer, size, "%u", batterySeriesCount);
      return;
    case VEHICLE_FIELD_BATTERY_AH:
      // Always in Ah, and whole Ah once it is large, so the keypad's eight
      // characters hold the biggest pack ("9999900").
      if (batteryCapacityDeciAh > 99999UL) {
        snprintf(buffer, size, "%lu", (unsigned long)((batteryCapacityDeciAh + 5UL) / 10UL));
      } else {
        formatBatteryCapacityDeciAh(buffer, size, batteryCapacityDeciAh, false);
      }
      return;
    case VEHICLE_FIELD_CELL_MIN_V:
      snprintf(buffer, size, "%u.%02u", batteryCellMinMv / 1000, batteryCellMinMv % 1000 / 10);
      return;
    case VEHICLE_FIELD_CELL_NOMINAL_V:
      snprintf(buffer, size, "%u.%02u", batteryCellNominalMv / 1000, batteryCellNominalMv % 1000 / 10);
      return;
    case VEHICLE_FIELD_CELL_MAX_V:
      snprintf(buffer, size, "%u.%02u", batteryCellMaxMv / 1000, batteryCellMaxMv % 1000 / 10);
      return;
    case VEHICLE_FIELD_BATTERY_MAX_A:
      snprintf(buffer, size, "%u", batteryMaxAmps);
      return;
    case VEHICLE_FIELD_MOTOR_MAX_A:
      snprintf(buffer, size, "%u", motorMaxAmps);
      return;
    case VEHICLE_FIELD_CONT_KW:
      snprintf(buffer, size, "%u.%u", continuousPowerDeciKw / 10, continuousPowerDeciKw % 10);
      return;
    case VEHICLE_FIELD_PEAK_KW:
      snprintf(buffer, size, "%u.%u", peakPowerDeciKw / 10, peakPowerDeciKw % 10);
      return;
    case VEHICLE_FIELD_WHEEL_MM:
      // Percent rather than a decimal multiplier: the numeric keypad has no
      // decimal point, and "103" is a less error-prone thing to type than "1.03".
      snprintf(buffer, size, "%u", telemetrySpeedFromController() ? speedCalibrationPercent : wheelDiameterMm);
      return;
    case VEHICLE_FIELD_VESC_BAUD:
      snprintf(buffer, size, "%lu", (unsigned long)vescUartBaud);
      return;
    case VEHICLE_FIELD_VESC_CAN_ID:
      snprintf(buffer, size, "%u", vescCanId);
      return;
    case VEHICLE_FIELD_MOTOR_POLE_PAIRS:
      snprintf(buffer, size, "%u", vescMotorPolePairs);
      return;
    case VEHICLE_FIELD_DRIVE_RATIO:
      snprintf(buffer, size, "%u.%02u", vescDriveRatioHundredths / 100, vescDriveRatioHundredths % 100);
      return;
    default: {
      const char *current = vehicleFieldTextPtr(field);
      snprintf(buffer, size, "%s", current ? current : "");
      return;
    }
  }
}

static uint16_t parseDeciValue(const char *text, int minValue, int maxValue) {
  float value = atof(text);
  int deci = (int)roundf(value * 10.0F);
  return constrain(deci, minValue, maxValue);
}

void saveVehicleInputValue(int field, const char *text) {
  if (vehicleFieldIsText(field)) {
    char *target = vehicleFieldTextPtr(field);
    const size_t targetSize = vehicleFieldTextSize(field);
    if (target && targetSize > 0) {
      strncpy(target, text, targetSize);
      target[targetSize - 1] = '\0';
    }
  } else {
    if (field >= SPEED_FIELD_BASE && field < SPEED_FIELD_BASE + SPEED_FIELD_COUNT) {
      const int speedField = field - SPEED_FIELD_BASE;
      if (speedField == SPEED_FIELD_POWER_CURVE) {
        speedPowerCurvePercent = constrain(atoi(text), 0, 100);
        saveVehicleProfile();
        return;
      }
      if (speedField == SPEED_FIELD_ACCEL_CURVE) {
        speedAccelCurvePercent = constrain(atoi(text), 0, 100);
        saveVehicleProfile();
        return;
      }
      uint32_t *target = speedFieldKmhPtr(speedField);
      if (target) {
        const float shown = constrain(atof(text), 0.0F, 999.0F);
        *target = constrain((int)roundf(displaySpeedToKmh(shown)), 0, 999999);
        normalizeSpeedSettings();
      }
      saveVehicleProfile();
      return;
    }
    switch (field) {
      case VEHICLE_FIELD_BATTERY_S:
        batterySeriesCount = constrain(atoi(text), 4, 32);
        break;
      case VEHICLE_FIELD_BATTERY_AH:
        {
          const double deciAh = floor(atof(text) * 10.0 + 0.5);
          batteryCapacityDeciAh = deciAh < (double)kBatteryCapacityMinDeciAh
                                      ? kBatteryCapacityMinDeciAh
                                      : (deciAh > (double)kBatteryCapacityMaxDeciAh ? kBatteryCapacityMaxDeciAh
                                                                                     : (uint32_t)deciAh);
        }
        break;
      case VEHICLE_FIELD_CELL_MIN_V:
        batteryCellMinMv = constrain((int)lroundf(atof(text) * 100.0F) * 10, 2000, 4000);
        batteryNormalizeCellVoltages(BATTERY_CELL_MIN);
        break;
      case VEHICLE_FIELD_CELL_NOMINAL_V:
        batteryCellNominalMv = constrain((int)lroundf(atof(text) * 100.0F) * 10, 2500, 4300);
        batteryNormalizeCellVoltages(BATTERY_CELL_NOMINAL);
        break;
      case VEHICLE_FIELD_CELL_MAX_V:
        batteryCellMaxMv = constrain((int)lroundf(atof(text) * 100.0F) * 10, 3000, 4500);
        batteryNormalizeCellVoltages(BATTERY_CELL_MAX);
        break;
      case VEHICLE_FIELD_BATTERY_MAX_A:
        batteryMaxAmps = constrain(atoi(text), 1, 500);
        break;
      case VEHICLE_FIELD_MOTOR_MAX_A:
        motorMaxAmps = constrain(atoi(text), 1, 500);
        break;
      case VEHICLE_FIELD_CONT_KW:
        continuousPowerDeciKw = parseDeciValue(text, 1, 500);
        break;
      case VEHICLE_FIELD_PEAK_KW:
        peakPowerDeciKw = parseDeciValue(text, 1, 500);
        break;
      case VEHICLE_FIELD_WHEEL_MM:
        if (telemetrySpeedFromController()) {
          speedCalibrationPercent = constrain(atoi(text), 50, 200);
        } else {
          wheelDiameterMm = constrain(atoi(text), 300, 1000);
        }
        break;
      case VEHICLE_FIELD_VESC_BAUD:
        vescUartBaud = constrain((uint32_t)strtoul(text, NULL, 10), 9600UL, 921600UL);
        break;
      case VEHICLE_FIELD_VESC_CAN_ID:
        vescCanId = constrain(atoi(text), 0, 254);
        break;
      case VEHICLE_FIELD_MOTOR_POLE_PAIRS:
        vescMotorPolePairs = constrain(atoi(text), 1, 64);
        break;
      case VEHICLE_FIELD_DRIVE_RATIO:
        vescDriveRatioHundredths = constrain((int)lroundf(atof(text) * 100.0F), 10, 10000);
        break;
      default:
        break;
    }
  }
  saveVehicleProfile();
}
