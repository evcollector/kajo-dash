#pragma once

#include "controller_manager.h"

void vescUartBackendBegin();
void vescBleBackendBegin();
void vescBackendStop();
ControllerPollResult vescBackendPoll(ControllerSample &out);

void farDriverBackendBegin();
void farDriverBackendStop();
ControllerPollResult farDriverBackendPoll(ControllerSample &out);

