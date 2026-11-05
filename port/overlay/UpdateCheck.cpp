#include "UpdateCheck.h"
#include "AndroidBridge.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace redbound {

static const char* kLatestPage = "https://github.com/alox-dev/redbound/releases/latest";

static std::atomic<UpdateState> g_state{ UpdateState::Idle };
static std::mutex g_mutex;
static std::string g_latest, g_url;

static std::string resolveLatestUrl() {
#if defined(_WIN32)
	std::string result;
	HINTERNET session = WinHttpOpen(L"Redbound", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return result;
	WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
	HINTERNET connection = WinHttpConnect(session, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection ? WinHttpOpenRequest(connection, L"HEAD", L"/alox-dev/redbound/releases/latest", 0, WINHTTP_NO_REFERER,
	                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : 0;
	if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, 0)) {
		DWORD size = 0;
		WinHttpQueryOption(request, WINHTTP_OPTION_URL, 0, &size);
		if (size > 0) {
			std::vector<wchar_t> url(size / sizeof(wchar_t) + 1, 0);
			if (WinHttpQueryOption(request, WINHTTP_OPTION_URL, &url[0], &size))
				for (const wchar_t* c = &url[0]; *c; ++c) result += (char)(*c < 128 ? *c : '?');
		}
	}
	if (request) WinHttpCloseHandle(request);
	if (connection) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return result;
#elif defined(__ANDROID__)
	return androidResolveUrl(kLatestPage);
#else
	const std::string command = std::string("curl -sSIL -o /dev/null -w '%{url_effective}' --max-time 6 '") + kLatestPage +
	                            "' 2>/dev/null || wget -q --spider --server-response --timeout=6 '" + kLatestPage +
	                            "' 2>&1 | sed -n 's/^ *[Ll]ocation: *//p' | tail -n 1";
	std::string result;
	if (FILE* p = popen(command.c_str(), "r")) {
		char buffer[1024];
		size_t n;
		while ((n = fread(buffer, 1, sizeof(buffer), p)) > 0 && result.size() < 4096) result.append(buffer, n);
		pclose(p);
	}
	while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) result.pop_back();
	return result;
#endif
}

static std::string tagFromUrl(const std::string& url) {
	const std::string marker = "/releases/tag/";
	const size_t at = url.find(marker);
	if (at == std::string::npos) return std::string();
	std::string tag = url.substr(at + marker.size());
	const size_t end = tag.find_first_of("/?#");
	if (end != std::string::npos) tag = tag.substr(0, end);
	return tag;
}

static std::vector<int> versionParts(std::string v) {
	if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v = v.substr(1);
	std::vector<int> parts;
	int value = 0;
	bool digits = false;
	for (char c : v) {
		if (c >= '0' && c <= '9') {
			value = value * 10 + (c - '0');
			digits = true;
		} else if (c == '.') {
			parts.push_back(value);
			value = 0;
			digits = false;
		} else {
			break;
		}
	}
	if (digits) parts.push_back(value);
	while (parts.size() < 3) parts.push_back(0);
	return parts;
}

bool isNewerVersion(const std::string& candidate, const std::string& current) {
	const std::vector<int> a = versionParts(candidate), b = versionParts(current);
	for (size_t i = 0; i < 3; ++i)
		if (a[i] != b[i]) return a[i] > b[i];
	return false;
}

void startUpdateCheck(const std::string& currentVersion) {
	UpdateState expected = UpdateState::Idle;
	if (!g_state.compare_exchange_strong(expected, UpdateState::Checking)) return;
	std::thread([currentVersion] {
		const std::string url = resolveLatestUrl();
		const std::string tag = tagFromUrl(url);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_latest = tag;
			g_url = url;
		}
		if (tag.empty()) {
			printf("updates: could not check the latest release\n");
			g_state = UpdateState::Failed;
		} else if (isNewerVersion(tag, currentVersion)) {
			printf("updates: %s is available (running %s)\n", tag.c_str(), currentVersion.c_str());
			g_state = UpdateState::Available;
		} else {
			printf("updates: up to date (latest %s, running %s)\n", tag.c_str(), currentVersion.c_str());
			g_state = UpdateState::UpToDate;
		}
	}).detach();
}

UpdateState updateState() { return g_state; }

std::string latestVersion() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_latest;
}

std::string latestReleaseUrl() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_url.empty() ? std::string(kLatestPage) : g_url;
}

}
