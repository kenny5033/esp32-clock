#include "esp_http_server.h"

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

esp_err_t getHomeHandler(httpd_req_t*);
esp_err_t postHomeHandler(httpd_req_t*);

httpd_uri_t getHome = {
  .uri = "/",
  .method = HTTP_GET,
  .handler = getHomeHandler,
  .user_ctx = NULL
};

httpd_uri_t postHome = {
  .uri = "/",
  .method = HTTP_POST,
  .handler = postHomeHandler,
  .user_ctx = NULL
};

esp_err_t getHomeHandler(httpd_req_t* req) {
  httpd_resp_send(req, (char*)index_html_start, index_html_end - index_html_start);
  return ESP_OK;
}

esp_err_t postHomeHandler(httpd_req_t* req) {
  uint8_t payload[1 << 7];
  size_t payloadBytes = (sizeof(payload) < req->content_len) ? sizeof(payload) : req->content_len;

  // get payload into... payload
  if (httpd_req_recv(req, (char*)payload, payloadBytes) <= 0)
    return ESP_ERR_INVALID_STATE; // *something* happend; this is lazy

  // echo payload back
  char resp[1 << 7];
  // snprintf(resp, payloadBytes, "%s", payload);
  memcpy((void*)resp, (void*)payload, payloadBytes);
  resp[payloadBytes] = '\0';
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

  return ESP_OK;
}

