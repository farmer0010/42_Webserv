# 42_Webserv — 네트워크 레이어 진단 및 진행 현황

> 담당: 메인 루프 / 이벤트 디스패치 / 네트워크
> 주요 클래스: `ServerManager`, `ServerSocket`, `ClientSocket`
> 작성 기준 브랜치: `feature/network`

---

## 1. 담당 경계

| 영역 | 클래스 | 담당 |
|---|---|---|
| 1. Config 파싱 | `Config`, `ConfigParser`, `ServerBlock`, `Location` | 타 팀원 |
| 2. 메인루프 / 이벤트 디스패치 / 네트워크 | `ServerManager`, `ServerSocket`, `ClientSocket` | **본인** |
| 3. HTTP / CGI 로직 | `HttpRequest`, `HttpResponse`, `RequestHandler`, `Cgi` | 타 팀원 |

본 문서는 2번 영역 관점의 진단 결과 및 진행 추적이며, 다른 영역에 대한 변경은 모두 "공유 항목"으로 분리한다.

---

## 2. 초기 진단 — 처음 발견한 17개 항목

### 치명적

| # | 항목 | 위치 |
|---|---|---|
| 1 | CGI 결과가 응답으로 안 나간다 | `ClientSocket::handleCgiRead` |
| 2 | 헤더 키 대소문자 불일치 (`Host`/`Connection`) | `selectServerBlock`, `isKeepAlive` |
| 3 | `send()` 에 SIGPIPE 보호 없음 | `ClientSocket::handleWrite` / 시스템 전반 |
| 4 | chunked Transfer-Encoding 미처리 | `ClientSocket::isRequestComplete` |
| 5 | `EPOLLHUP` 를 `EPOLLIN` 보다 먼저 처리 | `ServerManager::dispatchEvents` |

### 중요 (HTTP/1.1 준수 / robustness)

| # | 항목 | 위치 |
|---|---|---|
| 6 | 413 응답 후 `_recv_buffer` 점유 유지 | `ClientSocket::sendErrorResponse` |
| 7 | `epoll_wait(-1)` — 타임아웃 부재 | `ServerManager::run` |
| 8 | keep-alive 시 파이프라인 데이터 손실 | `ClientSocket::resetForKeepAlive` |
| 9 | HTTP/1.1 Host 헤더 검증 누락 | `ClientSocket::processRequest` |
| 10 | 응답 reason phrase 결손 (413만 세팅) | `ClientSocket::sendErrorResponse` |
| 11 | `Content-Length` 파싱이 `atoi` (음수/오버플로/중복/TE 동시) | `isRequestComplete`, `isBodyTooLarge` |

### 작지만 거슬리는 것

| # | 항목 | 위치 |
|---|---|---|
| 12 | `removeClient` 가 존재 안 하는 fd에도 `delete _clients[fd]` | `ServerManager::removeClient` |
| 13 | `accept()` 실패 후 단순 return — EMFILE 시 epoll 스핀 위험 | `ServerManager::handleAccept` |
| 14 | `getaddrinfo` 결과 NULL 가드 누락 | `ServerSocket::init` |
| 15 | `addCgiFd` 실패 후 클라이언트에 알리지 않음 → 영원히 매달림 | `ServerManager::addCgiFd` |
| 16 | CGI 동시 readiness 미처리 (latency only) | `ServerManager::dispatchEvents` |
| 17 | CGI 자식 회수(waitpid) 없음 — 좀비 누적 | `ServerManager::removeCgi` |

### 초기 진단 외 추가 발견

| 항목 | 위치 |
|---|---|
| `_state != READING` 인데 `handleRead` 들어오면 잘못된 재처리 가능 | `ClientSocket::handleRead` |
| `ClientSocket::_cgi` dead member (NULL 외 미사용) | `ClientSocket` 헤더 |
| `resetForKeepAlive` 가 `_request_handler` 갱신 안 함 → CGI 누수 | `ClientSocket::resetForKeepAlive` |
| `RequestHandler::init` 가 `ServerBlock*` 미수령 → Location 매칭 항상 fallback | `processRequest` ↔ `RequestHandler::init` |
| Makefile에 `http/*.cpp` + Config 관련 cpp 누락 → 링크 실패 | `Makefile` |

---

## 3. 처리 이력 (commit 단위)

