*This project has been created as part of the 42 curriculum by taewonki, juyoukim, jaemyu.*

> English version: [README.md](./README.md)

# 42_Webserv

C++98 로 작성한 HTTP/1.1 웹서버.

## Description

42 Webserv 과제 구현. nginx 스타일 구성 파일을 읽어 단일 프로세스에서 `epoll` 기반 이벤트 루프로 다수 연결을 처리한다. GET / POST / DELETE 메서드, 정적 파일 서빙, 파일 업로드, CGI(Python/PHP) 실행, keep-alive, `error_page` 매핑, `client_max_body_size`, chunked Transfer-Encoding 종결 인식까지 다룬다.

### Goal

- nginx 와 유사한 라우팅 / 메서드 제한 / CGI 위임 동작을 단일 프로세스 이벤트 루프로 재현
- HTTP/1.1 핵심 (RFC 7230 / 7231) 준수
- 부하 도구(siege) 하에서 높은 가용성 확보 — 정적 GET (Connection: close) 시나리오에서 100%

## Instructions

### 빌드

```bash
make            # webserv 바이너리 생성
make re         # 전체 재빌드
make clean      # 오브젝트만 삭제
make fclean     # 오브젝트 + 바이너리 삭제
```

요구: `c++` (g++ 또는 clang++), GNU make. 외부 라이브러리 의존 없음. 컴파일러 플래그 `-Wall -Wextra -Werror -std=c++98`.

### 실행

```bash
./webserv                       # conf/default.conf 사용
./webserv conf/<your>.conf      # 지정 conf 사용
```

기본 conf 는 `0.0.0.0:8080` 에서 리슨. 종료는 `Ctrl-C`.

### 동작 확인

```bash
curl http://localhost:8080/                                   # 200, 정적 index
curl http://localhost:8080/cgi-bin/hello.py                   # 200, CGI
curl -X POST -F file=@<path> http://localhost:8080/uploads/   # 201, 업로드
curl -X DELETE http://localhost:8080/uploads/<filename>       # 200, 삭제
```

브라우저로 보면 더 편한 페이지들:
- `/` — 라우트 네비게이션
- `/uploads.html` — 업로드/목록/삭제 테스트 UI
- `/error.html` — 에러 페이지(400/403/404/405/413/5xx) 미리보기

## Features

- HTTP/1.1 method: **GET / POST / DELETE**
- 정적 파일 서빙 + 디렉터리 기본 인덱스
- 파일 업로드 (`multipart/form-data`)
- CGI (`.py` / `.php`) — 비동기 pipe I/O
- keep-alive 지원
- `error_page` 매핑 (per ServerBlock)
- `client_max_body_size` 제한 (per ServerBlock)
- `Content-Length` / `Transfer-Encoding: chunked` 종결 인식
- 다중 ServerBlock + 다중 `listen` (host:port)
- `server_name` 기반 가상 호스팅
- 클라이언트 idle / CGI 실행 타임아웃 (60s / 30s) 자동 정리
- CGI 좀비/orphan 자식 자동 회수
- 에러 응답 시 `Connection: close` 강제
- RFC 7230 §3.3.3 위반 조합 (중복 CL, CL+TE 공존) → 400 차단

## Architecture / 역할 분담

세 영역으로 나눠 담당했다.

| 영역 | 담당 | 주요 클래스 |
|---|---|---|
| Config 파싱 | **jaemyu** | `Config`, `ConfigParser`, `ServerBlock`, `Location` |
| 메인루프 / 이벤트 디스패치 / 네트워크 | **taewonki** | `ServerManager`, `ServerSocket`, `ClientSocket` |
| HTTP / CGI 로직 | **juyoukim** | `HttpRequest`, `HttpResponse`, `RequestHandler`, `Cgi` |

### Project Layout

```
.
├── conf/                # 서버 설정 (default.conf 등)
├── includes/            # 헤더
├── srcs/
│   ├── main.cpp
│   ├── core/            # network / event-loop  (taewonki)
│   ├── config/          # config parser         (jaemyu)
│   └── http/            # HTTP / CGI            (juyoukim)
├── cgi-bin/             # CGI 스크립트 샘플
├── www/                 # 정적 페이지 + 에러 페이지
├── Makefile
└── README.md
```

