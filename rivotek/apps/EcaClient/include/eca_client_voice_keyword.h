#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_VOICE_KEYWORD_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_VOICE_KEYWORD_H

#include "eca_client_model.h"

#define ECA_CLIENT_VOICE_KEYWORD_PIPE "/var/pipe/eca_voice_keyword"

int eca_client_voice_keyword_start(eca_client_model_t *model);
void eca_client_voice_keyword_stop(void);

#endif
