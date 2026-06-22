#include "dns_server.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DNSServer";

#define DNS_PORT 53
#define DNS_BUFFER_SIZE 512

static TaskHandle_t s_dns_task_handle = NULL;
static int s_dns_socket = -1;
static bool s_dns_running = false;

// DNS Header Struct
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

// DNS Question Tail (Type + Class)
typedef struct __attribute__((packed)) {
    uint16_t qtype;
    uint16_t qclass;
} dns_question_tail_t;

// DNS Answer Struct
typedef struct __attribute__((packed)) {
    uint16_t name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rd_length;
    uint32_t rdata;
} dns_answer_t;

static void dns_server_task(void *pvParameters)
{
    uint8_t buffer[DNS_BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    ESP_LOGI(TAG, "DNS Server starting on port %d...", DNS_PORT);

    // Create UDP Socket
    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        s_dns_running = false;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    // Bind Socket
    if (bind(s_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
        close(s_dns_socket);
        s_dns_socket = -1;
        s_dns_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Server bound to port %d", DNS_PORT);

    while (s_dns_running) {
        int len = recvfrom(s_dns_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            if (errno == EBADF || !s_dns_running) {
                break; // Socket was closed / stopping
            }
            ESP_LOGW(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (len < sizeof(dns_header_t)) {
            continue; // Query too small
        }

        dns_header_t *query_header = (dns_header_t *)buffer;
        uint16_t qd_count = ntohs(query_header->qd_count);

        if (qd_count == 0) {
            continue; // No questions
        }

        // Parse Question Section to find end of query name
        int name_len = 0;
        uint8_t *name_ptr = buffer + sizeof(dns_header_t);
        while (name_ptr < buffer + len) {
            uint8_t label_len = *name_ptr;
            if (label_len == 0) {
                name_ptr++; // skip null byte termination
                break;
            }
            name_ptr += label_len + 1;
        }

        // Verify bounds
        if (name_ptr + sizeof(dns_question_tail_t) > buffer + len) {
            continue; // Out of bounds
        }

        dns_question_tail_t *q_tail = (dns_question_tail_t *)name_ptr;
        name_ptr += sizeof(dns_question_tail_t); // pointing to where answers will start

        int question_section_len = name_ptr - buffer;

        // Build Response
        dns_header_t *resp_header = (dns_header_t *)buffer;
        resp_header->flags = htons(0x8400); // Standard Query Response, Authoritative Answer, No Error
        resp_header->an_count = htons(1);   // One answer record
        resp_header->ns_count = 0;
        resp_header->ar_count = 0;

        // Append Answer Section
        dns_answer_t answer;
        answer.name = htons(0xC00C);         // Compression pointer to domain name in query (offset 12)
        answer.type = htons(0x0001);         // Type A (IPv4)
        answer.class = htons(0x0001);        // Class IN (Internet)
        answer.ttl = htonl(60);              // Time-To-Live (60 seconds)
        answer.rd_length = htons(4);         // 4 bytes for IPv4 address
        answer.rdata = inet_addr("192.168.4.1"); // Target portal IP

        // Copy answer to buffer end
        if (question_section_len + sizeof(dns_answer_t) <= DNS_BUFFER_SIZE) {
            memcpy(name_ptr, &answer, sizeof(dns_answer_t));
            int resp_len = question_section_len + sizeof(dns_answer_t);
            
            // Send back to client
            sendto(s_dns_socket, buffer, resp_len, 0, (struct sockaddr *)&client_addr, addr_len);
        }
    }

    ESP_LOGI(TAG, "DNS Server task shutting down...");
    if (s_dns_socket >= 0) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t DnsServer_Start(void)
{
    if (s_dns_running) {
        return ESP_OK;
    }

    s_dns_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        dns_server_task,
        "dns_server_task",
        3072,
        NULL,
        5,
        &s_dns_task_handle,
        0
    );

    if (ret != pdPASS) {
        s_dns_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void DnsServer_Stop(void)
{
    if (!s_dns_running) {
        return;
    }

    s_dns_running = false;
    if (s_dns_socket >= 0) {
        shutdown(s_dns_socket, SHUT_RDWR);
        close(s_dns_socket);
        s_dns_socket = -1;
    }
}
