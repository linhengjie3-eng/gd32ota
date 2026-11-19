#include "bsp_uart.h"
#include "board_config.h"
#include "gd32f30x.h"

#define ESP_RX_RING_SIZE 1024u

static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static uint8_t rx_ring[ESP_RX_RING_SIZE];

static void usart_gpio_init(uint32_t gpio, uint32_t gpio_rcu,
                            uint32_t tx_pin, uint32_t rx_pin)
{
    rcu_periph_clock_enable(gpio_rcu);
    gpio_init(gpio, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, tx_pin);
    gpio_init(gpio, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, rx_pin);
}

static void usart_basic_init(uint32_t peripheral, uint32_t baudrate)
{
    usart_deinit(peripheral);
    usart_baudrate_set(peripheral, baudrate);
    usart_word_length_set(peripheral, USART_WL_8BIT);
    usart_parity_config(peripheral, USART_PM_NONE);
    usart_stop_bit_set(peripheral, USART_STB_1BIT);
    usart_receive_config(peripheral, USART_RECEIVE_ENABLE);
    usart_transmit_config(peripheral, USART_TRANSMIT_ENABLE);
    usart_enable(peripheral);
}

void bsp_debug_uart_init(uint32_t baudrate)
{
    usart_gpio_init(DEBUG_UART_GPIO, DEBUG_UART_GPIO_RCU,
                    DEBUG_UART_TX_PIN, DEBUG_UART_RX_PIN);
    rcu_periph_clock_enable(DEBUG_UART_RCU);
    usart_basic_init(DEBUG_UART_PERIPH, baudrate);
}

void bsp_debug_uart_write(const uint8_t *data, size_t length)
{
    size_t i;
    for (i = 0u; i < length; ++i) {
        while (usart_flag_get(DEBUG_UART_PERIPH, USART_FLAG_TBE) == RESET) {}
        usart_data_transmit(DEBUG_UART_PERIPH, data[i]);
    }
}

int bsp_debug_putchar(int ch)
{
    while (usart_flag_get(DEBUG_UART_PERIPH, USART_FLAG_TBE) == RESET) {}
    usart_data_transmit(DEBUG_UART_PERIPH, (uint8_t)ch);
    return ch;
}

void bsp_esp_uart_init(uint32_t baudrate)
{
    usart_gpio_init(ESP_UART_GPIO, ESP_UART_GPIO_RCU,
                    ESP_UART_TX_PIN, ESP_UART_RX_PIN);
    rcu_periph_clock_enable(ESP_UART_RCU);
    rx_head = 0u;
    rx_tail = 0u;
    usart_basic_init(ESP_UART_PERIPH, baudrate);
    nvic_irq_enable(ESP_UART_IRQ, 2u, 0u);
    usart_interrupt_enable(ESP_UART_PERIPH, USART_INT_RBNE);
}

void bsp_esp_uart_write(const uint8_t *data, size_t length)
{
    size_t i;
    for (i = 0u; i < length; ++i) {
        while (usart_flag_get(ESP_UART_PERIPH, USART_FLAG_TBE) == RESET) {}
        usart_data_transmit(ESP_UART_PERIPH, data[i]);
    }
    while (usart_flag_get(ESP_UART_PERIPH, USART_FLAG_TC) == RESET) {}
}

size_t bsp_esp_uart_read(uint8_t *data, size_t capacity)
{
    size_t count = 0u;
    while ((count < capacity) && (rx_tail != rx_head)) {
        data[count++] = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1u) % ESP_RX_RING_SIZE);
    }
    return count;
}

/* 中断向量名由芯片决定, 与 ESP_UART_IRQ 宏保持一致 (USART1) */
void USART1_IRQHandler(void)
{
    if (usart_interrupt_flag_get(ESP_UART_PERIPH, USART_INT_FLAG_RBNE) != RESET) {
        uint8_t byte = (uint8_t)usart_data_receive(ESP_UART_PERIPH);
        uint16_t next = (uint16_t)((rx_head + 1u) % ESP_RX_RING_SIZE);
        if (next != rx_tail) {
            rx_ring[rx_head] = byte;
            rx_head = next;
        }
    }
}
