/****************************************************************************
 * vendor/allwinnertech/boards/r528/drivers/gt911_iic_touch.c
 *
 * GT911 IIC Touchscreen Driver for R528 Platform
 * GT911 capacitive touch controller driver
 *
 ****************************************************************************/

#include <nuttx/config.h>
#ifndef OPEN_MAX
#define OPEN_MAX 256
#endif
#ifndef CLOCK_MAX
#define CLOCK_MAX 4294967295U
#endif

#include <stdbool.h>
#include <stdint.h>
#include <debug.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/touchscreen.h>
#include <nuttx/wqueue.h>
#include <nuttx/semaphore.h>

#include <arch/board/board.h>
#include "aw_common.h"
#include "hal_gpio.h"
#include "gt911_iic_touch.h"

#define POLL_MINDELAY  (2)
#define POLL_MAXDELAY  (8)
#define POLL_INCREMENT (2)

#define RST GPIOB(4)
#define INT GPIOB(5)
#define GPIO_SET(pin, val) hal_gpio_set_data(pin,val)

#define TOUCH_NO_DATA 0
#define TOUCH_INVALID (1 << 0)
#define TOUCH_VALID   (1 << 1)
#define TOUCH_EVENT_MASK (0x00)
#define TOUCH_COUNT 0x0F
#define GT911_TOUCH_POINTS 1
#define HIGH true
#define LOW false
#define GT911_TOUCH_POLLMODE

#ifndef CONFIG_GT911_TOUCH_ROTATION
#  define CONFIG_GT911_TOUCH_ROTATION 0
#endif

#ifndef CONFIG_GT911_RAW_X_MIN
#  define CONFIG_GT911_RAW_X_MIN 0
#endif

#ifndef CONFIG_GT911_RAW_X_MAX
#  define CONFIG_GT911_RAW_X_MAX (GT911_LCD_WIDTH - 1)
#endif

#ifndef CONFIG_GT911_RAW_Y_MIN
#  define CONFIG_GT911_RAW_Y_MIN 0
#endif

#ifndef CONFIG_GT911_RAW_Y_MAX
#  define CONFIG_GT911_RAW_Y_MAX (GT911_LCD_HEIGHT - 1)
#endif

#ifndef CONFIG_GT911_MOVE_THRESHOLD
#  define CONFIG_GT911_MOVE_THRESHOLD 4
#endif

#ifndef CONFIG_GT911_TOUCH_DEBUG_MOVE_STEP
#  define CONFIG_GT911_TOUCH_DEBUG_MOVE_STEP 8
#endif

enum{
  ERROR_TRANSFER  = 1,
  ERROR_READ,
  ERROR_SLAVE,
  ERROR_REGISTERED,
  ERROR_REGISTER,
  ERROR_CONTROL,
  ERROR_IRQ,
};

/* GT911 touch device instance */
struct gt911_touch_dev_s
{
  struct touch_lowerhalf_s lower;         /* Standard touch lower half */
  uint8_t touch_buf[GT911_TOUCH_DATA_LEN]; /* Raw touch data buffer */
  FAR struct i2c_master_s *i2c;           /* I2C driver instance */
  uint32_t  frequency;                    /* Current I2C frequency */
  struct work_s work;                     /* Work queue for touch processing */
  bool touch_valid;                       /* Valid touch data flag */
  uint8_t i2c_addr;                       /* Detected I2C address */
  sem_t waitsem;                          /* Semaphore for ISR synchronization */
#ifdef GT911_TOUCH_POLLMODE
  uint32_t poll_interval;                 /* Polling interval in milliseconds */
#else
  uint32_t irq;                           /* Interrupt line for GT911 */
#endif
  uint8_t last_state;
  bool down_pending;
  int16_t last_x;
  int16_t last_y;
  int16_t pending_x;
  int16_t pending_y;
  uint8_t pending_id;
  uint16_t pending_pressure;
#ifdef CONFIG_GT911_TOUCH_DEBUG
  int16_t last_log_x;
  int16_t last_log_y;
  uint32_t debug_seq;
#endif
};

extern void up_udelay(useconds_t microseconds);

