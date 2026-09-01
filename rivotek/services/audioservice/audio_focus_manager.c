#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <debug.h>

#include <nuttx/mutex.h>

#include "audio_focus_manager.h"

#define AUDIO_FOCUS_PENDING_SLOTS 4
#define AUDIO_FOCUS_EVENT_QUEUE   8

struct audio_focus_callback_slot_s
{
  pid_t pid;
  audio_focus_change_cb_t change_cb;
  audio_focus_lost_cb_t lost_cb;
};

struct audio_focus_pending_slot_s
{
  pid_t pid;
  audio_focus_change_cb_t change_cb;
  audio_focus_lost_cb_t lost_cb;
};

struct audio_focus_event_s
{
  pid_t pid;
  int change;
  audio_focus_change_cb_t change_cb;
  audio_focus_lost_cb_t lost_cb;
};

struct audio_focus_state_s
{
  mutex_t lock;
  sem_t event_sem;
  pthread_t worker;
  bool initialized;

  int owner;
  pid_t owner_pid;
  unsigned int owner_refcount;

  bool has_suspended_music;
  pid_t suspended_music_pid;
  unsigned int suspended_music_refcount;

  struct audio_focus_callback_slot_s callback[3];
  struct audio_focus_pending_slot_s pending[AUDIO_FOCUS_PENDING_SLOTS];

  struct audio_focus_event_s event_queue[AUDIO_FOCUS_EVENT_QUEUE];
  unsigned int event_count;
};

static struct audio_focus_state_s g_audio_focus;
static pthread_mutex_t g_audio_focus_init_lock = PTHREAD_MUTEX_INITIALIZER;

static bool audio_focus_type_valid(int type)
{
  return type == AUDIO_FOCUS_AI || type == AUDIO_FOCUS_MUSIC;
}

static bool audio_focus_gain_valid(int gain_type)
{
  return gain_type == AUDIO_FOCUS_GAIN ||
         gain_type == AUDIO_FOCUS_GAIN_TRANSIENT;
}

static bool audio_focus_pid_alive(pid_t pid)
{
  if (pid <= 0)
    {
      return false;
    }

  if (kill(pid, 0) == 0)
    {
      return true;
    }

  return errno == EPERM;
}

static void audio_focus_add_timeout(struct timespec *ts)
{
  ts->tv_nsec += (CONFIG_AUDIOSERVICE_MONITOR_INTERVAL_MS % 1000) * 1000000L;
  ts->tv_sec += CONFIG_AUDIOSERVICE_MONITOR_INTERVAL_MS / 1000;

  if (ts->tv_nsec >= 1000000000L)
    {
      ts->tv_sec += 1;
      ts->tv_nsec -= 1000000000L;
    }
}

static int audio_focus_find_pending_slot(pid_t pid)
{
  int i;

  for (i = 0; i < AUDIO_FOCUS_PENDING_SLOTS; i++)
    {
      if (g_audio_focus.pending[i].pid == pid)
        {
          return i;
        }
    }

  return -1;
}

static int audio_focus_find_free_pending_slot(void)
{
  int i;

  for (i = 0; i < AUDIO_FOCUS_PENDING_SLOTS; i++)
    {
      if (g_audio_focus.pending[i].pid == 0)
        {
          return i;
        }
    }

  return -1;
}

static int audio_focus_find_callback_owner_by_pid(pid_t pid)
{
  int type;

  for (type = AUDIO_FOCUS_AI; type <= AUDIO_FOCUS_MUSIC; type++)
    {
      if (g_audio_focus.callback[type].pid == pid)
        {
          return type;
        }
    }

  return AUDIO_FOCUS_IDLE;
}

