/* Historical ESP32 bridge, retained for restoration after hardware repair.
 * The dual-STM32 fallback targets do not build or run this code.
 * Current wiring/controls: docs/14-dual-stm32-fallback.md. */
#include <Arduino.h>
#if !defined(CRTC_C3_DASHBOARD)
#include "HWCDC.h"
#else
#include "soc/usb_serial_jtag_struct.h"
#endif
#include "driver/uart.h"
#include "driver/gpio.h"
#include "soc/uart_reg.h"
#include "esp_system.h"
#include "board_link.h"
#include "control_input.h"
#include "motor_command.h"
#include "remote_bridge.h"
#include "ble_remote.h"
#include "servo_output.h"
#if defined(CRTC_C3_DASHBOARD)
#include <Preferences.h>
#endif

#if defined(CRTC_C3_DASHBOARD)
#if !defined(CONFIG_IDF_TARGET_ESP32C3)
#error c3-dashboard requires ESP32-C3
#endif
#define CRTC_FIRMWARE_NAME "crtc-c3-bootquiet-v5"
#elif !defined(CONFIG_IDF_TARGET_ESP32S3)
#error The historical target requires ESP32-S3
#else
#define CRTC_FIRMWARE_NAME "crtc-lower-servo-slew-v4"
#endif
static const uart_port_t port=static_cast<uart_port_t>(LINK_ESP_UART);
static QueueHandle_t uart_events;
static link_node_t client;
static remote_bridge_t remote_bridge;
static uint32_t ble_generation, ble_status_ms, owner_loop_ms;
static uint8_t ble_status_kind;
static bool transport_ready=false;
static uint32_t uart_rx_bytes, uart_tx_bytes, uart_tx_frames, uart_faults;
static uint8_t rx_trace[128];
static unsigned rx_trace_head, rx_trace_used;
static int last_uart_event=-1;
static char console_line[2][64];
static unsigned console_used[2];
static bool console_overflow[2], led_operation;
static uint32_t console_last_byte_ms[2];
static unsigned led_console;
static bool console_stop_barrier;
static bool servo_ready;
static uint32_t servo_boot_ms;
static const uint32_t servo_boot_quiet_ms=1500;
#if defined(CRTC_C3_DASHBOARD)
static Preferences preferences;
static bool preferences_ready;
static int save_settings(const device_settings_t *settings)
{
    uint8_t blob[DEVICE_SETTINGS_BYTES];
    if(!preferences_ready || !device_settings_encode(settings,blob)) return 0;
    return preferences.putBytes("params5",blob,sizeof(blob))==sizeof(blob);
}
static void load_settings()
{
    uint8_t blob[DEVICE_SETTINGS_BYTES]; device_settings_t settings;
    preferences_ready=preferences.begin("crtc",false);
    remote_bridge.save_settings=save_settings;
    if(!preferences_ready) return;
    const size_t size=preferences.getBytesLength("params5");
    if(size==sizeof(blob) && preferences.getBytes("params5",blob,sizeof(blob))==sizeof(blob) &&
       device_settings_decode(&settings,blob,sizeof(blob)) && remote_bridge_load_settings(&remote_bridge,&settings)) {
        remote_bridge.settings_loaded=1;
    } else if(!size) {
        // Only a genuinely empty store seeds defaults. Never overwrite a
        // damaged or newer-schema record with firmware defaults.
        remote_bridge_get_settings(&remote_bridge,&settings);
        remote_bridge.settings_loaded=save_settings(&settings)?1:0;
    }
}
#endif

static void stop_control(uint32_t now)
{
    remote_bridge_stop(&remote_bridge,REMOTE_REASON_STOP,now);
    ble_remote_drop_input();
    console_stop_barrier=true;
    client.last_tx_ms=now-LINK_COMMAND_PERIOD_MS; /* Send disabled zero immediately. */
}

