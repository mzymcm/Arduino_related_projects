/*
 * SSD1306 I2C Framebuffer Driver for Linux
 * Register as /dev/fbX, supports 128x64 monochrome OLED
 * Based on framebuffer subsystem and i2c
 *
 * Author: Combined from various sources, adapted for 6.12 kernel
 * Modified: Page mode, dual bit-order control, fb memory cleared, custom mmap
 * Optimized: Configuration macros for easy customization, corrected orientation
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#define DRIVER_NAME         "ssd1306_fb"
#define SSD1306_I2C_ADDR    0x3C
#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64
#define SSD1306_PAGES       8               // 64 / 8
#define SSD1306_FB_SIZE     1024             // 128 * 64 / 8

/* ==================== Configuration Macros ==================== */
/* Modify these macros before compiling to customize the driver behavior.
   Refer to SSD1306 datasheet for possible values. */

/* Segment remap: 0xA0 = normal (column 0 mapped to SEG0), 0xA1 = horizontal mirror */
#define SSD1306_CONFIG_SEGREMAP     0xA0    /* Normal (disable global mirror) */

/* COM scan direction: 0xC0 = normal (COM0 to COM63), 0xC8 = reverse (COM63 to COM0) */
#define SSD1306_CONFIG_COMSCAN      0xC0    /* Normal scan: framebuffer y=0 = screen top */

/* Charge pump setting: 0x14 = enable, 0x10 = disable */
#define SSD1306_CONFIG_CHARGEPUMP   0x14

/* Contrast value (0x00 to 0xFF) */
#define SSD1306_CONFIG_CONTRAST     0x7F

/* Display clock divide ratio / oscillator frequency (default 0x80) */
#define SSD1306_CONFIG_CLOCKDIV     0x80

/* Multiplex ratio: for 64 rows, use 0x3F */
#define SSD1306_CONFIG_MULTIPLEX    0x3F

/* Display offset (vertical shift) */
#define SSD1306_CONFIG_OFFSET       0x00

/* COM pins hardware configuration (0x12 for typical 128x64) */
#define SSD1306_CONFIG_COMPINS      0x12

/* Pre-charge period (default 0xF1) */
#define SSD1306_CONFIG_PRECHARGE    0xF1

/* VCOMH deselect level (default 0x20) */
#define SSD1306_CONFIG_VCOMDETECT   0x20

/* Memory mode: 0x00 = horizontal, 0x01 = vertical, 0x10 = page (used here) */
#define SSD1306_CONFIG_MEMORYMODE   0x10

/* =============================================================== */

/* I2C commands (standard definitions, not meant to be changed) */
#define SSD1306_SETCONTRAST         0x81
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_DISPLAYALLON        0xA5
#define SSD1306_NORMALDISPLAY       0xA6
#define SSD1306_INVERTDISPLAY       0xA7
#define SSD1306_DISPLAYOFF          0xAE
#define SSD1306_DISPLAYON            0xAF
#define SSD1306_SETDISPLAYOFFSET    0xD3
#define SSD1306_SETCOMPINS          0xDA
#define SSD1306_SETVCOMDETECT       0xDB
#define SSD1306_SETDISPLAYCLOCKDIV  0xD5
#define SSD1306_SETPRECHARGE        0xD9
#define SSD1306_SETMULTIPLEX        0xA8
#define SSD1306_SETLOWCOLUMN        0x00
#define SSD1306_SETHIGHCOLUMN       0x10
#define SSD1306_SETSTARTLINE        0x40
#define SSD1306_MEMORYMODE          0x20
#define SSD1306_COLUMNADDR          0x21
#define SSD1306_PAGEADDR            0x22
#define SSD1306_CHARGEPUMP          0x8D
#define SSD1306_EXTERNALVCC         0x01
#define SSD1306_SWITCHCAPVCC        0x02

/* Orientation commands (optional, for reference) */
#define SSD1306_SEGREMAP_NORMAL      0xA0
#define SSD1306_SEGREMAP_MIRROR      0xA1
#define SSD1306_COMSCAN_NORMAL       0xC0
#define SSD1306_COMSCAN_REVERSE      0xC8

