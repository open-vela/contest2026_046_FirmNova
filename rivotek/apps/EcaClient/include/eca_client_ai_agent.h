#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_AI_AGENT_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_AI_AGENT_H

#include <stddef.h>

int eca_client_ai_agent_start(void);
int eca_client_ai_agent_speak(const char *text, void *arg);
int eca_client_ai_agent_speak_priority(const char *text);
int eca_client_ai_agent_is_idle(void);
int eca_client_ai_agent_voice_start(void);
int eca_client_ai_agent_voice_stop(void);
int eca_client_ai_agent_voice_status(char *status, size_t status_size);

#endif
