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
| 8 | keep-alive buffer 손실 | ❌ |
| 9 | Host 검증 | ✅ `2a07321` |
| 10 | reason phrase | ✅ `2a07321` |
| 11 | Content-Length atoi | ✅ `f1c1dd9` |
| 12 | removeClient 가드 | ❌ |
| 13 | accept() 실패 | ❌ |
| 14 | getaddrinfo NULL | ❌ |
| 15 | addCgiFd 실패 처리 | ❌ |
| 16 | CGI 동시 readiness | ❌ |
| 17 | CGI 좀비 회수 | ✅ `99ad1d9` |
| 추가 | handleRead 상태 가드 | ✅ `99ad1d9` |
| 추가 | dead member `_cgi` | ✅ `b911ec8` |
| 추가 | resetForKeepAlive 핸들러 갱신 | ✅ `b911ec8` |
| 추가 | Location 매칭 wiring | ✅ `b7f78ed` |
| 추가 | Makefile 보강 | ✅ `b7f78ed` |

**진척**: 처음 진단 17개 중 **11개 완료**, 진단 외 추가 5개 모두 완료.

---

## 5. HTTP / Config 담당자 공유 항목

### 🟢 해소됨

- ~~**`HttpRequest::HttpRequest()` / `~HttpRequest()` 정의 누락**~~ — 어느 시점에 추가되어 링크 통과 확인됨. 직전 commit `f1c1dd9` 이후 진행한 타임아웃 작업에서 Docker 빌드 결과 `/app/webserv` 바이너리 생성 성공.

### 🟡 컨벤션 명시화 / 후속 보강

- **`HttpRequest::parse` 가 헤더 키를 lowercase 로 저장** — 의도된 RFC 부합 동작이나 hpp `getHeaders()` 주석에 한 줄 명시 부탁. 향후 호출자 전부 이 컨벤션을 따라야 함.
- **`Cgi.hpp` 빌드 차단** — `ssize_t` 위해 `<sys/types.h>` 추가했음 (`514aa48`). 본 commit 은 HTTP 영역을 건드린 것이므로 HTTP 담당자가 동일 fix 를 본인 PR 흐름에 흡수해도 OK.
- **`RequestHandler::handleCgiResponse` 강건화** — `Status: 200` 같이 reason 없는 케이스에서 `value.substr(4)` 가 throw 할 수 있음. 길이 가드 추가 필요.
- **`HttpResponse::buildResponse` 가 status_code 기반 표준 reason phrase 자체 매핑하면** 우리 쪽 `sendErrorResponse` 의 switch 가 사라질 수 있음. 옵션.
- **`Cgi::~Cgi` 에서 자식 프로세스 회수 보장** — `kill(pid, SIGKILL)` + `waitpid` 로 escalation. 우리 쪽 WNOHANG 은 *이미* 종료된 자식만 회수.

### 🟢 합의 사항

- `Config::addServerBlock` 호출은 `ServerManager::init` 이후엔 절대 발생하지 않도록 한다 (vector realloc 시 포인터 무효화 방지). 현재 흐름은 이 조건을 자연스럽게 충족.

---

## 6. 남은 우선순위

### 추천 진행 순서

| 순서 | 항목 | 비고 |
|---|---|---|
| 1 | **#15** `addCgiFd` 실패 처리 | 무한 매달림 방지 |
| 2 | **#12, #14** `removeClient` 가드 / `getaddrinfo` NULL 가드 | cleanup 묶음 |
| 3 | **#8** keep-alive buffer 보존 (파이프라이닝) | 평가 시 보지 않을 가능성 높음, 후순위 |
| 4 | **#13, #16** accept EMFILE / CGI 동시 readiness | 인지만, 평가에서 거의 안 봄 |

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
