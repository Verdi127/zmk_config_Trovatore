#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(trovatore_dual_oled, LOG_LEVEL_INF);

#define OLED_1_NODE DT_NODELABEL(oled_1)
#define OLED_2_NODE DT_NODELABEL(oled_2)

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_FB_SIZE (OLED_WIDTH * OLED_HEIGHT / 8)
#define OLED_UPDATE_INTERVAL_MS 200

#if !DT_NODE_HAS_STATUS(OLED_1_NODE, okay)
#error "oled_1 node is not defined or disabled"
#endif

#if !DT_NODE_HAS_STATUS(OLED_2_NODE, okay)
#error "oled_2 node is not defined or disabled"
#endif

static struct k_work_delayable oled_2_work;
static const struct device *oled_1_dev;
static const struct device *oled_2_dev;
static uint8_t oled_1_fb[OLED_FB_SIZE];
static uint8_t oled_2_fb[OLED_FB_SIZE];
static uint8_t frame_idx;
static uint8_t demo_battery = 100;
static bool link_up = true;
static uint8_t layer_idx;

/* Simple pixel drawing utilities */
static void draw_pixel(uint8_t *fb, int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    
    int page = y / 8;
    int bit = y % 8;
    
    if (on) {
        fb[(page * OLED_WIDTH) + x] |= (1 << bit);
    } else {
        fb[(page * OLED_WIDTH) + x] &= ~(1 << bit);
    }
}

static void draw_vline(uint8_t *fb, int x, int y1, int y2, bool on)
{
    for (int y = y1; y <= y2; y++) {
        draw_pixel(fb, x, y, on);
    }
}

static void draw_hline(uint8_t *fb, int x1, int x2, int y, bool on)
{
    for (int x = x1; x <= x2; x++) {
        draw_pixel(fb, x, y, on);
    }
}

static void draw_rect(uint8_t *fb, int x, int y, int w, int h, bool on)
{
    draw_hline(fb, x, x + w - 1, y, on);
    draw_hline(fb, x, x + w - 1, y + h - 1, on);
    draw_vline(fb, x, y, y + h - 1, on);
    draw_vline(fb, x + w - 1, y, y + h - 1, on);
}

static void clear_fb(uint8_t *fb)
{
    memset(fb, 0x00, OLED_FB_SIZE);
}

static void draw_battery_icon(uint8_t *fb, int x, int y, uint8_t percent)
{
    int fill = (percent * 12) / 100;
    draw_rect(fb, x, y, 14, 8, true);
    draw_rect(fb, x + 14, y + 2, 2, 4, true);

    for (int i = 0; i < fill; i++) {
        draw_vline(fb, x + 1 + i, y + 1, y + 6, true);
    }
}

static void draw_bongo_cat(uint8_t *fb, int x, int y, uint8_t frame)
{
    bool paws_down = (frame % 2) == 0;

    draw_rect(fb, x + 22, y + 8, 34, 14, true);

    draw_rect(fb, x + 8, y + 8, 16, 10, true);
    draw_pixel(fb, x + 12, y + 12, true);
    draw_pixel(fb, x + 18, y + 12, true);
    draw_hline(fb, x + 13, x + 17, y + 15, true);

    draw_rect(fb, x + 8, y + 2, 4, 5, true);
    draw_rect(fb, x + 20, y + 2, 4, 5, true);

    draw_hline(fb, x + 24, x + 54, y + 20, true);
    if (paws_down) {
        draw_rect(fb, x + 29, y + 16, 5, 4, true);
        draw_rect(fb, x + 42, y + 16, 5, 4, true);
    } else {
        draw_rect(fb, x + 29, y + 12, 5, 4, true);
        draw_rect(fb, x + 42, y + 12, 5, 4, true);
    }
}