### `99ad1d9` — fix : 네트워크 안정성 개선 및 CGI 좀비 회수

- **#3** SIGPIPE 무시 (`main` 에 `std::signal(SIGPIPE, SIG_IGN)`)
- **#2** 헤더 키 lowercase 매칭 (`headers.find("host")`, `find("connection")`, value 소문자 비교)
- **#17** CGI 좀비 회수 — `dispatchEvents` 의 CGI 정상 종료 / 에러 종료 / 클라이언트 EPOLLERR|HUP 세 시점에 `waitpid(pid, NULL, WNOHANG)` 호출
- 추가: `handleRead` 진입 가드 (`_state != READING` 이면 return), `ClientSocket::getCgiPid()` getter 신설

### `b911ec8` — refactor : ClientSocket keep-alive 안정화 및 미사용 멤버 정리

- 추가: `resetForKeepAlive` 가 `_request_handler = RequestHandler();` 로 재생성 → 이전 요청의 cgi 포인터/내부 상태 잔존 차단
- 추가: `ClientSocket::_cgi` dead member 제거 (RequestHandler::getCgi() lookup 으로 대체됨)

### `43878c4` — merge : dev 통합

- 자동 머지 + `main.cpp` 충돌 해결 (SIGPIPE 무시 + dev 의 `config_path` 결정 로직 양쪽 유지)

### `b7f78ed` — feat : dev 머지에 따른 RequestHandler 연동 + Makefile 누락 보강

- **#1** CGI 응답이 응답 직렬화로 흘러가도록 `handleCgiRead` 가 `_response = _request_handler.getResponse();` 로 갱신
- 추가: `processRequest` 에서 `selectServerBlock()` 결과를 `RequestHandler::init(req, sb)` 에 전달 → Location 매칭 실제 활성화. NULL 시 500 가드
- Makefile `SRCS_FILES` 에 `http/HttpRequest.cpp`, `http/HttpResponse.cpp`, `http/RequestHandler.cpp`, `http/Cgi.cpp` 추가

### `514aa48` — fix : chunked 요청 종결 마커 인식 + Cgi.hpp 빌드 차단 해소

- **#4** `isRequestComplete` 에서 `Transfer-Encoding: chunked` 인 경우 body 영역에서 `\r\n0\r\n\r\n` 종결 마커(또는 body 시작이 `0\r\n\r\n` 인 희귀 케이스) 확인
- Transfer-Encoding 값은 소문자화하여 케이스 무관 매칭
- `includes/Cgi.hpp` 에 `<sys/types.h>` 추가 — `ssize_t` 미정의로 RequestHandler.cpp 컴파일이 실패하던 차단 해소 (HTTP 영역, 사용자 명시 허락 하에 진행)

### `ab419ee` — feat : 평가용 정적 페이지 및 CGI 샘플 추가

- `www/index.html` — 랜딩 페이지 (라우트 네비 + 업로드/CGI 폼)
- `www/error_pages/404.html`, `50x.html` — conf 의 `error_page` 매핑 대상
- `cgi-bin/hello.py` (실행권한 755) — REQUEST_METHOD/QUERY_STRING 등 환경변수 echo + POST 본문 echo

### `efdfc85` — feat : 네트워크 핵심 지점 로그 출력 추가

흐름 추적/디버깅용 `std::cout` / `std::cerr` 직접 박음. 정상 흐름은 cout, 비정상은 cerr 분리.

- `ServerSocket::init` listen 직후 `[server] listening host:port (fd=X)`. 기존 ctor/dtor 노이즈 두 줄 제거
- `ServerManager::handleAccept`: 성공 `[accept] fd=X from IP:PORT`, epoll_ctl 실패 `[epoll_ctl] ADD failed fd=X`
- `ServerManager::removeClient`: `[close] client fd=X`
- `ServerManager::sweepTimeouts`: `[timeout] fd=X (idle|cgi)`
- `ClientSocket::handleRead` (recv == 0): `[peer-closed] fd=X`
- `ClientSocket::processRequest` (parse 후): `[request] METHOD URI VER fd=X`
- `ClientSocket::processRequest` (CGI 진입): `[cgi] spawn pid=X fd=Y`
- `ClientSocket::handleCgiRead` (EOF): `[cgi] done pid=X fd=Y`
- `ClientSocket::handleWrite` (송신 완료): `[response] sent N bytes fd=X (keep-alive|close)`
- `ClientSocket::sendErrorResponse`: cerr `[error] STATUS fd=X`

