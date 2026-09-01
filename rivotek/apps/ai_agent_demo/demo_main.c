/*
 * demo_main.c — Minimal regression demo for ai_agent library (Step 1 verify).
 * Boots with CLI-only profile; smoke-tests that ai_agent_run() starts
 * and all CLI commands work.  Enable AI_AGENT_DEMO_APP in Kconfig to build.
 */
#include "ai_agent_lib.h"
#include "ai_agent_profile.h"

static const ai_agent_profile_t demo_profile = {
    .abi_version          = AI_AGENT_PROFILE_ABI_V1,
    .project_id           = "agent",
    .data_dir             = "/data/ai_agent",
    .role_brief           = "AI Agent on Vela/NuttX.",
    .enable_cli           = true,
    .default_llm_backend  = "https://token-plan-cn.xiaomimimo.com/v1",
    .default_llm_model    = "mimo-v2.5",
    .default_llm_api_key  = "tp-cvfqad66oiqvandjevmv8utn6wlaqp26lfsx5tm8pvfohk2x",
};

int ai_agent_main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    return ai_agent_run(&demo_profile);
}
