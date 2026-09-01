include $(APPDIR)/Make.defs

AI_AGENT_LIB_DIR = $(APPDIR)/../contest2026_046_FirmNova/rivotek/services/ai_agent

CFLAGS += ${INCDIR_PREFIX}$(AI_AGENT_LIB_DIR)/include
CFLAGS += ${INCDIR_PREFIX}$(AI_AGENT_LIB_DIR)/src
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/netutils/cjson/cJSON
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../frameworks/system/topics/include

MAINSRC  = demo_main.c
PROGNAME = $(CONFIG_AI_AGENT_DEMO_PROGNAME)
PRIORITY = $(CONFIG_AI_AGENT_DEMO_PRIORITY)
STACKSIZE = $(CONFIG_AI_AGENT_DEMO_STACKSIZE)
MODULE   = $(CONFIG_AI_AGENT_DEMO_APP)

include $(APPDIR)/Application.mk