### `226d384` — fix : HttpRequest ctor/dtor inline 정의 추가 (빌드 차단 해소)

- `HttpRequest::HttpRequest() / ~HttpRequest()` 가 헤더 선언만 있고 정의 없어 발생한 링크 단계 undefined reference 차단
- 헤더에 `{};` 형태 inline 빈 정의 추가 — 본인 영역 외이지만 빌드 차단 해소를 위한 임시 조치, 사용자 명시 허락 하에 진행
- HTTP 담당자가 본인 컨벤션(cpp 정의 / 그대로 inline 유지 등)을 정하면 그쪽으로 이관 가능

### `952f3d4` — feat : 클라이언트 idle / CGI 실행 타임아웃 도입

- **#7** `ServerManager.hpp` 에 상수 추가
  - `EPOLL_WAIT_INTERVAL_MS = 1000`
  - `CLIENT_IDLE_TIMEOUT = 60` (초)
  - `CGI_TIMEOUT = 30` (초)
- `ServerManager::sweepTimeouts()` 신설 — `_clients` 순회하여 두 조건(idle / CGI) 만료 시 `kill(SIGKILL)` + `waitpid` + CGI fd 정리 + `removeClient`
- `ServerManager::run()` 의 `epoll_wait(-1)` → `EPOLL_WAIT_INTERVAL_MS` 로 변경하고 매 cycle 끝에 `sweepTimeouts()` 호출
- `ClientSocket::_cgi_start_time` 멤버 추가 (생성자/`init`/`resetForKeepAlive` 에서 0 초기화, `processRequest` 의 CGI 진입 직전에 `time(NULL)` 기록)
- `getLastActiveTime()`, `getCgiStartTime()` getter 추가
- `_last_active_time` 갱신을 모든 handle*(`handleRead` 외에 `handleWrite`, `handleCgiWrite`, `handleCgiRead`)에 보강 → idle 만료가 진행 중인 요청을 잘못 잡지 않도록

### `f1c1dd9` — fix : Content-Length 파싱 강화 및 RFC 7230 §3.3.3 위반 조합 차단

- **#11** `ClientSocket` 에 헬퍼 3개 신설
  - `parseContentLength(const std::string&, size_t&) const` — 앞뒤 OWS 허용, ASCII digit 만 허용, 누적 곱셈 시 `numeric_limits<size_t>::max()` 비교로 자체 오버플로 검출 (`errno` 미사용)
  - `countRawHeader(const std::string& key) const` — 헤더 영역에서 `"\r\n" + key + ":"` 패턴 카운트 (중복 헤더 검출)
  - `validateHeaders() const` — Content-Length 중복 / Content-Length+Transfer-Encoding 공존 / CL 비숫자·오버플로 → 400
- `handleRead` 가 `isHeaderComplete` 직후 `validateHeaders` 호출, 400 이면 즉시 `sendErrorResponse(400)`
- `isRequestComplete`, `isBodyTooLarge` 의 `std::atoi` 제거 → `parseContentLength` 사용. 음수 입력에 거대값으로 우연히 413 처리되던 동작 제거 (지금은 validateHeaders 가 400 으로 잘라냄)
- `#include <limits>` 추가

### `9c71bca` — merge : dev 통합 (config 파싱 에러 처리 강화)

- dev 의 `f1350c0` "config 파싱 에러 처리 강화" 흡수
- 자동 머지, 충돌 없음 (Config 영역만 변경됨)
- `main.cpp` 가 Config 파싱과 ServerManager 실행을 별도 try/catch 로 분리, 로그 prefix 도 `[Config Error]` / `[Runtime Error]` 로 구분되도록 변경됨
- 본인 담당 영역(`ServerManager`/`ServerSocket`/`ClientSocket`)에는 영향 없음. 빌드는 여전히 `HttpRequest` ctor/dtor 미정의로 링크 차단 상태

### `2a07321` — fix : HTTP/1.1 검증 강화, 에러 응답 정리, EPOLLHUP 처리 순서 수정

