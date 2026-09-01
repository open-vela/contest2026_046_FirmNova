/*
 * ai_agent_profile.h — Project profile API for Rivotek AI Agent library.
 *
 * A thin app fills in an ai_agent_profile_t (static const) and calls
 * ai_agent_run(&profile).  The framework never writes to the profile.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AI_AGENT_PROFILE_ABI_V1 1u

/* forward — do not expose internal bus header to project code */
struct agent_msg;

typedef char *(*ai_agent_tool_provider_fn)(void);
typedef int   (*ai_agent_tool_executor_fn)(const char *name,
                                           const char *input_json,
                                           char *output, size_t output_size);
typedef int  (*ai_agent_hook_int_fn)(void *ctx);
typedef void (*ai_agent_hook_void_fn)(void *ctx);
/* return: 0=pass, >0=consumed, <0=drop */
typedef int  (*ai_agent_msg_filter_fn)(struct agent_msg *msg, void *ctx);
typedef bool (*ai_agent_tool_filter_fn)(const char *tool_name, void *ctx);

typedef struct ai_agent_profile {
    /* §1 ABI */
    uint32_t   abi_version;          /* must = AI_AGENT_PROFILE_ABI_V1 */
    void      *user_ctx;             /* passed verbatim to every hook   */

    /* §2 Identity */
    const char *project_id;          /* required; ^[a-z][a-z0-9_]{1,15}$ */
    const char *device_display_name; /* default = project_id            */
    const char *device_type;         /* skill_sync filter; default "all" */
    const char *node_id;             /* NULL = skip Node registration    */

    /* §3 Paths */
    const char *data_dir;            /* default "/data/<project_id>"    */
    const char *romfs_root;          /* default "/etc/<project_id>"     */
    const char *extra_skills_dir;    /* default romfs_root + "/skills"  */

    /* §4 System-prompt injection */
    const char *role_brief;          /* replaces default opening line   */
    const char *channel_summary;     /* replaces "Channels: ..." line   */
    const char *system_prompt_extra; /* appended after Skills section   */
    const char *timezone;            /* POSIX TZ; default "CST-8"       */
    const char *locale;              /* "zh-CN" etc; NULL=follow user   */

    /* §5 Channel runtime switches (compile-time OFF overrides these)  */
    bool enable_cli;
    bool enable_voice;
    bool enable_websocket;
    bool enable_feishu;
    bool enable_weixin;
    bool enable_mqtt;
    bool enable_node;
    bool enable_mcp;
    bool enable_lvgl_ui;

    /* §6 LLM defaults (written to config_store only when no value yet) */
    const char *default_llm_backend;  /* URL or preset, e.g. "https://host/v1" */
    const char *default_llm_model;
    const char *default_llm_api_key;  /* api_key; NULL = leave unchanged   */
    int         llm_timeout_sec;     /* 0 = use framework default (60s)  */

    /* §7 Conversation tuning (0 = use framework defaults)              */
    int  max_history_msgs;
    int  max_tool_iterations;
    int  max_parallel_tool_calls;
    int  context_buf_size;

    /* §8 Project tools                                                  */
    ai_agent_tool_provider_fn  project_get_tools; /* may be NULL        */
    ai_agent_tool_executor_fn  project_exec;      /* may be NULL        */
    ai_agent_tool_filter_fn    tool_visibility_filter; /* may be NULL   */

    /* §9 Skill filtering (NULL-terminated arrays; NULL array = no filter) */
    const char * const *skill_allowlist;
    const char * const *skill_blocklist;

    /* §10 Voice (effective only when enable_voice && CONFIG_AI_AGENT_*) */
    const char *voice_default_speaker;
    const char *voice_capture_dev;
    const char *voice_playback_dev;
    const char *voice_error_phrase;  /* "" = silent on TTS failure      */

    /* §11 Lifecycle hooks (all optional)                                */
    ai_agent_hook_int_fn  on_pre_init;   /* before P0; ERROR aborts     */
    ai_agent_hook_int_fn  on_post_init;  /* after P3; ERROR aborts      */
    ai_agent_hook_int_fn  on_ready;      /* after all channels start    */
    ai_agent_hook_void_fn on_shutdown;   /* before teardown             */

    /* §12 Message hooks (hot-path; must be non-blocking)               */
    ai_agent_msg_filter_fn on_inbound_message;
    ai_agent_msg_filter_fn on_outbound_message;

} ai_agent_profile_t;

#ifdef __cplusplus
}
#endif