static int gt911_detect_controller(FAR struct gt911_touch_dev_s *priv);
/* GT911 touch device instance */
static struct gt911_touch_dev_s g_gt911_touch = {
  .lower.maxpoint = 1,
  .lower.control = NULL,
  .lower.write = NULL,
#ifdef GT911_TOUCH_POLLMODE
  .poll_interval = POLL_MINDELAY,
#endif
  .frequency = GT911_I2C_FREQUENCY,
  .last_state = TOUCH_INVALID,
  .down_pending = false,
  .last_x = 0,
  .last_y = 0,
  .pending_x = 0,
  .pending_y = 0,
  .pending_id = 0,
  .pending_pressure = 0,
#ifdef CONFIG_GT911_TOUCH_DEBUG
  .last_log_x = -1,
  .last_log_y = -1,
  .debug_seq = 0,
#endif
};

static uint8_t data[SIZEOF_TOUCH_SAMPLE_S(GT911_TOUCH_POINTS)];
static void gt911_worker(FAR void *arg);

static int gt911_clamp_int(int value, int min, int max)
{
  if (value < min)
    {
      return min;
    }

  if (value > max)
    {
      return max;
    }

  return value;
}

static int gt911_scale_axis(int raw, int raw_min, int raw_max, int out_max)
{
  int range = raw_max - raw_min;
  int value;

  if (out_max <= 0)
    {
      return 0;
    }

  if (range == 0)
    {
      return 0;
    }

  raw = gt911_clamp_int(raw, raw_min, raw_max);
  value = ((raw - raw_min) * out_max + (range / 2)) / range;

  return gt911_clamp_int(value, 0, out_max);
}

static void gt911_get_panel_size(FAR int *width, FAR int *height)
{
  if (CONFIG_GT911_TOUCH_ROTATION == 90 ||
      CONFIG_GT911_TOUCH_ROTATION == 270)
    {
      *width = GT911_LCD_HEIGHT;
      *height = GT911_LCD_WIDTH;
    }
  else
    {
      *width = GT911_LCD_WIDTH;
      *height = GT911_LCD_HEIGHT;
    }
}

static void gt911_map_point(uint16_t raw_x, uint16_t raw_y,
                            FAR int *map_x, FAR int *map_y,
                            FAR int *out_x, FAR int *out_y)
{
  int panel_width;
  int panel_height;
  int x;
  int y;

  gt911_get_panel_size(&panel_width, &panel_height);

  x = gt911_scale_axis(raw_x, CONFIG_GT911_RAW_X_MIN,
                       CONFIG_GT911_RAW_X_MAX, panel_width - 1);
  y = gt911_scale_axis(raw_y, CONFIG_GT911_RAW_Y_MIN,
                       CONFIG_GT911_RAW_Y_MAX, panel_height - 1);

#ifdef CONFIG_GT911_SWAP_XY
  {
    int tmp = x;
    x = y;
    y = tmp;
  }
#endif

#ifdef CONFIG_GT911_INVERT_X
  x = panel_width - 1 - x;
#endif

#ifdef CONFIG_GT911_INVERT_Y
  y = panel_height - 1 - y;
#endif

  *map_x = gt911_clamp_int(x, 0, panel_width - 1);
  *map_y = gt911_clamp_int(y, 0, panel_height - 1);

  switch (CONFIG_GT911_TOUCH_ROTATION)
    {
      case 90:
        x = panel_height - 1 - *map_y;
        y = *map_x;
        break;

      case 180:
        x = panel_width - 1 - *map_x;
        y = panel_height - 1 - *map_y;
        break;

      case 270:
        x = *map_y;
        y = panel_width - 1 - *map_x;
        break;

      case 0:
      default:
        x = *map_x;
        y = *map_y;
        break;
    }

  *out_x = gt911_clamp_int(x, 0, GT911_LCD_WIDTH - 1);
  *out_y = gt911_clamp_int(y, 0, GT911_LCD_HEIGHT - 1);
}