- **#6** `sendErrorResponse` 진입부에 `_recv_buffer.clear()` 추가 → 에러 응답 후 잔여 요청 데이터 점유 해제
- **#10** `sendErrorResponse` 에 status_code 별 reason phrase 매핑 (400/403/404/405/413/500/501/505)
- **#9** `processRequest` 에서 `HTTP/1.1` 인데 `host` 헤더 없으면 400 반환
- **#5** `dispatchEvents` 의 클라이언트 분기에서 EPOLLHUP/EPOLLERR 즉시 종료를 제거. `peer_gone` 플래그로 표시한 뒤 EPOLLIN/EPOLLOUT 처리를 먼저 끝내고, 송신 잔여(`state != WRITING`)가 없을 때만 정리. 마지막 요청 직후 FIN 으로 응답이 못 나가던 케이스 해소

### `80ead96` — chore : 네트워크 레이어 안정화 마무리 + 진단 문서/빌드 환경

- **#14** `ServerSocket::init` 의 `getaddrinfo` 성공 후 `res == NULL` 가드 추가
- **#15** `ServerManager.hpp` 의 `addCgiFd` 반환 `void → bool` (구현부 시그니처와 sync). 호출자에서 실패 시 SIGKILL/waitpid/cleanup 분기 활성화
- `NETWORK_REVIEW.md` 진단 추적 문서 트래킹 시작
- `Dockerfile` 추가 (macOS 에서 epoll 빌드 검증용)

### `136a885` — fix : CGI pipe I/O 음수 반환 시 DONE 전이 제거 (A-1)

- `handleCgiWrite/handleCgiRead` 의 `cgi->writeToPipe/readFromPipe` 반환 `n<0` 분기를 DONE 전이가 아닌 그냥 `return` 으로 변경
- 서브젝트 룰상 read/write 후 errno 검사 금지 → EAGAIN / 실제 에러 구분 불가. EAGAIN 한 번에 연결이 끊기던 회귀 차단
- LT 모드라 다음 epoll 사이클이 재시도, 실제 에러는 EPOLLHUP/EOF 경로로 자연 정리, 진짜 hang 은 `CGI_TIMEOUT(30s)` 처리
- 검증: siege CGI c=2 98.5% → 99.9%, c=3 새로 98.7% 확보

### `(예정)` — feat : location 블록별 `client_max_body_size` 지원

- 평가용 conf (`cgi_tester` 동봉 conf) 가 `location /post_body { client_max_body_size 100; }` 사용. 기존 파서는 location 블록의 해당 directive 를 모름 → 시작 시점에 conf 에러.
- 변경 (사용자 명시 허락 하에 Config 영역도 함께)
  - `Location` 클래스에 `_client_max_body_size` 멤버 추가, `LOCATION_BODY_SIZE_UNSET` sentinel (`(size_t)-1`) 도입. 0 은 nginx 규약상 "제한 없음" 이미 차지 중이라 unset 표시로 못 씀.
  - `Location::init()` 가 sentinel 로 초기화.
  - `ConfigParser` 의 location 블록 분기에 `client_max_body_size` 인식 추가.
- 네트워크 영역
  - `ClientSocket::extractRawUri()` — raw buffer 의 request-line 에서 URI 추출 (파싱 전 location 매칭용)
  - `ClientSocket::resolveMaxBodySize()` — location 값 우선, sentinel 이면 server 값으로 폴백
  - `isBodyTooLarge` 가 위 헬퍼 사용
- 검증 (`conf/test_body_size.conf` 추가)
  - server 1MB / location `/post_body` 100B 한도 정의
  - `POST /` 500KB → 201, 2MB → 413 (server 한도)
  - `POST /post_body` 100B → 201, 101B → 413, 1KB → 413 (location 우선)

### `939dcc9` — fix : CGI 부하 시 SIGSEGV 해소 — removeClient 자식/매핑 일괄 정리

- c≥5 동시 CGI 시 `exit 139 (SIGSEGV)` 로 죽던 문제 해결
- `removeClient`
  - `_cgi_to_client` 에서 해당 `client_fd` 참조하는 모든 cgi fd 일괄 `removeCgi` (stale 엔트리 누락 경로 차단)
  - 살아있는 CGI 자식 있으면 `SIGKILL` + 블로킹 `waitpid` 로 좀비/orphan 방지 (기존 3곳 WNOHANG 회수가 미달인 케이스 보강)