static bool audio_focus_enqueue_event_locked(
  FAR const struct audio_focus_callback_slot_s *slot,
  int change)
{
  if (slot == NULL || slot->pid <= 0)
    {
      return false;
    }

  if (slot->change_cb == NULL && slot->lost_cb == NULL)
    {
      return false;
    }

  if (g_audio_focus.event_count >= AUDIO_FOCUS_EVENT_QUEUE)
    {
      syslog(LOG_WARNING, "audioservice: focus event queue full, drop change=%d\n",
             change);
      return false;
    }

  g_audio_focus.event_queue[g_audio_focus.event_count].pid = slot->pid;
  g_audio_focus.event_queue[g_audio_focus.event_count].change = change;
  g_audio_focus.event_queue[g_audio_focus.event_count].change_cb = slot->change_cb;
  g_audio_focus.event_queue[g_audio_focus.event_count].lost_cb = slot->lost_cb;
  g_audio_focus.event_count++;
  return true;
}

static void audio_focus_assign_pending_locked(pid_t pid, int type)
{
  int idx = audio_focus_find_pending_slot(pid);

  if (idx < 0 || !audio_focus_type_valid(type))
    {
      return;
    }

  g_audio_focus.callback[type].pid = pid;
  g_audio_focus.callback[type].change_cb = g_audio_focus.pending[idx].change_cb;
  g_audio_focus.callback[type].lost_cb = g_audio_focus.pending[idx].lost_cb;

  g_audio_focus.pending[idx].pid = 0;
  g_audio_focus.pending[idx].change_cb = NULL;
  g_audio_focus.pending[idx].lost_cb = NULL;
}

static void audio_focus_set_owner_locked(int type, pid_t pid, unsigned int refcount)
{
  g_audio_focus.owner = type;
  g_audio_focus.owner_pid = pid;
  g_audio_focus.owner_refcount = refcount;
}

static void audio_focus_restore_music_locked(void)
{
  struct audio_focus_callback_slot_s *music_slot =
    &g_audio_focus.callback[AUDIO_FOCUS_MUSIC];

  audio_focus_set_owner_locked(AUDIO_FOCUS_MUSIC,
                               g_audio_focus.suspended_music_pid,
                               g_audio_focus.suspended_music_refcount);

  g_audio_focus.has_suspended_music = false;
  g_audio_focus.suspended_music_pid = 0;
  g_audio_focus.suspended_music_refcount = 0;

  audio_focus_enqueue_event_locked(music_slot, AUDIO_FOCUS_CHANGE_GAIN);
}

static void audio_focus_clear_dead_slots_locked(void)
{
  int type;
  int i;

  if (g_audio_focus.owner != AUDIO_FOCUS_IDLE &&
      !audio_focus_pid_alive(g_audio_focus.owner_pid))
    {
      if (g_audio_focus.owner == AUDIO_FOCUS_AI &&
          g_audio_focus.has_suspended_music &&
          audio_focus_pid_alive(g_audio_focus.suspended_music_pid))
        {
          syslog(LOG_INFO,
                 "audioservice: dead AI owner, restore music pid=%d\n",
                 g_audio_focus.suspended_music_pid);
          audio_focus_restore_music_locked();
        }
      else
        {
          syslog(LOG_INFO,
                 "audioservice: reclaim dead owner pid=%d type=%d\n",
                 g_audio_focus.owner_pid,
                 g_audio_focus.owner);
          audio_focus_set_owner_locked(AUDIO_FOCUS_IDLE, 0, 0);
          g_audio_focus.has_suspended_music = false;
          g_audio_focus.suspended_music_pid = 0;
          g_audio_focus.suspended_music_refcount = 0;
        }
    }

  if (g_audio_focus.has_suspended_music &&
      !audio_focus_pid_alive(g_audio_focus.suspended_music_pid))
    {
      g_audio_focus.has_suspended_music = false;
      g_audio_focus.suspended_music_pid = 0;
      g_audio_focus.suspended_music_refcount = 0;
    }

  for (type = AUDIO_FOCUS_AI; type <= AUDIO_FOCUS_MUSIC; type++)
    {
      if (g_audio_focus.callback[type].pid > 0 &&
          !audio_focus_pid_alive(g_audio_focus.callback[type].pid))
        {
          g_audio_focus.callback[type].pid = 0;
          g_audio_focus.callback[type].change_cb = NULL;
          g_audio_focus.callback[type].lost_cb = NULL;
        }
    }

  for (i = 0; i < AUDIO_FOCUS_PENDING_SLOTS; i++)
    {
      if (g_audio_focus.pending[i].pid > 0 &&
          !audio_focus_pid_alive(g_audio_focus.pending[i].pid))
        {
          g_audio_focus.pending[i].pid = 0;
          g_audio_focus.pending[i].change_cb = NULL;
          g_audio_focus.pending[i].lost_cb = NULL;
        }
    }
}