#ifdef CONFIG_GT911_TOUCH_DEBUG
static void gt911_touch_debug(FAR struct gt911_touch_dev_s *priv,
                              FAR const char *event, uint16_t raw_x,
                              uint16_t raw_y, int map_x, int map_y,
                              int out_x, int out_y, bool force)
{
  int dx = 0;
  int dy = 0;
  int swap_xy = 0;
  int invert_x = 0;
  int invert_y = 0;

#ifdef CONFIG_GT911_SWAP_XY
  swap_xy = 1;
#endif
#ifdef CONFIG_GT911_INVERT_X
  invert_x = 1;
#endif
#ifdef CONFIG_GT911_INVERT_Y
  invert_y = 1;
#endif

  if (priv->last_log_x >= 0 && priv->last_log_y >= 0)
    {
      dx = out_x - priv->last_log_x;
      dy = out_y - priv->last_log_y;
    }

  if (!force && abs(dx) < CONFIG_GT911_TOUCH_DEBUG_MOVE_STEP &&
      abs(dy) < CONFIG_GT911_TOUCH_DEBUG_MOVE_STEP)
    {
      return;
    }

  priv->last_log_x = out_x;
  priv->last_log_y = out_y;
  priv->debug_seq++;

  iinfo("GT911_TOUCH[%lu] %s raw=(%u,%u) map=(%d,%d) out=(%d,%d) "
        "d=(%d,%d) src=%dx%d lcd=%dx%d rot=%d flags=swap:%d invx:%d "
        "invy:%d\n",
        (unsigned long)priv->debug_seq, event, raw_x, raw_y, map_x, map_y,
        out_x, out_y, dx, dy, CONFIG_GT911_RAW_X_MAX -
        CONFIG_GT911_RAW_X_MIN + 1, CONFIG_GT911_RAW_Y_MAX -
        CONFIG_GT911_RAW_Y_MIN + 1, GT911_LCD_WIDTH, GT911_LCD_HEIGHT,
        CONFIG_GT911_TOUCH_ROTATION, swap_xy, invert_x, invert_y);
}
#else
#  define gt911_touch_debug(priv, event, raw_x, raw_y, map_x, map_y, \
                            out_x, out_y, force)
#endif

static int gt911_i2c_read(FAR struct gt911_touch_dev_s *priv,
                              uint16_t regaddr, FAR uint8_t *buffer, size_t buflen)
{
  struct i2c_msg_s msg[2];
  uint8_t addr_buf[2];
  int ret;

  DEBUGASSERT(priv && priv->i2c);

  if (!priv->i2c) {
    ierr("fisker: ERROR - I2C instance is NULL!\n");
    return -EINVAL;
  }

  /* GT911 uses 16-bit register addresses in big-endian format */
  addr_buf[0] = (regaddr >> 8) & 0xFF;   /* High byte */
  addr_buf[1] = regaddr & 0xFF;          /* Low byte */

  /* Set up the address write operation */
  msg[0].frequency = priv->frequency;
  msg[0].addr      = priv->i2c_addr;
  msg[0].flags     = 0;
  msg[0].buffer    = addr_buf;
  msg[0].length    = 2;

  /* Set up the data read operation */
  msg[1].frequency = priv->frequency;
  msg[1].addr      = priv->i2c_addr;
  msg[1].flags     = I2C_M_READ;
  msg[1].buffer    = buffer;
  msg[1].length    = buflen;

  ret = I2C_TRANSFER(priv->i2c, msg, 2);
  if (ret)
    {
      ierr("ERROR: GT911 I2C read failed: %d\n", ret);
      return ERROR_TRANSFER;
    }
  return OK;
}

static int gt911_i2c_write(FAR struct gt911_touch_dev_s *priv,
                               uint16_t regaddr, uint8_t value)
{
  struct i2c_msg_s msg;
  uint8_t dat[3];
  int ret;

  DEBUGASSERT(priv && priv->i2c);

  /* GT911 uses 16-bit register addresses in big-endian format */
  dat[0] = (regaddr >> 8) & 0xFF;   /* High byte */
  dat[1] = regaddr & 0xFF;          /* Low byte */
  dat[2] = value;                   /* Data byte */

  /* Set up the I2C message */
  msg.frequency = priv->frequency;
  msg.addr      = priv->i2c_addr;
  msg.flags     = 0;
  msg.buffer    = dat;
  msg.length    = 3;

  ret = I2C_TRANSFER(priv->i2c, &msg, 1);
  if (ret < 0)
    {
      ierr("ERROR: GT911 I2C write failed: %d\n", ret);
      return ERROR_TRANSFER;
    }

  return ret;
}

static int gt911_detect_controller(FAR struct gt911_touch_dev_s *priv)
{
  uint8_t product_id[4];
  int ret;

  memset(product_id, 0, sizeof(product_id));
  ret = gt911_i2c_read(priv, GT911_REG_PRODUCT_ID, product_id, 4);
  if(strcmp((char*)product_id,"911")){
    ierr("fisker: Failed to read GT911 product ID: %d\n", ret);
    return ERROR_READ;
  }
  return OK;
}

