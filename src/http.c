#include "gwrun.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t write_body(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GwBuffer *buf = (GwBuffer *)userdata;
	size_t len = size * nmemb;
	return buffer_append(buf, ptr, len) ? len : 0;
}

static int file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		return 0;
	}
	fclose(f);
	return 1;
}

static const char *default_ca_bundle(void)
{
	const char *env = getenv("CURL_CA_BUNDLE");
	if (env && env[0]) {
		return env;
	}
	if (file_exists("ca-bundle.crt")) {
		return "ca-bundle.crt";
	}
#ifdef _WIN32
	if (file_exists("C:\\msys64\\ucrt64\\etc\\ssl\\certs\\ca-bundle.crt")) {
		return "C:\\msys64\\ucrt64\\etc\\ssl\\certs\\ca-bundle.crt";
	}
#endif
	return NULL;
}

int http_post_json(const GwOptions *opts, const char *body, GwHttpResponse *response)
{
	CURL *curl;
	CURLcode rc;
	struct curl_slist *headers = NULL;
	const char *auth;
	const char *ca_bundle;

	response->status = 0;
	response->error[0] = '\0';
	buffer_init(&response->body);

	curl = curl_easy_init();
	if (!curl) {
		snprintf(response->error, sizeof(response->error), "failed to initialize libcurl");
		return 0;
	}

	auth = auth_header_get(1);

	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: application/json");
	if (auth && auth[0]) {
		GwBuffer auth_header;
		buffer_init(&auth_header);
		if (!buffer_append_cstr(&auth_header, "Authorization: ") ||
			!buffer_append_cstr(&auth_header, auth)) {
			buffer_free(&auth_header);
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
			snprintf(response->error, sizeof(response->error), "out of memory building auth header");
			return 0;
		}
		headers = curl_slist_append(headers, auth_header.data);
		buffer_free(&auth_header);
	}

	curl_easy_setopt(curl, CURLOPT_URL, opts->server);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response->body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, opts->timeout_ms > 0 ? opts->timeout_ms : 30000L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "gwrun/" GWRUN_VERSION);

	ca_bundle = default_ca_bundle();
	if (ca_bundle) {
		curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);
	}

	rc = curl_easy_perform(curl);
	if (rc != CURLE_OK) {
		snprintf(response->error, sizeof(response->error), "%s", curl_easy_strerror(rc));
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		return 0;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return response->status >= 200 && response->status < 300;
}

void http_response_free(GwHttpResponse *response)
{
	buffer_free(&response->body);
}