static bool audio_focus_drain_events(void)
{
  struct audio_focus_event_s events[AUDIO_FOCUS_EVENT_QUEUE];
  unsigned int count;
  bool has_event = false;
  unsigned int i;

  if (nxmutex_lock(&g_audio_focus.lock) < 0)
    {
      return false;
    }

  audio_focus_clear_dead_slots_locked();
  count = g_audio_focus.event_count;
  if (count > 0)
    {
      memcpy(events, g_audio_focus.event_queue,
             sizeof(struct audio_focus_event_s) * count);
      g_audio_focus.event_count = 0;
      has_event = true;
    }

  nxmutex_unlock(&g_audio_focus.lock);

  for (i = 0; i < count; i++)
    {
      if (!audio_focus_pid_alive(events[i].pid))
        {
          continue;
        }

      if (events[i].change_cb != NULL)
        {
          events[i].change_cb(events[i].change);
        }

      if (events[i].lost_cb != NULL &&
          events[i].change < 0)
        {
          events[i].lost_cb();
        }
    }

  return has_event;
}

static void *audio_focus_worker(void *arg)
{
  (void)arg;

  for (;;)
    {
      struct timespec ts;

      clock_gettime(CLOCK_REALTIME, &ts);
      audio_focus_add_timeout(&ts);

      if (sem_timedwait(&g_audio_focus.event_sem, &ts) < 0 &&
          errno != ETIMEDOUT && errno != EINTR)
        {
          syslog(LOG_ERR, "audioservice: sem_timedwait failed: %d\n", errno);
        }

      audio_focus_drain_events();
    }

  return NULL;
}

int audio_focus_manager_init(void)
{
  pthread_attr_t attr;
  struct sched_param param;
  int ret;

  pthread_mutex_lock(&g_audio_focus_init_lock);

  if (g_audio_focus.initialized)
    {
      pthread_mutex_unlock(&g_audio_focus_init_lock);
      return OK;
    }

  memset(&g_audio_focus, 0, sizeof(g_audio_focus));

  ret = nxmutex_init(&g_audio_focus.lock);
  if (ret < 0)
    {
      pthread_mutex_unlock(&g_audio_focus_init_lock);
      return ret;
    }

  ret = sem_init(&g_audio_focus.event_sem, 0, 0);
  if (ret < 0)
    {
      ret = -errno;
      nxmutex_destroy(&g_audio_focus.lock);
      pthread_mutex_unlock(&g_audio_focus_init_lock);
      return ret;
    }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, CONFIG_AUDIOSERVICE_WORKER_STACKSIZE);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  param.sched_priority = CONFIG_AUDIOSERVICE_WORKER_PRIORITY;
  pthread_attr_setschedparam(&attr, &param);

  ret = pthread_create(&g_audio_focus.worker, &attr,
                       audio_focus_worker, NULL);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      sem_destroy(&g_audio_focus.event_sem);
      nxmutex_destroy(&g_audio_focus.lock);
      pthread_mutex_unlock(&g_audio_focus_init_lock);
      return -ret;
    }

  g_audio_focus.initialized = true;
  audio_focus_set_owner_locked(AUDIO_FOCUS_IDLE, 0, 0);

  pthread_mutex_unlock(&g_audio_focus_init_lock);
  return OK;
}