static void gt911_touch_store_pending(FAR struct gt911_touch_dev_s *priv,
                                      FAR struct touch_point_s *point)
{
  priv->pending_x = point->x;
  priv->pending_y = point->y;
  priv->pending_id = point->id;
  priv->pending_pressure = point->pressure;
  priv->down_pending = true;
}

static void gt911_touch_load_pending(FAR struct gt911_touch_dev_s *priv,
                                     FAR struct touch_point_s *point)
{
  point->x = priv->pending_x;
  point->y = priv->pending_y;
  point->id = priv->pending_id;
  point->pressure = priv->pending_pressure;
  point->timestamp = touch_get_time();
}

static void gt911_touch_process_event_one(uint8_t touch_state, FAR struct gt911_touch_dev_s *priv, FAR struct touch_sample_s *sample){

  if(touch_state == TOUCH_NO_DATA)
    {
      return;
    }

  sample->npoints = 1;
  sample->point[0].flags &= TOUCH_EVENT_MASK;

  if(touch_state == TOUCH_VALID){

    switch(priv->last_state){

      case TOUCH_INVALID:

        if (!priv->down_pending)
          {
            gt911_touch_store_pending(priv, &sample->point[0]);
            return;
          }

        sample->point[0].flags |= TOUCH_DOWN | TOUCH_ID_VALID |
                                  TOUCH_POS_VALID |
                                  TOUCH_PRESSURE_VALID;
        priv->last_state = TOUCH_VALID;
        priv->down_pending = false;
        priv->last_x = sample->point[0].x;
        priv->last_y = sample->point[0].y;
        touch_event(priv->lower.priv, sample);
        break;

      case TOUCH_VALID:

        if(sample->point[0].x == priv->last_x &&
           sample->point[0].y == priv->last_y)
          {
            break;
          }

        sample->point[0].flags |= TOUCH_MOVE | TOUCH_ID_VALID |
                                  TOUCH_POS_VALID |
                                  TOUCH_PRESSURE_VALID;
        priv->last_x = sample->point[0].x;
        priv->last_y = sample->point[0].y;
        touch_event(priv->lower.priv, sample);
        break;
    }
  }
  else{

    switch(priv->last_state){

      case TOUCH_INVALID:

       if (priv->down_pending)
         {
           gt911_touch_load_pending(priv, &sample->point[0]);
           sample->point[0].flags |= TOUCH_DOWN | TOUCH_ID_VALID |
                                     TOUCH_POS_VALID |
                                     TOUCH_PRESSURE_VALID;
           touch_event(priv->lower.priv, sample);

           sample->point[0].pressure = 0;
           sample->point[0].timestamp = touch_get_time();
           sample->point[0].flags = TOUCH_UP | TOUCH_ID_VALID |
                                    TOUCH_POS_VALID;
           priv->down_pending = false;
           touch_event(priv->lower.priv, sample);
         }
       else
         {
           sample->npoints = 0;
           memset(&sample->point[0], 0, sizeof(sample->point[0]));
         }
       break;

      case TOUCH_VALID:

       sample->point[0].id = 0;
       sample->point[0].x = priv->last_x;
       sample->point[0].y = priv->last_y;
       sample->point[0].pressure = 0;
       sample->point[0].timestamp = touch_get_time();
       sample->point[0].flags |= TOUCH_UP | TOUCH_ID_VALID |
                                  TOUCH_POS_VALID;
       priv->last_state = TOUCH_INVALID;
       priv->down_pending = false;
       priv->last_x = 0;
       priv->last_y = 0;
       touch_event(priv->lower.priv, sample);
       break;
    }
  }
}

