#include <Arduino.h>
#include "HWCDC.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "soc/uart_reg.h"
#include "esp_system.h"
#include "board_link.h"
#include "control_input.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error This target requires ESP32-S3
#endif
static const uart_port_t port=static_cast<uart_port_t>(LINK_ESP_UART);
static QueueHandle_t uart_events;
static link_node_t client;
static bool transport_ready=false;
static uint32_t uart_rx_bytes, uart_tx_bytes, uart_tx_frames, uart_faults;
static uint8_t rx_trace[128];
static unsigned rx_trace_head, rx_trace_used;
static int last_uart_event=-1;
static char console_line[2][32];
static unsigned console_used[2];
static bool console_overflow[2], led_operation;
static uint32_t console_last_byte_ms[2];
static unsigned led_console;

/* Native USB diagnostics never waits for a host or blocks the UART owner loop. */
static void console_reply(const char *text,unsigned which)
{
    size_t length=strlen(text);
    if(which==0) {
        if(USBSerial && USBSerial.availableForWrite()>=static_cast<int>(length))
            USBSerial.write(reinterpret_cast<const uint8_t *>(text),length);
    } else if(Serial.availableForWrite()>=static_cast<int>(length)) {
        Serial.write(reinterpret_cast<const uint8_t *>(text),length);
    }
}
static void console_command(const char *line,uint32_t now,unsigned which)
{
    char response[384];
    if(!strcmp(line,"INFO")) {
        snprintf(response,sizeof(response),
            "INFO firmware=crtc-board-link-led-v1 transport=%u connected=%u token=%08lX flags=%u encoder_mask=%u parser_errors=%lu rejected=%lu rx_bytes=%lu tx_bytes=%lu tx_frames=%lu uart_faults=%lu last_uart_event=%d rx_level=%d uptime_ms=%lu tx_gpio=%u rx_gpio=%u\n",
            transport_ready?1U:0U,client.connected,static_cast<unsigned long>(client.token),
            client.remote_flags,client.remote_encoder_valid_mask,
            static_cast<unsigned long>(client.parser.errors),
            static_cast<unsigned long>(client.rejected_frames),
            static_cast<unsigned long>(uart_rx_bytes),
            static_cast<unsigned long>(uart_tx_bytes),
            static_cast<unsigned long>(uart_tx_frames),
            static_cast<unsigned long>(uart_faults),last_uart_event,
            gpio_get_level(static_cast<gpio_num_t>(LINK_ESP_RX_GPIO)),
            static_cast<unsigned long>(now),LINK_ESP_TX_GPIO,LINK_ESP_RX_GPIO);
        console_reply(response,which);
    } else if(!strcmp(line,"RX")) {
        unsigned used=snprintf(response,sizeof(response),"RX bytes=%lu hex=",
                               static_cast<unsigned long>(uart_rx_bytes));
        unsigned first=(rx_trace_head+sizeof(rx_trace)-rx_trace_used)%sizeof(rx_trace);
        for(unsigned i=0;i<rx_trace_used;i++)
            used+=snprintf(response+used,sizeof(response)-used,"%02X",
                           rx_trace[(first+i)%sizeof(rx_trace)]);
        response[used++]='\n'; response[used]=0;
        console_reply(response,which);
    } else if(!strcmp(line,"LED ON") || !strcmp(line,"LED OFF")) {
        uint8_t on=!strcmp(line,"LED ON")?1:0;
        if(led_operation || !transport_ready || !link_node_request_led(&client,on,now)) {
            console_reply("ERR LED LINK_OR_BUSY\n",which);
        } else {
            led_operation=true; led_console=which;
            console_reply(on?"QUEUED LED=1\n":"QUEUED LED=0\n",which);
        }
    } else if(!strcmp(line,"HELP")) {
        console_reply("INFO | RX | LED ON | LED OFF | HELP\n",which);
    } else {
        console_reply("ERR COMMAND\n",which);
    }
}
static void console_poll(uint32_t now)
{
    for(unsigned which=0;which<2;which++) {
        if(console_used[which] && static_cast<uint32_t>(now-console_last_byte_ms[which])>=500U) {
            console_used[which]=0; console_overflow[which]=true;
        }
        for(unsigned budget=0;budget<16;budget++) {
            if(!(which==0?USBSerial.available():Serial.available())) break;
            int byte=which==0?USBSerial.read():Serial.read();
            if(byte<0) break;
            console_last_byte_ms[which]=now;
            if(byte=='\r') continue;
            if(byte=='\n') {
                if(console_overflow[which]) console_reply("ERR LINE\n",which);
                else if(console_used[which]) {
                    console_line[which][console_used[which]]=0;
                    console_command(console_line[which],now,which);
                }
                console_used[which]=0; console_overflow[which]=false;
            } else if(byte>=32 && byte<=126 && console_used[which]<sizeof(console_line[which])-1U) {
                if(!console_overflow[which]) console_line[which][console_used[which]++]=static_cast<char>(byte);
            } else {
                console_overflow[which]=true;
            }
        }
    }
    if(led_operation) {
        if(client.remote_led_valid) {
            char response[64];
            snprintf(response,sizeof(response),"ACK LED=%u seq=%lu\n",client.remote_led_on,
                     static_cast<unsigned long>(client.remote_led_sequence));
            console_reply(response,led_console); led_operation=false;
        } else if(!client.led_pending && !client.led_waiting_ack) {
            console_reply("ERR LED TIMEOUT_OR_DISCONNECT\n",led_console); led_operation=false;
        }
    }
}