- `dispatchEvents` CGI 분기에 `_clients[client_fd]` NULL/없음 가드 — 마지막 방어선
- 검증: c=5 0%(crash) → 81.3%, c=8 0% → 43.9%, c=15 생존, 정적 GET 회귀 없음

---

## 4. 처리 현황 요약표

| # | 항목 | 상태 |
|---|---|---|
| 1 | CGI 응답 미반영 | ✅ `b7f78ed` |
| 2 | 헤더 키 대소문자 | ✅ `99ad1d9` |
| 3 | SIGPIPE | ✅ `99ad1d9` |
| 4 | chunked Transfer-Encoding | ✅ `514aa48` |
| 5 | EPOLLHUP 순서 | ✅ `2a07321` |
| 6 | 413 후 buffer 유지 | ✅ `2a07321` |
| 7 | epoll_wait 타임아웃 | ✅ `952f3d4` |
| 8 | keep-alive buffer 손실 | ❌ (시지 6% 재현) |
| 9 | Host 검증 | ✅ `2a07321` |
| 10 | reason phrase | ✅ `2a07321` |
| 11 | Content-Length atoi | ✅ `f1c1dd9` |
| 12 | removeClient 가드 | ✅ `939dcc9` |
| 13 | accept() EMFILE 가드 | ❌ |
| 14 | getaddrinfo NULL | ✅ `80ead96` |
| 15 | addCgiFd 실패 처리 | ✅ `80ead96` |
| 16 | CGI 동시 readiness | ❌ (latency only) |
| 17 | CGI 좀비 회수 | ✅ `99ad1d9` + `939dcc9` (보강) |
| 추가 | handleRead 상태 가드 | ✅ `99ad1d9` |
| 추가 | dead member `_cgi` | ✅ `b911ec8` |
| 추가 | resetForKeepAlive 핸들러 갱신 | ✅ `b911ec8` |
| 추가 | Location 매칭 wiring | ✅ `b7f78ed` |
| 추가 | Makefile 보강 | ✅ `b7f78ed` |
| A-1 | CGI pipe I/O n<0 → DONE 오인 (EAGAIN 한번에 연결 끊김) | ✅ `136a885` |
| B   | c≥5 CGI SIGSEGV (orphan _cgi_to_client + 미회수 자식) | ✅ `939dcc9` |
| B-fu | Phantom EPOLLOUT — `[accept] → sent 0 bytes` 무요청 응답 | ❌ |
| 추가 | location 블록별 `client_max_body_size` 지원 (Location 멤버 + 파서 + 우선순위) | ✅ (예정 commit) |