## Technical Choices

- **C++98 표준** — 과제 제약. STL 컨테이너/이터레이터 사용, C++11 이후 기능 (`auto`, lambda, smart pointer) 미사용
- **epoll (Linux), Level-Triggered** — 부분 read/write 는 LT 의 자동 재발화에 위임. ET 의 까다로운 backlog 관리 회피
- **단일 프로세스 / 단일 스레드** — 모든 I/O 비차단, `epoll_wait` 한 루프에서 모든 fd 디스패치
- **CGI 비동기** — `fork` + `pipe` 후 두 pipe fd 를 epoll 에 등록. 부모는 본 루프로 즉시 복귀
- **타임아웃** — idle 60s / CGI 30s. `sweepTimeouts()` 가 매 cycle 끝에 만료 클라이언트를 `SIGKILL` + `waitpid` + 정리
- **errno 검사 금지 룰 준수** — `recv`/`send`/`read`/`write` 후 errno 미참조. 부분 I/O 와 EAGAIN 모두 LT 재발화로 자연 해결, 진짜 hang 은 타임아웃이 정리

## Resources

### References

- [RFC 7230](https://datatracker.ietf.org/doc/html/rfc7230) — HTTP/1.1 Message Syntax and Routing
- [RFC 7231](https://datatracker.ietf.org/doc/html/rfc7231) — HTTP/1.1 Semantics and Content
- [RFC 3875](https://datatracker.ietf.org/doc/html/rfc3875) — The Common Gateway Interface (CGI)
- [nginx documentation](https://nginx.org/en/docs/) — directive 의미와 라우팅 동작 참조
- man pages: [epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html), [accept(2)](https://man7.org/linux/man-pages/man2/accept.2.html), [pipe(2)](https://man7.org/linux/man-pages/man2/pipe.2.html), [fork(2)](https://man7.org/linux/man-pages/man2/fork.2.html), [waitpid(2)](https://man7.org/linux/man-pages/man2/waitpid.2.html)
- [42 Webserv subject](https://cdn.intra.42.fr/pdf/pdf/154361/en.subject.pdf)
- [siege](https://www.joedog.org/siege-home/) — 부하 테스트 도구

### AI 활용

본 프로젝트는 Anthropic **Claude** (Claude Code, `claude-opus-4-7` 모델) 를 아래 작업에 활용했다.

- **코드 리뷰 / 정적 분석**: 네트워크 레이어의 epoll 디스패치, FD 라이프사이클, CGI 프로세스 회수 흐름을 점검. 발견된 항목의 우선순위 정리
- **부하 테스트 결과 해석**: siege 결과 (특히 CGI c≥5 SIGSEGV) 의 근본 원인 추적, `[response] sent 0 bytes` 등 비정상 로그 패턴 진단
- **회귀 검증**: 패치 적용 전후 동일 시나리오 비교 측정
- **문서화 보조**: 진단/우선순위/시지 결과 표 작성
- **평가용 보조 자산**: `/uploads.html`, `/error.html` 같은 테스트/미리보기 페이지, `cgi-bin/list_uploads.py` (autoindex 우회용) 작성
- **수정 사항 검토**: 본인 영역(network) 외의 코드 변경이 필요할 때 영향 평가 후 해당 담당자에게 공유

핵심 설계 결정 (상태 기계, 모듈 분할, 메시지 파싱 전략) 은 사람이 수행했다. AI 가 생성한 코드는 모두 사람이 검토 + 빌드 + 테스트 통과 후 머지했다.

## Code Convention

- 변수명: `snake_case`
- 함수명: `camelCase` (getter / setter 는 멤버 변수와 이름 일치)
- 생성자 / setter 파라미터: 멤버 변수와 동일 이름
- 한 줄로 표현 가능한 함수 (주로 getter) 는 한 줄로
- 생성자는 객체 생성만 담당. 초기값 할당은 별도 `init()` 메서드로 분리 (생성자 예외 시 소멸자 미호출 방지)
- **RAII**: 멤버 변수 해제는 본인 클래스의 소멸자에서만 수행. 다른 클래스에 해제 위임 금지
