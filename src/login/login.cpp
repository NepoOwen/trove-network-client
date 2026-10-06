// File: login/login.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "login.hpp"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <vector>

namespace login {
namespace {

constexpr const char* BASE_URL = "auth.trionworlds.com";
constexpr const char* USER_AGENT = "Glyph (stable-251-1-a-335833)";

std::string to_lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

#ifdef _WIN32
std::string to_utf8(const wchar_t* w, int len = -1) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    if (len == -1 && !s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
#endif  // _WIN32

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

struct HttpResponse {
    uint16_t status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

#ifdef _WIN32
HttpResponse http_post(const std::string& host, const std::string& path, const std::string& body, const std::string& content_type) {
    HttpResponse resp;

    HINTERNET session = WinHttpOpen(to_wide(USER_AGENT).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return resp;
    HINTERNET conn = WinHttpConnect(session, to_wide(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) { WinHttpCloseHandle(session); return resp; }
    HINTERNET req = WinHttpOpenRequest(conn, L"POST", to_wide(path).c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return resp; }

    std::wstring headers = L"Content-Type: " + to_wide(content_type) + L"\r\n";
    WinHttpAddRequestHeaders(req, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return resp;
    }

    DWORD status = 0, status_size = sizeof(status);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX))
        resp.status = (uint16_t)status;

    DWORD hdr_len = 0;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                        WINHTTP_NO_OUTPUT_BUFFER, &hdr_len, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && hdr_len > 0) {
        std::wstring raw(hdr_len / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                raw.data(), &hdr_len, WINHTTP_NO_HEADER_INDEX)) {
            std::string h = to_utf8(raw.c_str(), (int)(hdr_len / sizeof(wchar_t)));
            size_t pos = 0;
            while (pos < h.size()) {
                size_t eol = h.find("\r\n", pos);
                if (eol == std::string::npos) eol = h.size();
                std::string line = h.substr(pos, eol - pos);
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                    resp.headers[line.substr(0, colon)] = trim(line.substr(colon + 1));
                pos = eol + 2;
            }
        }
    }

    DWORD avail = 0;
    std::vector<char> chunk;
    do {
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        chunk.resize(avail);
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read)) break;
        resp.body.append(chunk.data(), read);
    } while (avail > 0);

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return resp;
}

#else  // !_WIN32 (libcurl)

static size_t curl_write_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    ((std::string*)userdata)->append(ptr, total);
    return total;
}

static size_t curl_write_header(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    std::string line(ptr, total);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        size_t s = val.find_first_not_of(" \t");
        val = (s == std::string::npos) ? std::string() : val.substr(s);
        (*(std::map<std::string, std::string>*)userdata)[key] = val;
    }
    return total;
}

HttpResponse http_post(const std::string& host, const std::string& path, const std::string& body, const std::string& content_type) {
    HttpResponse resp;
    CURL* c = curl_easy_init();
    if (!c) return resp;

    std::string url = "https://" + host + path;
    std::string ct = "Content-Type: " + content_type;
    struct curl_slist* hdrs = curl_slist_append(nullptr, ct.c_str());

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_body);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, curl_write_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &resp.headers);

    if (curl_easy_perform(c) == CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        resp.status = (uint16_t)status;
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    return resp;
}

#endif  // !_WIN32

// Finds <tag ...>value</tag>, tolerating attributes on the opening tag.
std::string extract_xml_value(const std::string& text, const std::string& tag) {
    std::string open = "<" + tag;
    size_t p = text.find(open);
    if (p == std::string::npos) return {};
    size_t gt = text.find('>', p);
    if (gt == std::string::npos) return {};
    size_t e = text.find("</" + tag, gt);
    if (e == std::string::npos) return {};
    return text.substr(gt + 1, e - gt - 1);
}

std::string extract_auth_ticket_xml(const std::string& text) {
    size_t start = text.find("<?xml");
    if (start == std::string::npos) start = text.find("<authTicket");
    if (start == std::string::npos) return {};
    size_t end = text.find("</authTicket>", start);
    if (end == std::string::npos) return {};
    return text.substr(start, end + 13 - start);
}

std::string extract_signature_b64(const std::string& text) {
    size_t pos = text.find("Signature:");
    if (pos == std::string::npos) return {};
    pos += 10;
    while (pos < text.size() && std::isspace((unsigned char)text[pos])) pos++;
    size_t end = pos;
    while (end < text.size() && !std::isspace((unsigned char)text[end])) end++;
    return text.substr(pos, end - pos);
}

static FILE* safe_fopen(const char* path, const char* mode) {
#ifdef _MSC_VER
    FILE* f = nullptr;
    fopen_s(&f, path, mode);
    return f;
#else
    return fopen(path, mode);
#endif
}

