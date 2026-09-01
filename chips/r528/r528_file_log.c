#include <syslog.h>
#include <errno.h>
#include <sys/mount.h>
//#include <nuttx/clock.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <syslog.h>

#include <nuttx/board.h>
#include <nuttx/signal.h>
#include <nuttx/syslog/syslog.h>

#define SYSLOG_LOG_DIR "/data/log"
#define SYSLOG_MAX_LOGS CONFIG_R528_FILE_LOG_MAX_NUMBER

//static struct work_s g_syslog_flush_work;


/****************************************************************************
 * Name: syslog_flush_delayed
 * Description:
 *   Delayed work callback to flush syslog buffers.
 ****************************************************************************/
//static void syslog_flush_delayed(FAR void *arg)
//{
//  UNUSED(arg);
//  syslog(LOG_INFO, "SYSLOG flush delayed\n");
  //syslog_flush();

  //work_queue(HPWORK, &g_syslog_flush_work, syslog_flush_delayed,NULL, SEC2TICK(20));
//}

/****************************************************************************
 * Name: syslog_file_rotate_on_boot
 * Description:
 *   Rotate existing log files on boot.
 *   First scan for existing log files, then rotate them.
 *   If 9_syslog.txt exists, delete it.
 *   Then rename existing files: max->max+1, max-1->max, ..., 0->1.
 *   After rotation, new logs will be written to 0_syslog.txt.
 ****************************************************************************/
static void syslog_file_rotate_on_boot(void)
{
  char src_path[64];
  char dst_path[64];
  int i;
  int max_index = -1;
    syslog(LOG_INFO, "SYSLOG file register_syslog_channel: 0 \n");
  /* Scan for existing log files to find the maximum index */
  for (i = 0; i < SYSLOG_MAX_LOGS; i++)
    {
      snprintf(src_path, sizeof(src_path), "%s/%d_syslog.txt", SYSLOG_LOG_DIR, i);
      if (access(src_path, F_OK) == 0)
        {
          max_index = i;
        }
    }

  /* No existing log files, nothing to rotate */
  if (max_index < 0)
    {
      return;
    }

  /* If we have 10 log files (0-9), delete the oldest one (9) */
  if (max_index == SYSLOG_MAX_LOGS - 1)
    {
      snprintf(src_path, sizeof(src_path), "%s/%d_syslog.txt", SYSLOG_LOG_DIR, SYSLOG_MAX_LOGS - 1);
      remove(src_path);
      max_index = SYSLOG_MAX_LOGS - 2;  /* Now max is 8 */
    }

  /* Rotate existing files from max_index down to 0 */
  for (i = max_index; i >= 0; i--)
    {
      snprintf(src_path, sizeof(src_path), "%s/%d_syslog.txt", SYSLOG_LOG_DIR, i);
      snprintf(dst_path, sizeof(dst_path), "%s/%d_syslog.txt", SYSLOG_LOG_DIR, i + 1);
      rename(src_path, dst_path);
    }
}

void register_syslog_channel(void)
{
  int ret = 0;
  char log_path[64];
  struct syslog_channel_s *channel = NULL;
  syslog(LOG_INFO, "Start to initialize /data partition\n");
  ret = mount("/dev/usrdata", "/data", "yaffs", 0, NULL);
  if (ret < 0 && errno != EBUSY) {
    syslog(LOG_ERR, "ERROR: Failed to mount /data: %d\n", errno);
  } else {
    mkdir(SYSLOG_LOG_DIR, 0755);
    syslog_file_rotate_on_boot();


    snprintf(log_path, sizeof(log_path), "%s/0_syslog.txt", SYSLOG_LOG_DIR);

    channel = syslog_file_channel(log_path);
    if (channel != NULL) {
      syslog(LOG_INFO, "SYSLOG file channel initialized: %s\n", log_path);
      //work_queue(HPWORK, &g_syslog_flush_work, syslog_flush_delayed, NULL, SEC2TICK(20));
    } else {
      syslog(LOG_ERR, "ERROR: Failed to initialize syslog file channel\n");
    }
  }
}