int audio_focus_request_ex(int type, int gain_type)
{
  pid_t pid = getpid();
  bool should_wakeup = false;
  int ret;

  if (!audio_focus_type_valid(type) || !audio_focus_gain_valid(gain_type))
    {
      return -EINVAL;
    }

  ret = audio_focus_manager_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_audio_focus.lock);
  if (ret < 0)
    {
      return ret;
    }

  audio_focus_clear_dead_slots_locked();
  audio_focus_assign_pending_locked(pid, type);

  if (g_audio_focus.owner == AUDIO_FOCUS_IDLE)
    {
      audio_focus_set_owner_locked(type, pid, 1);
      nxmutex_unlock(&g_audio_focus.lock);
      return OK;
    }

  if (g_audio_focus.owner == type)
    {
      if (g_audio_focus.owner_pid != pid)
        {
          nxmutex_unlock(&g_audio_focus.lock);
          return -EBUSY;
        }

      g_audio_focus.owner_refcount++;
      nxmutex_unlock(&g_audio_focus.lock);
      return OK;
    }

  if (type == AUDIO_FOCUS_MUSIC)
    {
      nxmutex_unlock(&g_audio_focus.lock);
      return -EBUSY;
    }

  /* AI preempts music */
  if (g_audio_focus.owner == AUDIO_FOCUS_MUSIC)
    {
      struct audio_focus_callback_slot_s *music_slot =
        &g_audio_focus.callback[AUDIO_FOCUS_MUSIC];

      if (gain_type == AUDIO_FOCUS_GAIN_TRANSIENT)
        {
          g_audio_focus.has_suspended_music = true;
          g_audio_focus.suspended_music_pid = g_audio_focus.owner_pid;
          g_audio_focus.suspended_music_refcount = g_audio_focus.owner_refcount;
          should_wakeup |= audio_focus_enqueue_event_locked(
            music_slot, AUDIO_FOCUS_CHANGE_LOSS_TRANSIENT);
        }
      else
        {
          g_audio_focus.has_suspended_music = false;
          g_audio_focus.suspended_music_pid = 0;
          g_audio_focus.suspended_music_refcount = 0;
          should_wakeup |= audio_focus_enqueue_event_locked(
            music_slot, AUDIO_FOCUS_CHANGE_LOSS);
        }

      audio_focus_set_owner_locked(AUDIO_FOCUS_AI, pid, 1);
      audio_focus_assign_pending_locked(pid, AUDIO_FOCUS_AI);
      nxmutex_unlock(&g_audio_focus.lock);

      if (should_wakeup)
        {
          sem_post(&g_audio_focus.event_sem);
        }

      return OK;
    }

  nxmutex_unlock(&g_audio_focus.lock);
  return -EBUSY;
}

int audio_focus_request(int type)
{
  int gain_type;

  if (!audio_focus_type_valid(type))
    {
      return -EINVAL;
    }

  gain_type = (type == AUDIO_FOCUS_AI) ?
              AUDIO_FOCUS_GAIN_TRANSIENT : AUDIO_FOCUS_GAIN;

  return audio_focus_request_ex(type, gain_type);
}

int audio_focus_release(int type)
{
  pid_t pid = getpid();
  bool should_wakeup = false;
  int ret;

  if (!audio_focus_type_valid(type))
    {
      return -EINVAL;
    }

  ret = audio_focus_manager_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_audio_focus.lock);
  if (ret < 0)
    {
      return ret;
    }

  audio_focus_clear_dead_slots_locked();

  if (g_audio_focus.owner != type || g_audio_focus.owner_pid != pid)
    {
      nxmutex_unlock(&g_audio_focus.lock);
      return OK;
    }

  if (g_audio_focus.owner_refcount > 1)
    {
      g_audio_focus.owner_refcount--;
      nxmutex_unlock(&g_audio_focus.lock);
      return OK;
    }

  if (type == AUDIO_FOCUS_AI &&
      g_audio_focus.has_suspended_music &&
      audio_focus_pid_alive(g_audio_focus.suspended_music_pid))
    {
      audio_focus_restore_music_locked();
      should_wakeup = true;
    }
  else
    {
      audio_focus_set_owner_locked(AUDIO_FOCUS_IDLE, 0, 0);
      g_audio_focus.has_suspended_music = false;
      g_audio_focus.suspended_music_pid = 0;
      g_audio_focus.suspended_music_refcount = 0;
    }

  nxmutex_unlock(&g_audio_focus.lock);

  if (should_wakeup)
    {
      sem_post(&g_audio_focus.event_sem);
    }

  return OK;
}