/* Native USB diagnostics never waits for a host or blocks the UART owner loop. */
static void console_reply(const char *text,unsigned which)
{
    size_t length=strlen(text);
#if defined(CRTC_C3_DASHBOARD)
    (void)which;
    if(Serial.availableForWrite()>=static_cast<int>(length)) Serial.write(reinterpret_cast<const uint8_t *>(text),length);
#else
    if(which==0) {
        if(USBSerial && USBSerial.availableForWrite()>=static_cast<int>(length))
            USBSerial.write(reinterpret_cast<const uint8_t *>(text),length);
    } else if(Serial.availableForWrite()>=static_cast<int>(length)) {
        Serial.write(reinterpret_cast<const uint8_t *>(text),length);
    }
#endif
}
static void console_command(const char *line,uint32_t now,unsigned which)
{
    char response[384];
    if(!strcmp(line,"INFO")) {
        snprintf(response,sizeof(response),
            "INFO firmware=" CRTC_FIRMWARE_NAME " transport=%u connected=%u token=%08lX flags=%u encoder_mask=%u parser_errors=%lu rejected=%lu rx_bytes=%lu tx_bytes=%lu tx_frames=%lu uart_faults=%lu last_uart_event=%d rx_level=%d uptime_ms=%lu tx_gpio=%u rx_gpio=%u\n",
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
    } else if(!strcmp(line,"SERVO STATUS")) {
        snprintf(response,sizeof(response),"SERVO ready=%u quiet_ms=%lu pulses=%u,%u,%u targets=%u,%u,%u levels=%d,%d,%d moving=%u\n",
            remote_bridge.servo_write?1U:0U,
            static_cast<unsigned long>(now-servo_boot_ms<servo_boot_quiet_ms?servo_boot_quiet_ms-(now-servo_boot_ms):0),
            servo_output_pulse(0),servo_output_pulse(1),servo_output_pulse(2),
            remote_bridge.servos[0].target,remote_bridge.servos[1].target,remote_bridge.servos[2].target,
            gpio_get_level(static_cast<gpio_num_t>(LINK_ESP_ARM_SERVO_GPIO)),
            gpio_get_level(static_cast<gpio_num_t>(LINK_ESP_FOREARM_SERVO_GPIO)),
            gpio_get_level(static_cast<gpio_num_t>(LINK_ESP_DOOR_SERVO_GPIO)),
            (remote_bridge.servos[0].moving?1:0)|(remote_bridge.servos[1].moving?2:0)|(remote_bridge.servos[2].moving?4:0));
        console_reply(response,which);
    } else if(!strcmp(line,"SERVO OFF ALL") || (!strncmp(line,"SERVO OFF ",10) &&
              line[10]>='0' && line[10]<='2' && !line[11])) {
        unsigned first=!strcmp(line,"SERVO OFF ALL")?0U:static_cast<unsigned>(line[10]-'0');
        unsigned end=!strcmp(line,"SERVO OFF ALL")?3U:first+1U; bool ok=true;
        stop_control(now);
        for(unsigned i=first;i<end;i++) {
            if(!servo_output_write(static_cast<uint8_t>(i),0)) { ok=false; continue; }
            remote_bridge.servo_us[i]=0; remote_bridge.servo_error_mask&=static_cast<uint8_t>(~(1U<<i));
            servo_motion_reference(&remote_bridge.servos[i],0,now);
        }
        console_reply(ok?"ACK SERVO OFF\n":"ERR SERVO OFF\n",which);
#if defined(CRTC_C3_DASHBOARD)
    } else if(!strcmp(line,"SETTINGS")) {
        snprintf(response,sizeof(response),"SETTINGS loaded=%u revision=%u synced=%u pid=%u,%u,%u motor=%d,%d,%d,%d\n",
            remote_bridge.settings_loaded,remote_bridge.settings.revision,remote_bridge.synced_revision,
            remote_bridge.kp_milli,remote_bridge.ki_milli,remote_bridge.kd_milli,
            (remote_bridge.configs[0][1]&1)?-1:1,(remote_bridge.configs[1][1]&1)?-1:1,
            (remote_bridge.configs[2][1]&1)?-1:1,(remote_bridge.configs[3][1]&1)?-1:1);
        console_reply(response,which);
#endif
    } else if(!strcmp(line,"STOP")) {
        stop_control(now); console_reply("QUEUED STOP\n",which);
    } else if(!strcmp(line,"ENC")) {
        snprintf(response,sizeof(response),"ENC mask=%u age_ms=%lu counts=%ld,%ld,%ld,%ld\n",
            client.remote_encoder_valid_mask,
            static_cast<unsigned long>(now-client.remote_encoder_rx_ms),
            static_cast<long>(client.remote_encoder_counts[0]),
            static_cast<long>(client.remote_encoder_counts[1]),
            static_cast<long>(client.remote_encoder_counts[2]),
            static_cast<long>(client.remote_encoder_counts[3]));
        console_reply(response,which);
    } else if(!strncmp(line,"PWM",3)) {
        int16_t pwm[4]; uint32_t session;
        if(!motor_command_parse(line,&session,pwm)) {
            stop_control(now); console_reply("ERR PWM RANGE_OR_SYNTAX\n",which);
        } else if(pwm[0]==0 && pwm[1]==0 && pwm[2]==0 && pwm[3]==0) {
            stop_control(now); console_reply("QUEUED STOP\n",which);
        } else if(console_stop_barrier || remote_bridge.control.armed || remote_bridge.control.active ||
                  session!=client.token || !transport_ready || !link_node_submit_pwm(&client,
                    client.have_input_sequence?client.input_sequence+1U:1U,
                    pwm,1,LINK_COMMAND_MAX_TTL_MS,now)) {
            stop_control(now); console_reply("ERR PWM LINK_OR_UNSUPPORTED\n",which);
        } else {
            /* One freshly received line grants at most 200ms. Never renew a
             * cached console line; a host must send a new line to keep moving. */
            client.last_tx_ms=now-LINK_COMMAND_PERIOD_MS;
            console_reply("QUEUED PWM ttl_ms=200\n",which);
        }
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
        console_reply("INFO | RX | ENC | SETTINGS | SERVO STATUS | SERVO OFF 0/1/2/ALL | PWM token8hex m1 m2 m3 m4 (-250..250) | STOP | LED ON | LED OFF\n",which);
    } else {
        stop_control(now); console_reply("ERR COMMAND\n",which);
    }
}
static void console_poll(uint32_t now)
{
    console_stop_barrier=false;
    for(unsigned which=0;which<
#if defined(CRTC_C3_DASHBOARD)
        1
#else
        2
#endif
        ;which++) {
        if(console_used[which] && static_cast<uint32_t>(now-console_last_byte_ms[which])>=500U) {
            console_used[which]=0; console_overflow[which]=true;
            stop_control(now);
        }
        for(unsigned budget=0;budget<16;budget++) {
#if defined(CRTC_C3_DASHBOARD)
            if(!Serial.available()) break;
            int byte=Serial.read();
#else
            if(!(which==0?USBSerial.available():Serial.available())) break;
            int byte=which==0?USBSerial.read():Serial.read();
#endif
            if(byte<0) break;
            console_last_byte_ms[which]=now;
            if(byte=='\r') continue;
            if(byte=='\n') {
                if(console_overflow[which]) { stop_control(now); console_reply("ERR LINE\n",which); }
                else if(console_used[which]) {
                    console_line[which][console_used[which]]=0;
                    console_command(console_line[which],now,which);
                }
                console_used[which]=0; console_overflow[which]=false;
            } else if(byte>=32 && byte<=126 && console_used[which]<sizeof(console_line[which])-1U) {
                if(!console_overflow[which]) console_line[which][console_used[which]++]=static_cast<char>(byte);
            } else {
                console_overflow[which]=true;
                stop_control(now);
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
void crtc_cancel_control() { stop_control(millis()); }
static void renew_client_identity()
{
    /* A new reconnect nonce also invalidates USB lines retained across an STM
     * reboot whose startup timing could otherwise reproduce its old seed. */
    uint32_t next=esp_random();
    if(!next || next==client.boot_id) next=client.boot_id+0x9e3779b9UL;
    client.boot_id=next?next:1U;
}
static void fail_transport(uint32_t now)
{
    uart_faults++;
    uart_flush_input(port); xQueueReset(uart_events);
    link_node_transport_error(&client,now);
    renew_client_identity();
}
void setup()
{
    servo_ready=servo_output_begin(); servo_boot_ms=millis();
    uart_config_t config={};
#if !defined(CRTC_C3_DASHBOARD)
    USBSerial.setTxBufferSize(1024);
    USBSerial.setTxTimeoutMs(0);
    USBSerial.begin(115200);
#else
    /* Classic LuatOS board: Type-C uses its UART0 bridge. Reclaim the
     * native USB pads for the dedicated UART1; no eFuse changes. */
    USB_SERIAL_JTAG.conf0.usb_pad_enable=0;
#endif
    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
    link_node_init(&client,LINK_CLIENT,esp_random(),millis());
    remote_bridge_init(&remote_bridge,&client,esp_random());
#if defined(CRTC_C3_DASHBOARD)
    load_settings();
#endif
    ble_remote_begin(); owner_loop_ms=millis();
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
    /* BLE callbacks only queue events. All leases and UART writes remain here. */
}
static void remote_poll(uint32_t now)
{
    ble_remote_event_t event;
    now=millis();
    if(servo_ready && !remote_bridge.servo_write && now-servo_boot_ms>=servo_boot_quiet_ms)
        remote_bridge.servo_write=servo_output_write; // Availability, never a position command.
    remote_bridge_tick(&remote_bridge,now);
    for(unsigned budget=0;budget<12 && ble_remote_poll(&event);budget++) {
        now=millis(); /* Callback may have arrived after owner-loop entry. */
        if(event.kind!=BLE_REMOTE_PACKET) {
            ble_generation=event.generation;
            remote_bridge_stop(&remote_bridge,event.kind==BLE_REMOTE_OVERFLOW?
                               REMOTE_REASON_QUEUE:REMOTE_REASON_LINK,now);
            ble_remote_drop_input();
        } else if(event.generation==ble_generation && event.generation==ble_remote_generation() &&
                  ble_remote_connected() && !console_stop_barrier) {
            remote_bridge_receive(&remote_bridge,event.bytes,event.length,event.received_ms,now);
        }
    }
    if((uint32_t)(now-ble_status_ms)>=30U) {
        const uint8_t kinds[]={0,1,2,5,6,0,7,0,8,0,9};
        uint8_t packet[20]; remote_bridge_status(&remote_bridge,kinds[ble_status_kind],packet);
        ble_remote_publish(packet); ble_status_ms=now;
        if(packet[1]!=3) ble_status_kind=(ble_status_kind+1U)%(sizeof(kinds)/sizeof(kinds[0]));
    }
}
void loop()
{
    uint32_t now=millis(); uart_event_t event; uint8_t bytes[64], wire[LINK_WIRE_MAX];
    console_stop_barrier=false;
    if((uint32_t)(now-owner_loop_ms)>50U) {
        remote_bridge_stop(&remote_bridge,REMOTE_REASON_QUEUE,now); ble_remote_drop_input();
    }
    owner_loop_ms=now;
    if(!transport_ready) { console_poll(now); remote_poll(now); delay(1); return; }
    bool was_connected=client.connected;
    link_node_tick(&client,now);
    if(was_connected && !client.connected) renew_client_identity();
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
            was_connected=client.connected;
            link_node_receive(&client,bytes[i],now);
            if(was_connected && !client.connected) renew_client_identity();
        }
    }
    console_poll(now);
    remote_poll(millis());
    now=millis(); remote_bridge_tick(&remote_bridge,now);
    size_t length=link_node_transmit(&client,now,wire,sizeof(wire));
    if(length) {
        int written=uart_write_bytes(port,wire,length);
        if(written>0) uart_tx_bytes+=static_cast<uint32_t>(written);
        if(written!=static_cast<int>(length)) fail_transport(now);
        else uart_tx_frames++;
    }
    delay(1);
}