static int gt911_touch_process_data(FAR struct gt911_touch_dev_s *priv, FAR struct touch_sample_s *sample)
{
  FAR struct gt911_touch_data_s *raw = (FAR struct gt911_touch_data_s *)priv->touch_buf;
  if(!TOUCH_POINT_GET_STATUS(raw->status_id)){

    return TOUCH_NO_DATA;
  }

  if(!TOUCH_POINT_GET_NUM(raw->status_id) || TOUCH_POINT_GET_LARGE(raw->status_id)){

    gt911_i2c_write(priv, GT911_REG_COORD_ADDR, 0x00);
    return TOUCH_INVALID;
  }
  sample->npoints = GT911_TOUCH_POINTS;

  for (int i = 0; i < sample->npoints; i++)
  {
    uint16_t raw_x = TOUCH_POINT_GET_X(raw->touch[i]);
    uint16_t raw_y = TOUCH_POINT_GET_Y(raw->touch[i]);
    int map_x;
    int map_y;
    int out_x;
    int out_y;
#ifdef CONFIG_GT911_TOUCH_DEBUG
    const char *event = "MOVE";
#endif

    gt911_map_point(raw_x, raw_y, &map_x, &map_y, &out_x, &out_y);

    sample->point[i].x        = out_x;
    sample->point[i].y        = out_y;
    sample->point[i].id       = TOUCH_POINT_GET_ID(raw->touch[i]);
    sample->point[i].h        = 0;
    sample->point[i].w        = 0;
    sample->point[i].pressure = TOUCH_POINT_GET_SIZE(raw->touch[i]);
    sample->point[i].timestamp = touch_get_time();

#ifdef CONFIG_GT911_TOUCH_DEBUG
    if (priv->last_state == TOUCH_INVALID)
      {
        event = "DOWN";
      }

    gt911_touch_debug(priv, event, raw_x, raw_y, map_x, map_y, out_x, out_y,
                      priv->last_state == TOUCH_INVALID);
#endif

    memset(&raw->touch[i], 0, sizeof(raw->touch[i]));
  }

  raw->status_id = 0;
  gt911_i2c_write(priv, GT911_REG_COORD_ADDR, 0x00);
  return TOUCH_VALID;
}

static int gt911_control_initialize(void){

  int ret;
  FAR struct gt911_touch_dev_s *priv = &g_gt911_touch;

  printf("fisker: Initializing GT911 GPIO pins...\n");

  /* config GPIO Pin for RST and INT */
  hal_gpio_pinmux_set_function(RST, GPIO_MUXSEL_OUT);
  hal_gpio_set_driving_level(RST, GPIO_DRIVING_LEVEL3);
  hal_gpio_set_direction(RST, GPIO_DIRECTION_OUTPUT);

  hal_gpio_pinmux_set_function(INT, GPIO_MUXSEL_OUT);
  hal_gpio_set_driving_level(INT, GPIO_DRIVING_LEVEL3);
  hal_gpio_set_direction(INT, GPIO_DIRECTION_OUTPUT);

  /* try addr 0x5D */
  printf("fisker: Trying GT911 address 0x5D with specific reset sequence...\n");
  priv->i2c_addr = GT911_I2C_ADDR_1;

  hal_gpio_set_data(RST, LOW);
  hal_gpio_set_data(INT, LOW);
  up_udelay(20000);                 /* 复位保持20ms */
  hal_gpio_set_data(RST, HIGH);
  up_udelay(50000);                 /* 等待50ms芯片启动 */

#ifndef GT911_TOUCH_POLLMODE
  hal_gpio_set_data(INT, HIGH);
  hal_gpio_pinmux_set_function(INT, GPIO_MUXSEL_IN);
  hal_gpio_set_direction(INT, GPIO_DIRECTION_INPUT);
  hal_gpio_set_pull(INT, GPIO_PULL_DOWN_DISABLED);
  up_udelay(20000);
#endif

  printf("fisker: Testing communication with address 0x%02X...\n", priv->i2c_addr);

  ret = gt911_detect_controller(priv);

  if(ret) {
    printf("fisker: Address 0x5D failed, trying 0x14 with different sequence...\n");
    /* 尝试地址0x14的时序 */
    priv->i2c_addr = GT911_I2C_ADDR_2;
    /* 重新配置INT为输出 */
    hal_gpio_pinmux_set_function(INT, GPIO_MUXSEL_OUT);
    hal_gpio_set_direction(INT, GPIO_DIRECTION_OUTPUT);

    hal_gpio_set_data(RST, LOW);
    hal_gpio_set_data(INT, HIGH);  /* INT=HIGH选择0x14地址 */
    up_udelay(20000);

    hal_gpio_set_data(RST, HIGH);
    up_udelay(5000);
    hal_gpio_set_data(INT, LOW);
    up_udelay(50000);

    ret = gt911_detect_controller(priv);
    if(ret != OK) {
      ierr("fisker: GT911 detection failed at both addresses!\n");
      return ERROR_SLAVE;
    }
  }

  return OK;
}

