#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio_focus_manager.h"

static const char *audio_focus_owner_name(int owner)
{
  switch (owner)
    {
      case AUDIO_FOCUS_AI:
        return "AI_FOCUSED";
      case AUDIO_FOCUS_MUSIC:
        return "MUSIC_FOCUSED";
      default:
        return "IDLE";
    }
}

static const char *audio_focus_change_name(int change)
{
  switch (change)
    {
      case AUDIO_FOCUS_CHANGE_GAIN:
        return "GAIN";
      case AUDIO_FOCUS_CHANGE_LOSS:
        return "LOSS";
      case AUDIO_FOCUS_CHANGE_LOSS_TRANSIENT:
        return "LOSS_TRANSIENT";
      case AUDIO_FOCUS_CHANGE_LOSS_TRANSIENT_CAN_DUCK:
        return "LOSS_TRANSIENT_CAN_DUCK";
      default:
        return "UNKNOWN";
    }
}

static int audio_focus_parse_type(const char *name)
{
  if (strcmp(name, "ai") == 0)
    {
      return AUDIO_FOCUS_AI;
    }

  if (strcmp(name, "music") == 0)
    {
      return AUDIO_FOCUS_MUSIC;
    }

  return -EINVAL;
}

static void audio_focus_test_change_cb(int change)
{
  printf("audioservice: callback change=%s(%d)\n",
         audio_focus_change_name(change), change);
}

static void audio_focus_test_lost_cb(void)
{
  printf("audioservice: callback lost\n");
}

static void audio_focus_print_owner(const char *tag)
{
  printf("audioservice: %s owner=%s\n",
         tag,
         audio_focus_owner_name(audio_focus_get_owner()));
}

static int audio_focus_do_request(const char *type_name)
{
  int type = audio_focus_parse_type(type_name);
  int ret;

  if (type < 0)
    {
      return type;
    }

  ret = audio_focus_request(type);
  printf("audioservice: request %s ret=%d\n", type_name, ret);
  audio_focus_print_owner("after request");
  return ret;
}

static int audio_focus_do_release(const char *type_name)
{
  int type = audio_focus_parse_type(type_name);
  int ret;

  if (type < 0)
    {
      return type;
    }

  ret = audio_focus_release(type);
  printf("audioservice: release %s ret=%d\n", type_name, ret);
  audio_focus_print_owner("after release");
  return ret;
}

static int audio_focus_run_transient_case(void)
{
  int ret;

  printf("audioservice: run transient case start\n");

  ret = audio_focus_register_change_cb(audio_focus_test_change_cb);
  if (ret < 0)
    {
      printf("audioservice: register change cb failed: %d\n", ret);
      return ret;
    }

  ret = audio_focus_register_lost_cb(audio_focus_test_lost_cb);
  if (ret < 0)
    {
      printf("audioservice: register lost cb failed: %d\n", ret);
      return ret;
    }

  ret = audio_focus_request(AUDIO_FOCUS_MUSIC);
  if (ret < 0)
    {
      printf("audioservice: request music failed: %d\n", ret);
      return ret;
    }

  audio_focus_print_owner("step1 music focus");

  ret = audio_focus_request(AUDIO_FOCUS_AI);
  if (ret < 0)
    {
      printf("audioservice: request ai transient failed: %d\n", ret);
      return ret;
    }

  audio_focus_print_owner("step2 ai preempt");
  usleep(200 * 1000);

  ret = audio_focus_release(AUDIO_FOCUS_AI);
  if (ret < 0)
    {
      printf("audioservice: release ai failed: %d\n", ret);
      return ret;
    }

  audio_focus_print_owner("step3 ai release");
  usleep(200 * 1000);

  ret = audio_focus_release(AUDIO_FOCUS_MUSIC);
  if (ret < 0)
    {
      printf("audioservice: release music failed: %d\n", ret);
      return ret;
    }

  audio_focus_print_owner("step4 music release");
  printf("audioservice: run transient case done\n");
  return 0;
}