static void draw_glyph(uint8_t *fb, int x, int y, char c)
{
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t A[5] = {0x1E, 0x05, 0x05, 0x1E, 0x00};
    static const uint8_t B[5] = {0x1F, 0x15, 0x15, 0x0A, 0x00};
    static const uint8_t C[5] = {0x0E, 0x11, 0x11, 0x11, 0x00};
    static const uint8_t D[5] = {0x1F, 0x11, 0x11, 0x0E, 0x00};
    static const uint8_t E[5] = {0x1F, 0x15, 0x15, 0x11, 0x00};
    static const uint8_t F[5] = {0x1F, 0x05, 0x05, 0x01, 0x00};
    static const uint8_t I[5] = {0x00, 0x11, 0x1F, 0x11, 0x00};
    static const uint8_t L[5] = {0x1F, 0x10, 0x10, 0x10, 0x00};
    static const uint8_t N[5] = {0x1F, 0x02, 0x04, 0x1F, 0x00};
    static const uint8_t O[5] = {0x0E, 0x11, 0x11, 0x0E, 0x00};
    static const uint8_t R[5] = {0x1F, 0x05, 0x0D, 0x12, 0x00};
    static const uint8_t S[5] = {0x12, 0x15, 0x15, 0x09, 0x00};
    static const uint8_t T[5] = {0x01, 0x01, 0x1F, 0x01, 0x01};
    static const uint8_t Y[5] = {0x03, 0x04, 0x18, 0x04, 0x03};
    static const uint8_t ZERO[5] = {0x0E, 0x19, 0x15, 0x13, 0x0E};
    static const uint8_t ONE[5] = {0x00, 0x12, 0x1F, 0x10, 0x00};
    static const uint8_t TWO[5] = {0x12, 0x19, 0x15, 0x12, 0x00};
    static const uint8_t THREE[5] = {0x11, 0x15, 0x15, 0x0A, 0x00};
    static const uint8_t COLON[5] = {0x00, 0x0A, 0x00, 0x00, 0x00};

    const uint8_t *glyph = blank;

    switch (c) {
    case 'A': glyph = A; break;
    case 'B': glyph = B; break;
    case 'C': glyph = C; break;
    case 'D': glyph = D; break;
    case 'E': glyph = E; break;
    case 'F': glyph = F; break;
    case 'I': glyph = I; break;
    case 'L': glyph = L; break;
    case 'N': glyph = N; break;
    case 'O': glyph = O; break;
    case 'R': glyph = R; break;
    case 'S': glyph = S; break;
    case 'T': glyph = T; break;
    case 'Y': glyph = Y; break;
    case '0': glyph = ZERO; break;
    case '1': glyph = ONE; break;
    case '2': glyph = TWO; break;
    case '3': glyph = THREE; break;
    case ':': glyph = COLON; break;
    case ' ': glyph = blank; break;
    default: glyph = blank; break;
    }

    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            draw_pixel(fb, x + col, y + row, ((bits >> row) & 0x01) != 0);
        }
    }
}

static void draw_text(uint8_t *fb, int x, int y, const char *text)
{
    int cursor = x;
    while (*text != '\0') {
        draw_glyph(fb, cursor, y, *text);
        cursor += 6;
        text++;
    }
}

static void fill_oled_1_display(uint8_t *fb)
{
    clear_fb(fb);

    draw_hline(fb, 0, OLED_WIDTH - 1, 9, true);
    draw_text(fb, 2, 1, "BAT");
    draw_battery_icon(fb, 26, 1, demo_battery);
    draw_text(fb, 50, 1, "BONGO");

    draw_bongo_cat(fb, 30, 9, frame_idx);
}

static void fill_oled_2_display(uint8_t *fb)
{
    clear_fb(fb);

    draw_hline(fb, 0, OLED_WIDTH - 1, 9, true);
    draw_text(fb, 2, 1, "CONN:");
    draw_text(fb, 38, 1, link_up ? "ON" : "OFF");

    draw_text(fb, 2, 14, "LAYER:");
    switch (layer_idx % 3) {
    case 0:
        draw_text(fb, 44, 14, "BASE");
        break;
    case 1:
        draw_text(fb, 44, 14, "SYM");
        break;
    default:
        draw_text(fb, 44, 14, "NUM");
        break;
    }
}

static void render_oled_2(void)
{
    struct display_buffer_descriptor desc1 = {
        .buf_size = sizeof(oled_1_fb),
        .width = OLED_WIDTH,
        .height = OLED_HEIGHT,
        .pitch = OLED_WIDTH,
    };

    struct display_buffer_descriptor desc2 = {
        .buf_size = sizeof(oled_2_fb),
        .width = OLED_WIDTH,
        .height = OLED_HEIGHT,
        .pitch = OLED_WIDTH,
    };

    fill_oled_1_display(oled_1_fb);
    fill_oled_2_display(oled_2_fb);
    (void)display_write(oled_1_dev, 0, 0, &desc1, oled_1_fb);
    (void)display_write(oled_2_dev, 0, 0, &desc2, oled_2_fb);

    frame_idx++;
    if ((frame_idx % 10U) == 0U) {
        if (demo_battery > 5) {
            demo_battery -= 5;
        } else {
            demo_battery = 100;
        }
        link_up = !link_up;
        layer_idx++;
    }
}

static void oled_2_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    render_oled_2();
    k_work_schedule(&oled_2_work, K_MSEC(OLED_UPDATE_INTERVAL_MS));
}

static int trovatore_dual_oled_init(void)
{
    oled_1_dev = DEVICE_DT_GET(OLED_1_NODE);
    oled_2_dev = DEVICE_DT_GET(OLED_2_NODE);

    if (!device_is_ready(oled_1_dev)) {
        LOG_WRN("oled_1 is not ready");
        return 0;
    }

    if (!device_is_ready(oled_2_dev)) {
        LOG_WRN("oled_2 is not ready");
        return 0;
    }

    (void)display_blanking_off(oled_1_dev);
    (void)display_blanking_off(oled_2_dev);

    k_work_init_delayable(&oled_2_work, oled_2_work_handler);
    k_work_schedule(&oled_2_work, K_NO_WAIT);

    LOG_INF("Trovatore dual OLED enabled:");
    LOG_INF("  oled_1 (top)    = battery + bongo cat");
    LOG_INF("  oled_2 (bottom) = conn + layer");
    return 0;
}

SYS_INIT(trovatore_dual_oled_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
