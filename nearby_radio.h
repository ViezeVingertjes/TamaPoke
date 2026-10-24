#pragma once
#include <stdint.h>
#include "nearby.h"
#include "power.h"

// Capa de radio de los TamaPokes cercanos (ESP-NOW). La logica (paquete, tabla, horario) esta
// en nearby.h; esto solo enciende y apaga la radio. En el renderer del host hay una version
// de mentira (test/render/nearby_host.cpp).

struct NbSelf {
  bool valid;       // false si todavia es un huevo: no se envia nada
  uint8_t species;
  bool shiny, sleeping;
  uint16_t level;
};

void nearbyBegin();                         // lee (o crea) en la NVS el id de este aparato; busca durante 60 s
bool nearbySearching();
// Maquina de estados de la radio: se llama en cada pase del bucle principal (es barata cuando la
// radio esta apagada). Enciende la radio en las ventanas del horario, envia y recibe, y la apaga.
void nearbyPoll(uint32_t now, const PowerCtx &c, const NbSelf &self);
bool nearbyRadioOn();                       // con la radio encendida no se puede hacer light sleep
uint32_t nearbyNextEventMs(uint32_t now);   // ms hasta la proxima ventana (0 = ninguna): el sueno no pasa de ahi
int nearbyShown(const NbEntry **out);       // vecinos para dibujar (hasta NB_MAX_SHOWN)
uint32_t nearbyEpoch();                     // +1 cada vez que cambia quien esta cerca