/* Module parameters */
static bool swap_bit_order = false;
module_param(swap_bit_order, bool, 0644);
MODULE_PARM_DESC(swap_bit_order, "Swap bit order in OLED byte (0: bit0 top, 1: bit7 top)");

static bool fb_bit_reverse = false;
module_param(fb_bit_reverse, bool, 0644);
MODULE_PARM_DESC(fb_bit_reverse, "Reverse bit order in fb byte (0: LSB left, 1: MSB left)");

/* Private data structure */
struct ssd1306_device {
    struct i2c_client *client;
    struct fb_info *fb_info;
    struct task_struct *refresh_task;
    wait_queue_head_t wait_queue;
    unsigned char *internal_buffer;    // converted OLED format buffer
    unsigned char *fb_buffer;          // framebuffer buffer pointer (points to fb_info->screen_base)
    struct mutex lock;
    int xres, yres;
    int pages;
    int refresh_rate;                  // refresh interval (ms)
    bool running;
};

/* Fixed screen info */
static struct fb_fix_screeninfo ssd1306_fix = {
    .id           = "ssd1306_fb",
    .type         = FB_TYPE_PACKED_PIXELS,
    .visual       = FB_VISUAL_MONO01,   // monochrome, 0=black, 1=white
    .xpanstep     = 0,
    .ypanstep     = 0,
    .ywrapstep    = 0,
    .line_length  = SSD1306_WIDTH / 8,  // 16 bytes per line
    .accel        = FB_ACCEL_NONE,
};

/* Variable screen info */
static struct fb_var_screeninfo ssd1306_var = {
    .xres           = SSD1306_WIDTH,
    .yres           = SSD1306_HEIGHT,
    .xres_virtual   = SSD1306_WIDTH,
    .yres_virtual   = SSD1306_HEIGHT,
    .bits_per_pixel = 1,                // 1 bpp
    .red            = {0, 1, 0},
    .green          = {0, 1, 0},
    .blue           = {0, 1, 0},
    .transp         = {0, 0, 0},
    .nonstd         = 1,
    .activate       = FB_ACTIVATE_NOW,
    .height         = -1,
    .width          = -1,
    .pixclock       = 0,
    .left_margin    = 0,
    .right_margin   = 0,
    .upper_margin   = 0,
    .lower_margin   = 0,
    .hsync_len      = 0,
    .vsync_len      = 0,
    .vmode          = FB_VMODE_NONINTERLACED,
};

/* I2C write functions */
static int ssd1306_write_cmd(struct ssd1306_device *ssd1306, u8 cmd)
{
    u8 buf[2] = {0x00, cmd};  // control byte 0x00 for command
    int ret = i2c_master_send(ssd1306->client, buf, 2);
    if (ret != 2) {
        dev_err(&ssd1306->client->dev, "Failed to send command 0x%02x (ret=%d)\n", cmd, ret);
        return ret < 0 ? ret : -EIO;
    }
    return 0;
}