static void gt911_worker(FAR void *arg){

  FAR struct gt911_touch_dev_s *priv = (FAR struct gt911_touch_dev_s *)arg;
  FAR struct touch_sample_s *sample = (FAR struct touch_sample_s *)data;
  uint8_t touch_state;
  int ret;

  DEBUGASSERT(priv);
  memset(sample, 0, SIZEOF_TOUCH_SAMPLE_S(GT911_TOUCH_POINTS));

  ret = gt911_i2c_read(priv, GT911_REG_COORD_ADDR, priv->touch_buf,
                           1 + (GT911_TOUCH_POINTS * GT911_POINT_SIZE));  //读到的第一个字节是header信息
  if(ret){
    ierr("fisker: I2C read failed\n");
#ifdef GT911_TOUCH_POLLMODE
    goto error_read;
#else
    ierr("ERROR: Failed to read\n");
    return;
#endif
  }

  touch_state = gt911_touch_process_data(priv, sample);
  gt911_touch_process_event_one(touch_state, priv, sample);

#ifdef GT911_TOUCH_POLLMODE
  if(touch_state == TOUCH_VALID || priv->down_pending)
    priv->poll_interval = POLL_MINDELAY;
  else if(priv->last_state == TOUCH_INVALID)
    priv->poll_interval += POLL_INCREMENT;
  else
    priv->poll_interval = POLL_MINDELAY;

  if(priv->poll_interval < POLL_MINDELAY)
    priv->poll_interval = POLL_MINDELAY;
  if(priv->poll_interval> POLL_MAXDELAY)
    priv->poll_interval = POLL_MAXDELAY;

error_read:
  work_queue(HPWORK, &priv->work, gt911_worker, priv, priv->poll_interval);
#endif
}

#ifndef GT911_TOUCH_POLLMODE
static hal_irqreturn_t gt911_interrupt_handler(FAR void *arg){

      printf("fisker,interrupt_handler call !\n");

  int ret = 0;
  FAR struct gt911_touch_dev_s *priv = (FAR struct gt911_touch_dev_s *)arg;
  DEBUGASSERT(priv->work.worker == NULL);

  ret = work_queue(HPWORK, &priv->work, gt911_worker, priv, 0);
  if (ret != 0)
    {
      ierr("ERROR: Failed to queue work: %d\n", ret);
      return HAL_IRQ_ERR;
    }
  return HAL_IRQ_OK;
}

#endif
/*
int gt911_register(FAR struct i2c_master_s *dev){

  FAR struct gt911_touch_dev_s *priv = &g_gt911_touch;
  iinfo("GT911: mount i2c_master_s instance!\n");
  priv->i2c = dev;
  priv->is_registered = true;
  return OK;
}
int gt911_unregister(void){

  FAR struct gt911_touch_dev_s *priv = &g_gt911_touch;
  priv->is_registered = false;
  priv->i2c = NULL;
  return OK;
}
*/
int gt911_register(FAR const char *devpath,FAR struct i2c_master_s *dev)
{
  FAR struct gt911_touch_dev_s *priv = &g_gt911_touch;
  int ret;

  if(dev == NULL){
    ierr("GT911: ERROR: i2c_master_s instance is NULL\n");
    return ERROR_REGISTERED;
  }
  priv->i2c = dev;

  if(gt911_control_initialize()){
    ierr("GT911: ERROR: Failed to initialize control\n");
    return ERROR_CONTROL;
    }

#ifndef  GT911_TOUCH_POLLMODE
  hal_gpio_to_irq(INT, &priv->irq);
  hal_gpio_irq_request(priv->irq, gt911_interrupt_handler, IRQ_TYPE_EDGE_RISING, priv);
  nxsem_init(&priv->waitsem, 0, 0);
#endif
  ret = touch_register(&priv->lower, devpath, GT911_TOUCH_POINTS);
  if (ret < 0)
    {
      ierr("GT911: ERROR: Failed to register touch driver: %d\n", ret);
      goto errout;
    }
  iinfo("GT911: IIC touchscreen successfully initialized and registered\n");
#ifdef GT911_TOUCH_POLLMODE
  work_queue(HPWORK, &priv->work, gt911_worker, priv, priv->poll_interval);
#else
  hal_gpio_irq_enable(priv->irq);
#endif
  return OK;

errout:
  nxsem_destroy(&priv->waitsem);
  return ERROR_REGISTER;
}

void gt911_unregister(FAR const char *devpath){

  FAR struct gt911_touch_dev_s *priv = &g_gt911_touch;
#ifndef GT911_TOUCH_POLLMODE
  hal_gpio_irq_disable(priv->irq);
  hal_gpio_irq_free(priv->irq);
#endif
  touch_unregister(&priv->lower, devpath);
  priv->i2c = NULL;
}
