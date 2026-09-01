/*
 * ai_agent_lib.h — Public entry point for the Rivotek AI Agent library.
 */
#pragma once
#include "ai_agent_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the AI Agent (blocks until ai_agent_request_shutdown() is called).
 * profile must remain valid for the entire call duration (use static const).
 *
 * Returns:
 *   0        — clean shutdown
 *   -EINVAL  — profile validation failed
 *   -ENOSYS  — ABI mismatch
 *   -EBUSY   — another instance is already running
 *   -ENOMEM  — critical subsystem init failed
 */
int  ai_agent_run(const ai_agent_profile_t *profile);

/** Request graceful shutdown (thread-safe, async). */
void ai_agent_request_shutdown(void);

/** Returns true after shutdown has been requested. */
bool ai_agent_shutdown_requested(void);

/** Returns the currently active profile (NULL if not running). */
const ai_agent_profile_t *ai_agent_current_profile(void);

#ifdef __cplusplus
}
#endif