static void audio_focus_print_usage(const char *prog)
{
  printf("Usage:\n");
  printf("  %s status\n", prog);
  printf("  %s request <ai|music>\n", prog);
  printf("  %s release <ai|music>\n", prog);
  printf("  %s register_cb\n", prog);
  printf("  %s case transient\n", prog);
  printf("  q\n");
}

static int audio_focus_handle_command(int argc, char *argv[])
{
  int ret;

  if (argc > 1 && strcmp(argv[1], "status") == 0)
    {
      printf("audioservice: owner=%s\n",
             audio_focus_owner_name(audio_focus_get_owner()));
      return 0;
    }

  if (argc > 2 && strcmp(argv[1], "request") == 0)
    {
      ret = audio_focus_do_request(argv[2]);
      if (ret < 0)
        {
          printf("audioservice: request failed: %d\n", ret);
          return 1;
        }

      return 0;
    }

  if (argc > 2 && strcmp(argv[1], "release") == 0)
    {
      ret = audio_focus_do_release(argv[2]);
      if (ret < 0)
        {
          printf("audioservice: release failed: %d\n", ret);
          return 1;
        }

      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "register_cb") == 0)
    {
      ret = audio_focus_register_change_cb(audio_focus_test_change_cb);
      if (ret < 0)
        {
          printf("audioservice: register change cb failed: %d\n", ret);
          return 1;
        }

      ret = audio_focus_register_lost_cb(audio_focus_test_lost_cb);
      if (ret < 0)
        {
          printf("audioservice: register lost cb failed: %d\n", ret);
          return 1;
        }

      printf("audioservice: callbacks registered\n");
      return 0;
    }

  if (argc > 2 && strcmp(argv[1], "case") == 0)
    {
      if (strcmp(argv[2], "transient") == 0)
        {
          ret = audio_focus_run_transient_case();
          return ret < 0 ? 1 : 0;
        }

      printf("audioservice: unknown case: %s\n", argv[2]);
      audio_focus_print_usage(argv[0]);
      return 1;
    }

  if (argc > 1)
    {
      audio_focus_print_usage(argv[0]);
      return 1;
    }

  return 0;
}

int main(int argc, char *argv[])
{
  int ret = audio_focus_manager_init();
  char line[128];
  char *cmd_argv[4];
  int cmd_argc;
  char *saveptr;
  char *token;

  if (ret < 0)
    {
      printf("audioservice: init failed: %d\n", ret);
      return 1;
    }

  if (argc > 1)
    {
      ret = audio_focus_handle_command(argc, argv);
      if (ret < 0)
        {
          return 1;
        }
    }

  printf("audioservice: ready, owner=%s\n",
         audio_focus_owner_name(audio_focus_get_owner()));

  for (;;)
    {
      printf("audioservice> ");
      fflush(stdout);

      if (fgets(line, sizeof(line), stdin) == NULL)
        {
          printf("\naudioservice: stdin closed, exit\n");
          break;
        }

      line[strcspn(line, "\r\n")] = '\0';
      if (line[0] == '\0')
        {
          continue;
        }

      if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0)
        {
          printf("audioservice: exit\n");
          break;
        }

      cmd_argv[0] = argv[0];
      cmd_argc = 1;
      saveptr = NULL;
      token = strtok_r(line, " \t", &saveptr);
      while (token != NULL && cmd_argc < (int)(sizeof(cmd_argv) / sizeof(cmd_argv[0])))
        {
          cmd_argv[cmd_argc++] = token;
          token = strtok_r(NULL, " \t", &saveptr);
        }

      ret = audio_focus_handle_command(cmd_argc, cmd_argv);
      if (ret < 0)
        {
          printf("audioservice: command failed: %d\n", ret);
        }
    }

  return 0;
}