static int ssd1306_write_data(struct ssd1306_device *ssd1306, u8 *data, int len)
{
    u8 *buf;
    int ret;

    buf = kmalloc(len + 1, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    buf[0] = 0x40;  // control byte 0x40 for data
    memcpy(buf + 1, data, len);
    ret = i2c_master_send(ssd1306->client, buf, len + 1);
    kfree(buf);

    if (ret != len + 1) {
        dev_err(&ssd1306->client->dev, "Failed to send %d data bytes (ret=%d)\n", len, ret);
        return ret < 0 ? ret : -EIO;
    }
    return 0;
}

/* OLED hardware initialization - now using configuration macros */
static int ssd1306_hw_init(struct ssd1306_device *ssd1306)
{
    u8 cmds[] = {
        SSD1306_DISPLAYOFF,
        SSD1306_SETDISPLAYCLOCKDIV, SSD1306_CONFIG_CLOCKDIV,
        SSD1306_SETMULTIPLEX, SSD1306_CONFIG_MULTIPLEX,
        SSD1306_SETDISPLAYOFFSET, SSD1306_CONFIG_OFFSET,
        SSD1306_SETSTARTLINE | 0x00,
        SSD1306_CHARGEPUMP, SSD1306_CONFIG_CHARGEPUMP,
        SSD1306_MEMORYMODE, SSD1306_CONFIG_MEMORYMODE,
        SSD1306_CONFIG_SEGREMAP,
        SSD1306_CONFIG_COMSCAN,
        SSD1306_SETCOMPINS, SSD1306_CONFIG_COMPINS,
        SSD1306_SETCONTRAST, SSD1306_CONFIG_CONTRAST,
        SSD1306_SETPRECHARGE, SSD1306_CONFIG_PRECHARGE,
        SSD1306_SETVCOMDETECT, SSD1306_CONFIG_VCOMDETECT,
        SSD1306_DISPLAYALLON_RESUME,
        SSD1306_NORMALDISPLAY,
        SSD1306_DISPLAYON
    };
    int i, ret;

    for (i = 0; i < ARRAY_SIZE(cmds); i++) {
        ret = ssd1306_write_cmd(ssd1306, cmds[i]);
        if (ret < 0) {
            dev_err(&ssd1306->client->dev, "Failed to send init command at index %d\n", i);
            return ret;
        }
        msleep(1);
    }

    return 0;
}

/* Set display position (page and column) */
static int ssd1306_set_pos(struct ssd1306_device *ssd1306, u8 page, u8 column)
{
    int ret;

    ret = ssd1306_write_cmd(ssd1306, 0xB0 + page);  // set page address (0-7)
    if (ret < 0)
        return ret;
    ret = ssd1306_write_cmd(ssd1306, column & 0x0F);        // lower column
    if (ret < 0)
        return ret;
    ret = ssd1306_write_cmd(ssd1306, 0x10 | (column >> 4)); // higher column
    return ret;
}

/* Convert framebuffer data to OLED format */
static void convert_fb_to_oled(struct ssd1306_device *ssd1306)
{
    u8 *fb = ssd1306->fb_buffer;
    u8 *oled = ssd1306->internal_buffer;
    int x, page, bit;
    u8 data;

    for (page = 0; page < 8; page++) {
        for (x = 0; x < 128; x++) {
            data = 0;
            for (bit = 0; bit < 8; bit++) {
                int y = page * 8 + bit;
                int fb_byte_offset = y * (128/8) + (x/8);
                int fb_bit_offset;

                /* Determine fb bit offset based on fb_bit_reverse */
                if (fb_bit_reverse)
                    fb_bit_offset = 7 - (x % 8);
                else
                    fb_bit_offset = x % 8;

                int pixel = (fb[fb_byte_offset] >> fb_bit_offset) & 1;

                if (swap_bit_order) {
                    if (pixel)
                        data |= (1 << (7 - bit));
                } else {
                    if (pixel)
                        data |= (1 << bit);
                }
            }
            oled[page * 128 + x] = data;
        }
    }
}

/* Refresh thread: periodically send framebuffer to OLED */
static int ssd1306_refresh_thread(void *data)
{
    struct ssd1306_device *ssd1306 = (struct ssd1306_device *)data;
    u8 *buf = ssd1306->internal_buffer;
    int page, ret;

    while (!kthread_should_stop()) {
        wait_event_interruptible_timeout(ssd1306->wait_queue,
                                         ssd1306->running || kthread_should_stop(),
                                         msecs_to_jiffies(ssd1306->refresh_rate));

        if (kthread_should_stop())
            break;

        if (!ssd1306->running)
            continue;

        mutex_lock(&ssd1306->lock);

        convert_fb_to_oled(ssd1306);

        for (page = 0; page < 8; page++) {
            ret = ssd1306_set_pos(ssd1306, page, 0);
            if (ret < 0) {
                dev_err_once(&ssd1306->client->dev,
                             "Failed to set page %d position\n", page);
                break;
            }
            ret = ssd1306_write_data(ssd1306, buf + page * 128, 128);
            if (ret < 0) {
                dev_err_once(&ssd1306->client->dev,
                             "Failed to write page %d data\n", page);
                break;
            }
        }

        mutex_unlock(&ssd1306->lock);
    }

    return 0;
}

/* fb_ops: open */
static int ssd1306_fb_open(struct fb_info *info, int user)
{
    struct ssd1306_device *ssd1306 = (struct ssd1306_device *)info->par;

    mutex_lock(&ssd1306->lock);
    ssd1306->running = true;
    wake_up(&ssd1306->wait_queue);
    mutex_unlock(&ssd1306->lock);

    return 0;
}

/* fb_ops: release */
static int ssd1306_fb_release(struct fb_info *info, int user)
{
    struct ssd1306_device *ssd1306 = (struct ssd1306_device *)info->par;

    mutex_lock(&ssd1306->lock);
    ssd1306->running = false;
    mutex_unlock(&ssd1306->lock);

    return 0;
}

/* fb_ops: blank */
static int ssd1306_fb_blank(int blank, struct fb_info *info)
{
    struct ssd1306_device *ssd1306 = (struct ssd1306_device *)info->par;

    if (blank) {
        ssd1306_write_cmd(ssd1306, SSD1306_DISPLAYOFF);
    } else {
        ssd1306_write_cmd(ssd1306, SSD1306_DISPLAYON);
    }

    return 0;
}

/* fb_ops: fill rectangle */
static void ssd1306_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
    sys_fillrect(info, rect);
}