**진척**: 처음 진단 17개 중 **14개 완료**, 진단 외 추가 7개 완료, 잔여 3개(#8/#13/#16) + 신규 추적 1개(B-fu).

---
## 5. HTTP / Config 담당자 공유 항목

### 🟢 해소됨

- ~~**`HttpRequest::HttpRequest()` / `~HttpRequest()` 정의 누락**~~ — 어느 시점에 추가되어 링크 통과 확인됨. 직전 commit `f1c1dd9` 이후 진행한 타임아웃 작업에서 Docker 빌드 결과 `/app/webserv` 바이너리 생성 성공.

### 🔴 시급 (시지 평가 영향)

- **`allow_methods` 미적용 — 서브젝트 핵심 요구사항 위반** (2026-06-06 발견)
  - 서브젝트: *"The configuration file should set the HTTP methods accepted by the route"*
  - 현 동작: `RequestHandler::processRequest` (`srcs/http/RequestHandler.cpp:47-86`) 가 `loc.getAllowMethods()` 를 조회하지 않고 메서드만 보고 분기.
  - 영향:
    - `POST /` → **201** (실제 `www/uploaded_file.bin` 생성) — conf 의 `location / { allow_methods GET; }` 무시
    - `DELETE /` → 403 (디렉터리 unlink 시도 실패) — 405 가 맞음
    - 평가자가 `curl -X POST localhost:8080/ -d x` 한 번에 잡히는 항목
  - 패치 방향: `processRequest` 의 CGI/메서드 분기 진입 전에 가드.
    ```cpp
    const std::vector<std::string>& allowed = loc.getAllowMethods();
    if (std::find(allowed.begin(), allowed.end(), request.getMethod()) == allowed.end()) {
        generateErrorPage(405);
        return this->response;
    }
    ```
  - 검증: `GET / POST / DELETE /` 가 각각 200/405/405 가 되는지, `/uploads` 는 GET/POST/DELETE 모두 정상 동작 유지.

- **dev `92bb6b4` 회귀 — `Cgi.cpp` double-close**
  - 변경 내용: `~Cgi` 가 `pipe_in[0]` 까지 close 하도록 추가. 그러나 `execute()` 의 부모 성공 경로(`Cgi.cpp:110-114`)와 fork 실패 경로(`Cgi.cpp:77-81`) 가 close 후 `-1` 미설정.
  - 결과: **모든 정상 CGI 요청마다 `pipe_in[0]` double-close** → 무관한 fd 가 닫혀 미스터리 EBADF / 다른 클라이언트 socket 손실. siege 고동시성에서 폭발.
  - 패치 방향: close 직후 `pipe_xx[i] = -1;` 한 줄씩.
- **autoindex 미구현** — `conf` 의 `autoindex on` 이 parser 만 통과하고 핸들러에서 사용 안 됨. `GET /uploads/` 가 404. 네트워크 쪽이 `cgi-bin/list_uploads.py` CGI 로 우회 중. `RequestHandler::handleGet` 에 `opendir/readdir` 기반 리스팅 추가 필요.
- **URL 디코딩 부재** — `HttpRequest::parse` / `RequestHandler::init` 어디서도 `%XX` 디코딩 없음. 공백 포함 파일명 업로드/접근 시 404. `handlePost` / `handleDelete` 양쪽 모두 영향.
- **`RequestHandler::handleCgiResponse` 강건화** — `Status: 200` 같이 reason 없는 케이스에서 `value.substr(4)` 가 throw → uncaught 시 서버 전체 종료. `value.length() > 4` 가드 필요.

### 🟡 컨벤션 명시화 / 후속 보강

- **`HttpRequest::parse` 가 헤더 키를 lowercase 로 저장** — 의도된 RFC 부합 동작이나 hpp `getHeaders()` 주석에 한 줄 명시 부탁. 향후 호출자 전부 이 컨벤션을 따라야 함.
- **`Cgi.hpp` 빌드 차단** — `ssize_t` 위해 `<sys/types.h>` 추가했음 (`514aa48`). 본 commit 은 HTTP 영역을 건드린 것이므로 HTTP 담당자가 동일 fix 를 본인 PR 흐름에 흡수해도 OK.
- **`HttpResponse::buildResponse` 가 status_code 기반 표준 reason phrase 자체 매핑하면** 우리 쪽 `sendErrorResponse` 의 switch 가 사라질 수 있음. 옵션.
- **`Cgi::~Cgi` 에서 자식 프로세스 회수 보장** — `kill(pid, SIGKILL)` + `waitpid` 로 escalation. 우리 쪽 `removeClient` 가 보강해서 좀비는 차단됐지만, Cgi 객체 단독 라이프사이클에서도 보험 권장.
- **`ServerSocket::init` 의 fcntl/listen 실패 경로 double-close (B-3)** — `close(_server_fd)` 후 `_server_fd = -1` 미설정 → throw 후 `~ServerSocket` 가 다시 close. 시작 시점이라 실전 충돌 가능성은 낮지만 결함.

### 🟢 합의 사항

- `Config::addServerBlock` 호출은 `ServerManager::init` 이후엔 절대 발생하지 않도록 한다 (vector realloc 시 포인터 무효화 방지). 현재 흐름은 이 조건을 자연스럽게 충족.

---

## 6. 남은 우선순위

### 추천 진행 순서 (2026-06-06 갱신)

| 순서 | 항목 | 영역 | 비고 |
|---|---|---|---|
| 1 | **`allow_methods` 미적용** | HTTP 팀 | 서브젝트 핵심 요구사항. `curl -X POST /` 한 번에 발견. 평가 즉시 탈락 |
| 2 | **dev 92bb6b4 회귀** Cgi.cpp double-close | HTTP 팀 | 모든 CGI 요청 영향, 시지 평가 즉시 차단 |
| 3 | **autoindex 구현** | HTTP 팀 | `/uploads/` 디렉터리 보기, 평가 항목 |
| 4 | **B-fu** Phantom EPOLLOUT — `[accept] → sent 0 bytes` | 본인 | 시지 CGI c≥5 의 잔여 실패 주범 |
| 5 | **#8** keep-alive buffer 보존 | 본인 | 시지 정적 GET keep-alive 6% 실패 재현 |
| 6 | **A-2** handleAccept 루프 | 본인 | 404 꼬리 1.4s 의 원인. 5줄 패치 |
| 7 | **handleCgiResponse 가드** | HTTP 팀 | uncaught exception → 서버 종료 위험 |
| 8 | **#13** accept EMFILE 가드 | 본인 | 실평가에서 거의 안 보이지만 안전 |
| 9 | **#16** CGI 동시 readiness | 본인 | latency only, 후순위 |

### Chunked 디코딩 분담

- **현재 상태**: 네트워크 쪽이 종결 마커까지 누적 (`isRequestComplete`)
- **남은 결정**: 청크 디코딩을 누가 책임지는가
  - 옵션 A: 네트워크가 디코딩된 body 를 만들어 `HttpRequest` 에 넘김
  - 옵션 B: `HttpRequest::parse` 가 디코딩 (dev 의 "chunked 추가" 커밋이 이쪽인지 확인 필요)
- 어느 쪽이든 결정 후 한 번 더 정리.

---

## 7. 빌드 검증 흐름

로컬 macOS 는 epoll 미지원 → Dockerfile 기반 우분투 빌드.

```bash
docker build -t webserv-build-check .
```

- 컴파일 단계 10/10 통과 (모든 cpp)
- 링크 통과 — `/app/webserv` 바이너리 생성 확인 (502160 bytes)

---

## 8. 참고

- 사용 가능한 시스템콜 / 외부 함수는 42 Webserv 과제 subject 기준
- 평가 우선순위: HTTP/1.1 — 쿠키/세션(보너스) 제외
- 클라이언트 본문 크기 제한은 `ServerBlock::getClientMaxBodySize()` 를 통해 ServerBlock 별로 결정, 0 은 제한 없음

---

## 9. siege 결과 (2026-06-06)

Docker 컨테이너(`webserv-siege` 이미지, 포트 8080) 상대로 macOS host 에서 siege 4.1.7 구동.

### 시나리오별

| # | 시나리오 | 동시성 | Availability | TPS | Failed | 비고 |
|---|---|---|---|---|---|---|
| 1 | 정적 GET keep-alive | c=20, 15s | 94.01 % | 2527 | 1043 | siege abort. #8 의심 |
| 2 | 정적 GET close | c=15, 15s | **100.00 %** | 3631 | 0 | ✅ 완벽 |
| 3 | 404 close | c=15, 15s | 98.29 % | 3847 | 1035 | 꼬리 1.4s. A-2 의심 |
| 4 | CGI close | c=8, 10s | 4.52 % (`136a885` 후) → **43.90 %** (`939dcc9` 후) | 413 | 1025 | B-fu phantom EPOLLOUT 잔존 |
| 4b | CGI close | c=2, 5s | **99.92 %** | 246 | 1 | 낮은 동시성 안정 |
| 4c | CGI close | c=5, 5s | **81.32 %** | 432 | 547 | 패치 전엔 crash |
| 4d | CGI close | c=15, 10s | 30.39 % | 425 | 1033 | 서버 생존, 회복은 못함 |
| 5 | mixed URLs | c=10, 10s | 41.44 % | 66 | 985 | CGI 가 발목 |

### 패치 단계별 (CGI 한정)

| 시점 | c=2 | c=5 | c=8 | c=15 |
|---|---|---|---|---|
| 패치 전 | 98.5 % | crash (segfault) | 0.29 % (실질 crash) | crash |
| `136a885` (A-1) | 99.9 % | crash 여전 | 4.5 % | crash |
| `939dcc9` (B) | 99.9 % | **81.3 %** | **43.9 %** | 30.4 % (생존) |

### 빌드/실행 노트

- `docker build -t webserv-siege .` → 이미지 빌드
- `docker run -d --name webserv-test -p 8080:8080 webserv-siege` 로 기동
- macOS 호스트에서 `siege -b -c <N> -t <T>s -H "Connection: close" http://localhost:8080/<path>`
- siege 설정 파일: `~/.siege/siege.conf` (기본값 그대로)
