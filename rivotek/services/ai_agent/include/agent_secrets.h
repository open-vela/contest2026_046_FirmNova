/*
 * agent_secrets.h — Build-time secrets for AI Agent
 *
 * This file contains credential defaults that are baked into the firmware
 * at compile time. They are seeded into /data/agent/config/config.json
 * on first boot (or after a flash that clears /data) so that no manual
 * CLI commands (set_volc_key / set_volc_asr) are required.
 *
 * WARNING: Do not commit this file to a public repository.
 */

#pragma once

/* Doubao (Volcengine) Voice TTS — API key for the unified voice API */
#define AGENT_SECRET_VOLC_API_KEY "621baea8-7aa1-436b-b52e-07d06d3354c6"

/* Doubao (Volcengine) Streaming ASR — app_id and token */
#define AGENT_SECRET_VOLC_APP_ID  "8962802965"
#define AGENT_SECRET_VOLC_TOKEN   "sQ-gNoRSx6y7OlRXaLbUhL1Fp_YqEe96"