/* fb_ops: copy area */
static void ssd1306_fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
    sys_copyarea(info, area);
}

/* fb_ops: image blit */
static void ssd1306_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
    sys_imageblit(info, image);
}

/* Custom fb_mmap to handle DMA-coherent memory */
static int ssd1306_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
    struct ssd1306_device *ssd1306 = (struct ssd1306_device *)info->par;
    return dma_mmap_wc(&ssd1306->client->dev, vma,
                       info->screen_base, info->fix.smem_start,
                       info->fix.smem_len);
}

/* fb_ops structure */
static struct fb_ops ssd1306_fb_ops = {
    .owner        = THIS_MODULE,
    .fb_open      = ssd1306_fb_open,
    .fb_release   = ssd1306_fb_release,
    .fb_read      = fb_sys_read,
    .fb_write     = fb_sys_write,
    .fb_fillrect  = ssd1306_fb_fillrect,
    .fb_copyarea  = ssd1306_fb_copyarea,
    .fb_imageblit = ssd1306_fb_imageblit,
    .fb_blank     = ssd1306_fb_blank,
    .fb_mmap      = ssd1306_fb_mmap,
};

/* Probe function: called when I2C device matches */
static int ssd1306_probe(struct i2c_client *client)
{
    struct ssd1306_device *ssd1306;
    struct fb_info *fb_info;
    dma_addr_t dma_addr;
    int ret;

    dev_info(&client->dev, "SSD1306 framebuffer driver probing\n");

    ssd1306 = devm_kzalloc(&client->dev, sizeof(*ssd1306), GFP_KERNEL);
    if (!ssd1306)
        return -ENOMEM;

    ssd1306->client = client;
    ssd1306->xres = SSD1306_WIDTH;
    ssd1306->yres = SSD1306_HEIGHT;
    ssd1306->pages = SSD1306_PAGES;
    ssd1306->refresh_rate = 200;  // 200 ms refresh
    ssd1306->running = false;
    mutex_init(&ssd1306->lock);
    init_waitqueue_head(&ssd1306->wait_queue);
    i2c_set_clientdata(client, ssd1306);

    fb_info = framebuffer_alloc(sizeof(void *), &client->dev);
    if (!fb_info) {
        dev_err(&client->dev, "Failed to allocate framebuffer info\n");
        return -ENOMEM;
    }

    ssd1306->fb_info = fb_info;
    fb_info->par = ssd1306;
    fb_info->var = ssd1306_var;
    fb_info->fix = ssd1306_fix;
    fb_info->fbops = &ssd1306_fb_ops;
    fb_info->flags = 0;
    fb_info->pseudo_palette = NULL;

    fb_info->screen_base = dma_alloc_wc(&client->dev, SSD1306_FB_SIZE,
                                        &dma_addr, GFP_KERNEL);
    if (!fb_info->screen_base) {
        dev_err(&client->dev, "Failed to allocate framebuffer memory\n");
        ret = -ENOMEM;
        goto err_fb_release;
    }
    fb_info->fix.smem_start = dma_addr;
    fb_info->fix.smem_len = SSD1306_FB_SIZE;
    ssd1306->fb_buffer = fb_info->screen_base;

    /* Clear framebuffer to avoid random pixels on initial display */
    memset(fb_info->screen_base, 0, SSD1306_FB_SIZE);

    ssd1306->internal_buffer = devm_kzalloc(&client->dev, SSD1306_FB_SIZE, GFP_KERNEL);
    if (!ssd1306->internal_buffer) {
        ret = -ENOMEM;
        goto err_dma_free;
    }

    ret = ssd1306_hw_init(ssd1306);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to initialize OLED hardware\n");
        goto err_dma_free;
    }

    ssd1306->refresh_task = kthread_create(ssd1306_refresh_thread, ssd1306,
                                           "ssd1306_refresh");
    if (IS_ERR(ssd1306->refresh_task)) {
        ret = PTR_ERR(ssd1306->refresh_task);
        dev_err(&client->dev, "Failed to create refresh thread\n");
        goto err_dma_free;
    }
    wake_up_process(ssd1306->refresh_task);

    ret = register_framebuffer(fb_info);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to register framebuffer\n");
        goto err_stop_thread;
    }

    dev_info(&client->dev, "SSD1306 framebuffer registered as /dev/fb%d\n",
             fb_info->node);

    return 0;