int audio_focus_get_owner(void)
{
  int owner;
  int ret;

  ret = audio_focus_manager_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_audio_focus.lock);
  if (ret < 0)
    {
      return ret;
    }

  audio_focus_clear_dead_slots_locked();
  owner = g_audio_focus.owner;

  nxmutex_unlock(&g_audio_focus.lock);
  return owner;
}

int audio_focus_register_change_cb(audio_focus_change_cb_t cb)
{
  pid_t pid = getpid();
  int type;
  int idx;
  int ret;

  if (cb == NULL)
    {
      return -EINVAL;
    }

  ret = audio_focus_manager_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_audio_focus.lock);
  if (ret < 0)
    {
      return ret;
    }

  audio_focus_clear_dead_slots_locked();

  if (g_audio_focus.owner != AUDIO_FOCUS_IDLE &&
      g_audio_focus.owner_pid == pid)
    {
      g_audio_focus.callback[g_audio_focus.owner].pid = pid;
      g_audio_focus.callback[g_audio_focus.owner].change_cb = cb;
      nxmutex_unlock(&g_audio_focus.lock);
      return OK;
    }

  type = audio_focus_find_callback_owner_by_pid(pid);
  if (type != AUDIO_FOCUS_IDLE)
    {
      g_audio_focus.callback[type].change_cb = cb;
      nxmutex_unlock(&g_audio_focus.lock);
      return OK;
    }

  idx = audio_focus_find_pending_slot(pid);
  if (idx < 0)
    {
      idx = audio_focus_find_free_pending_slot();
    }

  if (idx < 0)
    {
      nxmutex_unlock(&g_audio_focus.lock);
      return -ENOMEM;
    }

  g_audio_focus.pending[idx].pid = pid;
  g_audio_focus.pending[idx].change_cb = cb;
  nxmutex_unlock(&g_audio_focus.lock);
  return OK;
}

int audio_focus_register_lost_cb(audio_focus_lost_cb_t cb)
{
  pid_t pid = getpid();
  int type;
  int idx;
  int ret;

  if (cb == NULL)
    {
      return -EINVAL;
    }

  ret = audio_focus_manager_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_audio_focus.lock);
  if (ret < 0)
    {
      return ret;
    }

  audio_focus_clear_dead_slots_locked();

  if (g_audio_focus.owner != AUDIO_FOCUS_IDLE &&
      g_audio_focus.owner_pid == pid)
    {
      g_audio_focus.callback[g_audio_focus.owner].pid = pid;
      g_audio_focus.callback[g_audio_focus.owner].lost_cb = cb;
      nxmutex_unlock(&g_audio_focus.lock);
      return OK;
    }

  type = audio_focus_find_callback_owner_by_pid(pid);
  if (type != AUDIO_FOCUS_IDLE)
    {
      g_audio_focus.callback[type].lost_cb = cb;
      nxmutex_unlock(&g_audio_focus.lock);
      return OK;
    }

  idx = audio_focus_find_pending_slot(pid);
  if (idx < 0)
    {
      idx = audio_focus_find_free_pending_slot();
    }

  if (idx < 0)
    {
      nxmutex_unlock(&g_audio_focus.lock);
      return -ENOMEM;
    }

  g_audio_focus.pending[idx].pid = pid;
  g_audio_focus.pending[idx].lost_cb = cb;
  nxmutex_unlock(&g_audio_focus.lock);
  return OK;
}
