#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/services/nus.h>

#define BLUE_LED 6
#define GREEN_LED 30

static const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static bool connected = false;

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
                  BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE,
            CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void blue_on(void)
{
    gpio_pin_set(gpio, BLUE_LED, 0);
}

static void blue_off(void)
{
    gpio_pin_set(gpio, BLUE_LED, 1);
}

static void green_on(void)
{
    gpio_pin_set(gpio, GREEN_LED, 0);
}

static void green_off(void)
{
    gpio_pin_set(gpio, GREEN_LED, 1);
}


/* Send data in chunks that fit the current BLE MTU */
static int send_data(struct bt_conn *conn,
                     const uint8_t *data,
                     uint16_t length)
{
    uint16_t mtu = bt_gatt_get_mtu(conn);
    uint16_t packet_size = mtu - 3;
    uint16_t sent = 0;

    while (sent < length) {

        uint16_t size = length - sent;

        if (size > packet_size) {
            size = packet_size;
        }

        int err = bt_nus_send(conn, &data[sent], size);

        if (err) {
            return err;
        }

        sent += size;

        if (sent < length) {
            k_sleep(K_MSEC(10));
        }
    }

    return 0;
}


/* Called when phone connects */
static void phone_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        return;
    }

    connected = true;

    blue_off();
    green_on();

    printk("Phone connected\n");
}


/* Called when phone disconnects */
static void phone_disconnected(struct bt_conn *conn, uint8_t reason)
{
    connected = false;

    green_off();

    printk("Phone disconnected\n");
}


BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = phone_connected,
    .disconnected = phone_disconnected,
};


/* Called when the phone sends data */
static void received(struct bt_conn *conn,
                     const uint8_t *const data,
                     uint16_t len)
{
    printk("Received: ");

    for (int i = 0; i < len; i++) {
        printk("%c", data[i]);
    }

    printk("\n");

    /* Send the same message back */
    send_data(conn, data, len);
}


static struct bt_nus_cb nus_callbacks = {
    .received = received,
};


int main(void)
{
    int err;

    if (!device_is_ready(gpio)) {
        return 0;
    }

    gpio_pin_configure(gpio, BLUE_LED, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpio, GREEN_LED, GPIO_OUTPUT_HIGH);

    blue_off();
    green_off();


    /* Start Bluetooth */
    err = bt_enable(NULL);

    if (err) {
        printk("Bluetooth error\n");
        return 0;
    }


    /* Start Nordic UART Service */
    err = bt_nus_init(&nus_callbacks);

    if (err) {
        printk("NUS error\n");
        return 0;
    }


    /* Start advertising */
    err = bt_le_adv_start(
        BT_LE_ADV_CONN,
        ad,
        ARRAY_SIZE(ad),
        sd,
        ARRAY_SIZE(sd)
    );

    if (err) {
        printk("Advertising error\n");
        return 0;
    }

    printk("Advertising as ConcussionHeadband\n");


    while (1) {

        if (!connected) {

            blue_on();
            k_sleep(K_MSEC(500));

            blue_off();
            k_sleep(K_MSEC(500));

        } else {

            blue_off();
            green_on();

            k_sleep(K_MSEC(100));
        }
    }

    return 0;
}
