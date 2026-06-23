#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(trovatore_dual_oled, LOG_LEVEL_INF);

#define OLED_1_NODE DT_NODELABEL(oled_1)
#define OLED_2_NODE DT_NODELABEL(oled_2)

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_FB_SIZE (OLED_WIDTH * OLED_HEIGHT / 8)
#define OLED_UPDATE_INTERVAL_MS 1000

#if !DT_NODE_HAS_STATUS(OLED_1_NODE, okay)
#error "oled_1 node is not defined or disabled"
#endif

#if !DT_NODE_HAS_STATUS(OLED_2_NODE, okay)
#error "oled_2 node is not defined or disabled"
#endif

static struct k_work_delayable oled_2_work;
static const struct device *oled_2_dev;
static uint8_t oled_2_fb[OLED_FB_SIZE];
static bool phase;

static void fill_oled_2_pattern(uint8_t *fb, bool phase_on)
{
    /* Simple test pattern: all pixels on ('b' pattern) */
    for (size_t page = 0; page < (OLED_HEIGHT / 8); page++) {
        for (size_t x = 0; x < OLED_WIDTH; x++) {
            fb[(page * OLED_WIDTH) + x] = 0xFF;  /* All pixels on */
        }
    }
}

static void render_oled_2(void)
{
    struct display_buffer_descriptor desc = {
        .buf_size = sizeof(oled_2_fb),
        .width = OLED_WIDTH,
        .height = OLED_HEIGHT,
        .pitch = OLED_WIDTH,
    };

    fill_oled_2_pattern(oled_2_fb, phase);
    phase = !phase;

    (void)display_write(oled_2_dev, 0, 0, &desc, oled_2_fb);
}

static void oled_2_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    render_oled_2();
    k_work_schedule(&oled_2_work, K_MSEC(OLED_UPDATE_INTERVAL_MS));
}

static int trovatore_dual_oled_init(void)
{
    const struct device *oled_1 = DEVICE_DT_GET(OLED_1_NODE);
    oled_2_dev = DEVICE_DT_GET(OLED_2_NODE);

    if (!device_is_ready(oled_1)) {
        LOG_WRN("oled_1 is not ready");
        return 0;
    }

    if (!device_is_ready(oled_2_dev)) {
        LOG_WRN("oled_2 is not ready");
        return 0;
    }

    /* oled_1 is managed by ZMK status screen via zephyr,display. */
    (void)display_blanking_off(oled_1);
    (void)display_blanking_off(oled_2_dev);

    k_work_init_delayable(&oled_2_work, oled_2_work_handler);
    k_work_schedule(&oled_2_work, K_NO_WAIT);

    LOG_INF("dual OLED enabled: oled_1=ZMK status, oled_2=custom pattern");
    return 0;
}

SYS_INIT(trovatore_dual_oled_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