err_stop_thread:
    kthread_stop(ssd1306->refresh_task);
err_dma_free:
    dma_free_wc(&client->dev, SSD1306_FB_SIZE,
                fb_info->screen_base, dma_addr);
err_fb_release:
    framebuffer_release(fb_info);
    return ret;
}

/* Remove function */
static void ssd1306_remove(struct i2c_client *client)
{
    struct ssd1306_device *ssd1306 = i2c_get_clientdata(client);
    struct fb_info *fb_info = ssd1306->fb_info;

    ssd1306->running = false;
    wake_up(&ssd1306->wait_queue);
    kthread_stop(ssd1306->refresh_task);

    ssd1306_write_cmd(ssd1306, SSD1306_DISPLAYOFF);

    unregister_framebuffer(fb_info);
    dma_free_wc(&client->dev, fb_info->fix.smem_len,
                fb_info->screen_base, fb_info->fix.smem_start);
    framebuffer_release(fb_info);

    dev_info(&client->dev, "SSD1306 framebuffer removed\n");
}

/* I2C device ID table */
static const struct i2c_device_id ssd1306_id[] = {
    { "ssd1306_fb", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ssd1306_id);

/* Device tree match table */
static const struct of_device_id ssd1306_of_match[] = {
    { .compatible = "solomon,ssd1306fb-i2c" },
    { .compatible = "ssd1306" },
    { }
};
MODULE_DEVICE_TABLE(of, ssd1306_of_match);

/* I2C driver structure */
static struct i2c_driver ssd1306_fb_driver = {
    .driver = {
        .name   = DRIVER_NAME,
        .of_match_table = ssd1306_of_match,
    },
    .probe    = ssd1306_probe,
    .remove   = ssd1306_remove,
    .id_table = ssd1306_id,
};

/* Module initialization */
static int __init ssd1306_fb_init(void)
{
    return i2c_add_driver(&ssd1306_fb_driver);
}

/* Module exit */
static void __exit ssd1306_fb_exit(void)
{
    i2c_del_driver(&ssd1306_fb_driver);
}

module_init(ssd1306_fb_init);
module_exit(ssd1306_fb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Combined from various sources");
MODULE_DESCRIPTION("SSD1306 I2C Framebuffer Driver - Configurable");
MODULE_VERSION("2.3");