std::string read_file(const char* path) {
    FILE* f = safe_fopen(path, "rb");
    if (!f) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

bool write_file(const char* path, const std::string& data) {
    FILE* f = safe_fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

} // namespace

Result authenticate(const std::string& email, const std::string& password, const std::string& auth_code) {
    Result r;
    std::string em = trim(email);
    if (em.empty() || password.empty()) { r.error = "Email and password are required"; return r; }
    std::string code = trim(auth_code);

    std::string form = "username=" + url_encode(em)
                     + "&password=" + url_encode(password)
                     + "&channel=131"
                     + "&includeStoreToken=1"
                     + "&publicMachine=0";
    if (!code.empty()) form += "&token=" + url_encode(code);

    HttpResponse resp = http_post(BASE_URL, "/multiauth/v1_2", form, "application/x-www-form-urlencoded");

    std::string err = resp.headers["X-Trionworlds-Error"];
    std::string err_msg = resp.headers["X-Trionworlds-Error-Message"];
    std::string tq = to_lower(resp.headers["X-Trionworlds-Token-Required"]);
    bool email_2fa = tq.find("email") != std::string::npos;
    bool mobile_2fa = tq.find("mobile") != std::string::npos;

    if (email_2fa || mobile_2fa) {
        if (!code.empty()) {
            r.error = std::string("Invalid authentication code. Please check your ") +
                      (mobile_2fa ? "authenticator app" : "email") + " and try again.";
            return r;
        }
        r.ok = true;
        r.token_required = true;
        r.token_required_type = mobile_2fa ? "mobile" : "email";
        return r;
    }

    if (!err.empty()) {
        r.error = "Glyph login failed: " + err;
        if (!err_msg.empty()) r.error += " (" + err_msg + ")";
        return r;
    }
    if (resp.status < 200 || resp.status >= 300) {
        r.error = resp.status == 500 ? "Account does not exist"
                                      : "Glyph login failed with HTTP " + std::to_string(resp.status);
        return r;
    }

    std::string t = trim(resp.body);
    if (to_lower(t).rfind("<!", 0) == 0 || to_lower(t).rfind("<html", 0) == 0) {
        r.error = t.find("rror 500") != std::string::npos ? "Account does not exist"
                                                            : "Glyph returned an unexpected response";
        return r;
    }

    r.ticket.xml = extract_auth_ticket_xml(resp.body);
    r.ticket.signature = extract_signature_b64(resp.body);
    r.ticket.raw = resp.body;
    r.ticket.account_id = std::strtoull(extract_xml_value(resp.body, "accountId").c_str(), nullptr, 10);
    r.ticket.email = extract_xml_value(resp.body, "email");
    r.ticket.channel_id = extract_xml_value(resp.body, "channelId");

    if (r.ticket.xml.empty()) { r.error = "Glyph login failed: no auth ticket in response"; return r; }
    r.ok = true;
    return r;
}

std::string default_auth_servers(const std::string& region) {
    static const char* NA = "dal-c35-b05.dal.triongames.com:6560|dal-c35-b06.dal.triongames.com:6560|dal-c35-b07.dal.triongames.com:6560|dal-c35-b08.dal.triongames.com:6560|dal-c35-b09.dal.triongames.com:6560";
    static const char* EU = "ams-c12-b01.ams.triongames.com:6560|ams-c12-b02.ams.triongames.com:6560|ams-c12-b03.ams.triongames.com:6560|ams-c12-b04.ams.triongames.com:6560|ams-c12-b05.ams.triongames.com:6560";
    static const char* PTS = "auth-pcpts01.trovegame.com:6560|auth-pcpts02.trovegame.com:6560";

    std::string r = to_lower(region);
    //for (auto& c : r) c = (char)std::toupper((unsigned char)c);
    if (r == "na") return NA;
    if (r == "pts") return PTS;
    return EU;
}

bool load_cached_ticket(uint64_t account_id, Ticket& out) {
    std::string base = "auth/" + std::to_string(account_id);
    out.raw = read_file((base + "_raw.bin").c_str());
    if (out.raw.empty()) return false;
    out.xml = extract_auth_ticket_xml(out.raw);
    out.signature = extract_signature_b64(out.raw);
    if (out.xml.empty() || out.signature.empty()) return false;
    out.account_id = account_id;
    out.email = extract_xml_value(out.xml, "email");
    out.channel_id = extract_xml_value(out.xml, "channelId");
    return out.signature.size() == 64;
}

bool save_ticket(const Ticket& t) {
    if (!t.account_id) return false;
    std::error_code ec;
    std::filesystem::create_directories("auth", ec);
    std::string base = "auth/" + std::to_string(t.account_id);
    return write_file((base + "_raw.bin").c_str(), t.raw);
}

std::vector<uint64_t> list_cached_accounts() {
    std::vector<uint64_t> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("auth", ec)) {
        if (ec) break;
        std::string name = entry.path().filename().string();
        size_t pos = name.find("_raw.bin");
        if (pos == std::string::npos) continue;
        uint64_t id = std::strtoull(name.substr(0, pos).c_str(), nullptr, 10);
        if (id) out.push_back(id);
    }
    return out;
}

}