bool crtc_submit_control(uint32_t seq,const int16_t targets[4],bool enable,uint16_t ttl)
{
    if(!transport_ready) return false;
    return link_node_submit_input(&client,seq,targets,enable?1:0,ttl,millis())!=0;
}
void crtc_cancel_control() { link_node_cancel_input(&client); }
static void fail_transport(uint32_t now)
{
    uart_faults++;
    uart_flush_input(port); xQueueReset(uart_events);
    link_node_transport_error(&client,now);
}
void setup()
{
    uart_config_t config={};
    USBSerial.setTxBufferSize(1024);
    USBSerial.setTxTimeoutMs(0);
    USBSerial.begin(115200);
    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
    link_node_init(&client,LINK_CLIENT,esp_random(),millis());
    config.baud_rate=LINK_BAUD; config.data_bits=UART_DATA_8_BITS;
    config.parity=UART_PARITY_DISABLE; config.stop_bits=UART_STOP_BITS_1;
    config.flow_ctrl=UART_HW_FLOWCTRL_DISABLE; config.source_clk=UART_SCLK_APB;
    if(uart_param_config(port,&config)!=ESP_OK) return;
    if(uart_set_pin(port,LINK_ESP_TX_GPIO,LINK_ESP_RX_GPIO,
                    UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK) return;
    /* The native IOMUX route can leave RX without a pull-up. Keep an
     * unconnected receive wire at UART idle without driving the peer. */
    if(gpio_set_pull_mode(static_cast<gpio_num_t>(LINK_ESP_RX_GPIO),
                          GPIO_PULLUP_ONLY)!=ESP_OK) return;
    if(uart_driver_install(port,256,256,8,&uart_events,0)!=ESP_OK) return;
    /* IDF's default UART mask omits frame errors; enable the event handled
     * below so malformed stop bits also invalidate the current link. */
    if(uart_enable_intr_mask(port,UART_FRM_ERR_INT_ENA_M)!=ESP_OK) return;
    transport_ready=true;
    /* No Bluetooth, Wi-Fi, PS2, servo or motor initialization. Native USB
     * uses its existing pins. Until a future input adapter submits fresh events,
     * the client sends only disabled zero commands after the handshake. */
}
void loop()
{
    uint32_t now=millis(); uart_event_t event; uint8_t bytes[64], wire[LINK_WIRE_MAX];
    if(!transport_ready) { console_poll(now); delay(10); return; }
    link_node_tick(&client,now);
    while(xQueueReceive(uart_events,&event,0)==pdTRUE) {
        if(event.type==UART_FIFO_OVF || event.type==UART_BUFFER_FULL ||
           event.type==UART_FRAME_ERR || event.type==UART_PARITY_ERR || event.type==UART_BREAK) {
            last_uart_event=event.type;
            fail_transport(now); break;
        }
    }
    /* Bounded draining keeps the timeout task running even with continuous noise. */
    for(unsigned chunk=0;chunk<4;chunk++) {
        int count=uart_read_bytes(port,bytes,sizeof(bytes),0);
        if(count<0) { fail_transport(now); break; }
        if(count==0) break;
        uart_rx_bytes+=static_cast<uint32_t>(count);
        for(int i=0;i<count;i++) {
            rx_trace[rx_trace_head]=bytes[i];
            rx_trace_head=(rx_trace_head+1U)%sizeof(rx_trace);
            if(rx_trace_used<sizeof(rx_trace)) rx_trace_used++;
            link_node_receive(&client,bytes[i],now);
        }
    }
    console_poll(now);
    size_t length=link_node_transmit(&client,now,wire,sizeof(wire));
    if(length) {
        int written=uart_write_bytes(port,wire,length);
        if(written>0) uart_tx_bytes+=static_cast<uint32_t>(written);
        if(written!=static_cast<int>(length)) fail_transport(now);
        else uart_tx_frames++;
    }
    delay(1);
}
